// Heating core: hardware outputs, inputs, time keeping (DS3231 + SNTP) and the control task that runs the
// periodic jobs with the same periods as the ESPHome intervals.
#include <string.h>
#include <sys/time.h>
#include "hc_priv.h"
#include "hc_board.h"
#include "hc_drivers.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

hc_rt_t g_rt;
static SemaphoreHandle_t s_lock;
static bool s_oled_ok;

void hc_lock(void) { xSemaphoreTakeRecursive(s_lock, portMAX_DELAY); }
void hc_unlock(void) { xSemaphoreGiveRecursive(s_lock); }
uint32_t hc_millis(void) { return (uint32_t) (esp_timer_get_time() / 1000); }

bool hc_local_time(struct tm *out)
{
    time_t now = time(NULL);
    if (now < 1704067200) return false;   // before 2024-01-01: clock not set
    localtime_r(&now, out);
    return true;
}

static void apply_timezone(void)
{
    setenv("TZ", g_cfg.timezone, 1);
    tzset();
}

// --- outputs -------------------------------------------------------------------------------------------------

void hc_relay_set(int zone, bool on)
{
    if (hc_mcp_write_pin(HC_ADDR_MCP_RELAYS, zone, on) != ESP_OK) {
        ESP_LOGE(TAG_HC, "Zone %d relay write failed", zone + 1);
        return;
    }
    if (on) g_rt.zone_on_since[zone] = hc_millis();
    g_rt.relay[zone] = on;
    hc_mcp_write_pin(HC_ADDR_MCP_IO, 8 + zone, on);   // zone LED follows the relay
}

void hc_pump_set(int pump, bool on)
{
    if (hc_mcp_write_pin(HC_ADDR_MCP_RELAYS, 8 + pump, on) != ESP_OK) return;
    g_rt.pump[pump] = on;
    gpio_set_level(pump ? HC_PIN_LED_PUMP2 : HC_PIN_LED_PUMP1, on);
}

void hc_status_led(bool on) { gpio_set_level(HC_PIN_LED_STATUS, on); }

// --- inputs --------------------------------------------------------------------------------------------------

// Dry-contact thermostat inputs on MCP 0x21 GPA (GPA0..7 = TERM8..TERM1), contact closed = low = demand.
// ESPHome filter delayed_on_off: 2 s.
void hc_inputs_poll(uint32_t dt_ms)
{
    static uint32_t stable_ms[HC_ZONES];
    uint8_t v;
    if (hc_mcp_read_gpioa(HC_ADDR_MCP_IO, &v) != ESP_OK) return;
    hc_lock();
    for (int i = 0; i < HC_ZONES; i++) {
        const bool raw = !(v & (1 << (7 - i)));
        if (raw == g_rt.wired[i]) { stable_ms[i] = 0; continue; }
        stable_ms[i] += dt_ms;
        if (stable_ms[i] >= 2000) { g_rt.wired[i] = raw; stable_ms[i] = 0; }
    }
    hc_unlock();
}

bool hc_input_fresh(const hc_input_t *in)
{
    return !isnan(in->v) && hc_millis() - in->ms < g_cfg.sensor_timeout_s * 1000u;
}

static void set_input(hc_input_t *in, float v)
{
    hc_lock();
    in->v = v;
    in->ms = hc_millis();
    hc_unlock();
}

void hc_input_matter_room(int zone, float c) { if (zone >= 0 && zone < HC_ZONES) set_input(&g_rt.mx_room[zone], c); }
void hc_input_matter_outdoor(float c) { set_input(&g_rt.mx_outdoor, c); }
void hc_input_external_room(int zone, float c) { if (zone >= 0 && zone < HC_ZONES) set_input(&g_rt.ext_room[zone], c); }
void hc_input_external_outdoor(float c) { set_input(&g_rt.ext_outdoor, c); }

void hc_set_remote_status(bool configured, bool connected)
{
    hc_lock();
    g_rt.remote_configured = configured;
    g_rt.remote_connected = connected;
    hc_unlock();
}

void hc_set_network_status(const char *text)
{
    hc_lock();
    strlcpy(g_rt.net_text, text ? text : "", sizeof(g_rt.net_text));
    hc_unlock();
}

// Room temperature per zone (template sensor, 30 s): a mapped Matter sensor takes precedence; otherwise the
// external (MQTT) value while the remote link is up (dropped room_timeout after it went down).
void hc_room_update(void)
{
    for (int i = 0; i < HC_ZONES; i++) {
        float v = NAN;
        if (hc_input_fresh(&g_rt.mx_room[i])) v = g_rt.mx_room[i].v;
        else if (g_rt.remote_offline_s < (int) g_cfg.room_timeout_s && hc_input_fresh(&g_rt.ext_room[i]))
            v = g_rt.ext_room[i].v;
        g_rt.room[i] = v;
    }
}

// Outdoor temperature (60 s): Matter sensor -> MQTT value -> boiler's outdoor sensor (NaN if none).
void hc_outdoor_update(void)
{
    if (hc_input_fresh(&g_rt.mx_outdoor)) g_rt.outdoor_temp = g_rt.mx_outdoor.v;
    else if (g_rt.remote_offline_s < (int) g_cfg.room_timeout_s && hc_input_fresh(&g_rt.ext_outdoor))
        g_rt.outdoor_temp = g_rt.ext_outdoor.v;
    else g_rt.outdoor_temp = g_rt.ot.t_outside;
}

// --- time ----------------------------------------------------------------------------------------------------

