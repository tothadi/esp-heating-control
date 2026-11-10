// Shared I2C bus (IO8 SDA, IO18 SCL, 400 kHz). The new i2c_master driver serialises transactions per bus.
#include "hc_drivers.h"
#include "hc_board.h"
#include "hc_i2c_priv.h"
#include "esp_log.h"

#define I2C_TIMEOUT_MS 50
static const char *TAG = "hc_i2c";
static i2c_master_bus_handle_t s_bus;
static struct { uint8_t addr; i2c_master_dev_handle_t dev; } s_devs[8];

esp_err_t hc_i2c_init(void)
{
    if (s_bus) return ESP_OK;
    i2c_master_bus_config_t cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = HC_PIN_I2C_SDA,
        .scl_io_num = HC_PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = false,   // external pull-ups on the board
    };
    esp_err_t err = i2c_new_master_bus(&cfg, &s_bus);
    if (err != ESP_OK) return err;
    static const uint8_t addrs[] = {HC_ADDR_MCP_RELAYS, HC_ADDR_MCP_IO, HC_ADDR_ADS_A, HC_ADDR_ADS_B,
                                    HC_ADDR_OLED, HC_ADDR_RTC};
    for (int i = 0; i < sizeof(addrs); i++) {
        i2c_device_config_t dc = {.dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = addrs[i],
                                  .scl_speed_hz = 400000};
        s_devs[i].addr = addrs[i];
        ESP_ERROR_CHECK(i2c_master_bus_add_device(s_bus, &dc, &s_devs[i].dev));
        ESP_LOGI(TAG, "0x%02x %s", addrs[i], i2c_master_probe(s_bus, addrs[i], I2C_TIMEOUT_MS) == ESP_OK ? "found" : "MISSING");
    }
    return ESP_OK;
}

i2c_master_dev_handle_t hc_i2c_dev(uint8_t addr)
{
    for (int i = 0; i < 8; i++)
        if (s_devs[i].dev && s_devs[i].addr == addr) return s_devs[i].dev;
    return NULL;
}

esp_err_t hc_i2c_write(uint8_t addr, const uint8_t *data, size_t len)
{
    i2c_master_dev_handle_t d = hc_i2c_dev(addr);
    return d ? i2c_master_transmit(d, data, len, I2C_TIMEOUT_MS) : ESP_ERR_INVALID_STATE;
}

esp_err_t hc_i2c_write_read(uint8_t addr, const uint8_t *w, size_t wlen, uint8_t *r, size_t rlen)
{
    i2c_master_dev_handle_t d = hc_i2c_dev(addr);
    return d ? i2c_master_transmit_receive(d, w, wlen, r, rlen, I2C_TIMEOUT_MS) : ESP_ERR_INVALID_STATE;
}
