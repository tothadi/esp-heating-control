// OpenTherm physical layer. TX: one RMT symbol per bit (two 500 us halves), idle = pin high (on this board OT_TX
// high = line low = OpenTherm idle; same polarity as the ESPHome build, which does not invert the out pin).
// RX: the board's OT_RX is low while the boiler draws its high current, so the logical level is the inverted pin
// (ESPHome: in_pin inverted). Decoder as in the ihormelnyk/ESPHome OpenTherm implementations: start bit, then each
// data bit is sampled on the first edge more than 750 us after the previous mid-bit edge.
#include <string.h>
#include "opentherm.h"
#include "driver/gpio.h"
#include "driver/rmt_tx.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "ot_phy";

typedef enum { RX_IDLE, RX_WAITING, RX_START_BIT, RX_RECEIVING, RX_READY, RX_INVALID } rx_state_t;

static rmt_channel_handle_t s_tx_chan;
static rmt_encoder_handle_t s_copy_enc;
static int s_rx_gpio = -1;
static volatile rx_state_t s_rx_state = RX_IDLE;
static volatile int64_t s_rx_ts;
static volatile uint32_t s_rx_frame;
static volatile int s_rx_bits;
static SemaphoreHandle_t s_rx_done;

static inline bool IRAM_ATTR rx_level(void) { return !gpio_get_level(s_rx_gpio); }   // true = active

static void IRAM_ATTR rx_isr(void *arg)
{
    const int64_t now = esp_timer_get_time();
    const bool active = rx_level();
    BaseType_t woken = pdFALSE;
    switch (s_rx_state) {
    case RX_WAITING:
        if (active) { s_rx_state = RX_START_BIT; s_rx_ts = now; }
        break;
    case RX_START_BIT:
        if (now - s_rx_ts < 750 && !active) {   // mid-bit edge of the start bit
            s_rx_state = RX_RECEIVING; s_rx_ts = now; s_rx_bits = 0; s_rx_frame = 0;
        } else {
            s_rx_state = RX_INVALID; xSemaphoreGiveFromISR(s_rx_done, &woken);
        }
        break;
    case RX_RECEIVING:
        if (now - s_rx_ts > 750) {
            if (s_rx_bits < 32) {
                // after a mid-bit edge to idle the bit is 1 (active -> idle), else 0
                s_rx_frame = (s_rx_frame << 1) | (active ? 0 : 1);
                s_rx_bits++;
                s_rx_ts = now;
            } else {
                s_rx_state = RX_READY; xSemaphoreGiveFromISR(s_rx_done, &woken);
            }
        }
        break;
    default:
        break;
    }
    if (woken) portYIELD_FROM_ISR();
}

esp_err_t ot_phy_init(int tx_gpio, int rx_gpio)
{
    s_rx_done = xSemaphoreCreateBinary();
    s_rx_gpio = rx_gpio;
    rmt_tx_channel_config_t tc = {
        .gpio_num = tx_gpio,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 1000000,
        .mem_block_symbols = 48,
        .trans_queue_depth = 2,
    };
    esp_err_t err = rmt_new_tx_channel(&tc, &s_tx_chan);
    if (err != ESP_OK) return err;
    rmt_copy_encoder_config_t ec = {};
    if ((err = rmt_new_copy_encoder(&ec, &s_copy_enc)) != ESP_OK) return err;
    if ((err = rmt_enable(s_tx_chan)) != ESP_OK) return err;
    // Idle the line: an empty-ish transmission ending at level 1
    rmt_symbol_word_t idle = {.level0 = 1, .duration0 = 100, .level1 = 1, .duration1 = 100};
    rmt_transmit_config_t txc = {.loop_count = 0, .flags.eot_level = 1};
    rmt_transmit(s_tx_chan, s_copy_enc, &idle, sizeof(idle), &txc);
    rmt_tx_wait_all_done(s_tx_chan, 100);

    gpio_config_t rc = {.pin_bit_mask = 1ULL << rx_gpio, .mode = GPIO_MODE_INPUT, .intr_type = GPIO_INTR_ANYEDGE};
    if ((err = gpio_config(&rc)) != ESP_OK) return err;
    err = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    return gpio_isr_handler_add(rx_gpio, rx_isr, NULL);
}

static bool parity_even(uint32_t f)
{
    f ^= f >> 16; f ^= f >> 8; f ^= f >> 4; f ^= f >> 2; f ^= f >> 1;
    return !(f & 1);
}

esp_err_t ot_phy_transact(uint32_t request, uint32_t *response)
{
    // Frame: start bit 1, 32 data bits (MSB first), stop bit 1. Bit 1 = active then idle; active = pin low.
    rmt_symbol_word_t sym[34];
    for (int i = 0; i < 34; i++) {
        const bool bit = (i == 0 || i == 33) ? true : (request >> (31 - (i - 1))) & 1;
        sym[i].duration0 = 500;
        sym[i].duration1 = 500;
        sym[i].level0 = bit ? 0 : 1;
        sym[i].level1 = bit ? 1 : 0;
    }
    s_rx_state = RX_IDLE;
    xSemaphoreTake(s_rx_done, 0);
    rmt_transmit_config_t txc = {.loop_count = 0, .flags.eot_level = 1};
    esp_err_t err = rmt_transmit(s_tx_chan, s_copy_enc, sym, sizeof(sym), &txc);
    if (err != ESP_OK) return err;
    rmt_tx_wait_all_done(s_tx_chan, 100);
    s_rx_ts = esp_timer_get_time();
    s_rx_state = RX_WAITING;
    // The slave answers 20..800 ms after the request; a frame takes 34 ms.
    if (xSemaphoreTake(s_rx_done, pdMS_TO_TICKS(850)) != pdTRUE) {
        // A complete frame whose stop-bit edge was the last edge: accept it if all 32 bits arrived.
        if (s_rx_state == RX_RECEIVING && s_rx_bits == 32) s_rx_state = RX_READY;
        else { s_rx_state = RX_IDLE; return ESP_ERR_TIMEOUT; }
    }
    const rx_state_t st = s_rx_state;
    s_rx_state = RX_IDLE;
    if (st != RX_READY) return ESP_ERR_INVALID_RESPONSE;
    const uint32_t f = s_rx_frame;
    if (!parity_even(f)) { ESP_LOGD(TAG, "parity error %08lx", (unsigned long) f); return ESP_ERR_INVALID_CRC; }
    *response = f;
    return ESP_OK;
}