static void sntp_synced(struct timeval *tv)
{
    ESP_LOGI(TAG_HC, "SNTP time synchronised; writing the RTC");
    hc_rtc_write(tv->tv_sec);
}

void hc_time_sntp_start(void)
{
    if (esp_sntp_enabled()) return;
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.cloudflare.com");
    sntp_set_time_sync_notification_cb(sntp_synced);
    esp_sntp_init();
}

// --- control task --------------------------------------------------------------------------------------------

static void control_task(void *arg)
{
    TickType_t last = xTaskGetTickCount();
    uint32_t t = 0;   // ms since start, in 10 ms steps
    for (;;) {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(10));
        t += 10;
        hc_lock();
        hc_button_poll(10);
        hc_unlock();
        if (t % 100 == 0) hc_inputs_poll(100);
        if (t % 250 == 0) {
            hc_lock();
            ot_master_get(&g_rt.ot);
            hc_status_led_run();
            hc_unlock();
        }
        if (t % 1000 == 0) {
            hc_ntc_sample();
            hc_lock();
            hc_pump_exercise_check();
            hc_settings_save_if_due();
            if (s_oled_ok) hc_display_render();
            hc_unlock();
            if (s_oled_ok) hc_oled_flush();
        }
        if (t % 5000 == 0) { hc_lock(); hc_display_rotate(); hc_unlock(); }
        if (t % 10000 == 0) {
            hc_lock();
            // remote link watchdog (the ESPHome "api.connected" interval)
            if (g_rt.remote_connected) g_rt.remote_offline_s = 0;
            else if (g_rt.remote_offline_s < 1000000) g_rt.remote_offline_s += 10;
            hc_zones_run();
            hc_unlock();
        }
        if (t % 30000 == 0) {
            hc_lock();
            hc_room_update();
            hc_schedule_run();
            hc_unlock();
        }
        if (t % 60000 == 0) {
            hc_lock();
            hc_outdoor_update();
            hc_flow_run();
            hc_unlock();
            hc_mcp_refresh();   // re-assert MCP directions/latches (recovers from a glitch on the power/IO board)
        }
    }
}

esp_err_t hc_start(void)
{
    s_lock = xSemaphoreCreateRecursiveMutex();
    hc_settings_load();
    apply_timezone();

    memset(&g_rt, 0, sizeof(g_rt));
    for (int i = 0; i < HC_ZONES; i++) {
        g_rt.floor[i] = g_rt.room[i] = g_rt.floor_tgt[i] = NAN;
        g_rt.sched_period[i] = -1;
        g_rt.mx_room[i].v = g_rt.ext_room[i].v = NAN;
    }
    g_rt.mx_outdoor.v = g_rt.ext_outdoor.v = NAN;
    g_rt.outdoor_temp = g_rt.flow_calc = NAN;
    for (int k = 0; k < 13; k++) g_rt.outdoor_hist[k] = NAN;

    // LEDs and button
    gpio_config_t out = {.pin_bit_mask = (1ULL << HC_PIN_LED_PUMP1) | (1ULL << HC_PIN_LED_PUMP2) |
                                         (1ULL << HC_PIN_LED_STATUS),
                         .mode = GPIO_MODE_OUTPUT};
    gpio_config(&out);
    gpio_set_level(HC_PIN_LED_PUMP1, 0);
    gpio_set_level(HC_PIN_LED_PUMP2, 0);
    gpio_set_level(HC_PIN_LED_STATUS, 0);
    gpio_config_t in = {.pin_bit_mask = 1ULL << HC_PIN_BUTTON, .mode = GPIO_MODE_INPUT};   // 10k pull-up on board
    gpio_config(&in);

    // I2C devices; relays, pumps and LEDs start off (ESPHome restore_mode ALWAYS_OFF)
    ESP_ERROR_CHECK(hc_i2c_init());
    if (hc_mcp_init() != ESP_OK) ESP_LOGE(TAG_HC, "MCP23017 init failed (power/IO board connected?)");
    s_oled_ok = hc_oled_init(g_cfg.oled_rotate180) == ESP_OK;
    if (!s_oled_ok) ESP_LOGW(TAG_HC, "OLED not found");

    // Clock from the DS3231 until SNTP is available
    time_t utc;
    if (hc_rtc_read(&utc) == ESP_OK && utc > 1704067200) {
        struct timeval tv = {.tv_sec = utc};
        settimeofday(&tv, NULL);
        ESP_LOGI(TAG_HC, "Time from RTC: %lld", (long long) utc);
    } else {
        ESP_LOGW(TAG_HC, "RTC time not valid: schedules wait for SNTP");
    }

    // Boiler: restored flow setpoint, hot water as configured, CH off until a zone has been open for 1 min
    esp_err_t err = ot_master_start(HC_PIN_OT_TX, HC_PIN_OT_RX);
    if (err != ESP_OK) ESP_LOGE(TAG_HC, "OpenTherm init failed: %s", esp_err_to_name(err));
    ot_master_set_t_set(g_cfg.ot_t_set);
    ot_master_set_dhw_enable(g_cfg.dhw_enable);
    ot_master_set_ch_enable(false);

    return xTaskCreatePinnedToCore(control_task, "heating", 6144, NULL, 8, NULL, 1) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

// Re-applies settings that need more than a value change (called by hc_json.c after a change).
void hc_settings_side_effects(void)
{
    apply_timezone();
    ot_master_set_dhw_enable(g_cfg.dhw_enable);
}
