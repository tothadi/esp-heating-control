// OpenTherm master: polls the boiler like the ESPHome opentherm component (sync mode) does - status (ID0) with
// the CH/DHW enable bits, the control setpoint (ID1) and the read-only values - one frame every 250 ms, status
// every second frame, so status/setpoint are refreshed well within the 1 s the specification requires.
#include <math.h>
#include <string.h>
#include "opentherm.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "opentherm";

enum { MSG_READ = 0, MSG_WRITE = 1, MSG_READ_ACK = 4, MSG_WRITE_ACK = 5, MSG_DATA_INVALID = 6, MSG_UNKNOWN_ID = 7 };
enum { ID_STATUS = 0, ID_TSET = 1, ID_FAULT = 5, ID_RELMOD = 17, ID_PRESSURE = 18, ID_TBOILER = 25, ID_TDHW = 26,
       ID_TOUTSIDE = 27, ID_TRET = 28 };

static ot_state_t s_st;
static SemaphoreHandle_t s_lock;
static int64_t s_last_ok_us;
static uint8_t s_unsupported[256 / 8];

static uint32_t frame(uint8_t type, uint8_t id, uint16_t value)
{
    uint32_t f = ((uint32_t) (type & 7) << 28) | ((uint32_t) id << 16) | value;
    uint32_t p = f;
    p ^= p >> 16; p ^= p >> 8; p ^= p >> 4; p ^= p >> 2; p ^= p >> 1;
    if (p & 1) f |= 0x80000000u;
    return f;
}

static float f88(uint16_t v) { return (int16_t) v / 256.0f; }

static float *value_ptr(uint8_t id)
{
    switch (id) {
    case ID_TBOILER: return &s_st.t_boiler;
    case ID_TRET: return &s_st.t_return;
    case ID_TDHW: return &s_st.t_dhw;
    case ID_TOUTSIDE: return &s_st.t_outside;
    case ID_RELMOD: return &s_st.rel_mod;
    case ID_PRESSURE: return &s_st.ch_pressure;
    case ID_FAULT: return &s_st.oem_fault_code;
    default: return NULL;
    }
}

static void exchange(uint8_t type, uint8_t id, uint16_t value)
{
    if (s_unsupported[id / 8] & (1 << (id % 8))) return;
    uint32_t resp;
    esp_err_t err = ot_phy_transact(frame(type, id, value), &resp);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "ID%u: %s", id, esp_err_to_name(err));
        return;
    }
    const uint8_t rtype = (resp >> 28) & 7, rid = (resp >> 16) & 0xFF;
    const uint16_t rval = resp & 0xFFFF;
    if (rid != id) return;
    s_last_ok_us = esp_timer_get_time();
    xSemaphoreTake(s_lock, portMAX_DELAY);
    float *vp = value_ptr(id);
    if (rtype == MSG_UNKNOWN_ID) {
        if (id != ID_STATUS && id != ID_TSET) {
            s_unsupported[id / 8] |= 1 << (id % 8);
            ESP_LOGW(TAG, "Boiler does not support data ID %u", id);
        }
        if (vp) *vp = NAN;
    } else if (rtype == MSG_DATA_INVALID) {
        if (vp) *vp = NAN;
    } else if (rtype == MSG_READ_ACK || rtype == MSG_WRITE_ACK) {
        if (id == ID_STATUS) {
            s_st.fault = rval & 0x01;
            s_st.ch_active = rval & 0x02;
            s_st.dhw_active = rval & 0x04;
            s_st.flame = rval & 0x08;
        } else if (id == ID_FAULT) {
            s_st.oem_fault_code = rval & 0xFF;
        } else if (vp) {
            *vp = f88(rval);
        }
    }
    xSemaphoreGive(s_lock);
}

static void ot_task(void *arg)
{
    static const uint8_t reads[] = {ID_TBOILER, ID_TRET, ID_TDHW, ID_TOUTSIDE, ID_RELMOD, ID_PRESSURE, ID_FAULT};
    int step = 0, ri = 0;
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(250));
        xSemaphoreTake(s_lock, portMAX_DELAY);
        const bool ch = s_st.ch_enable, dhw = s_st.dhw_enable;
        const float tset = s_st.t_set;
        xSemaphoreGive(s_lock);
        if ((step & 1) == 0) {
            exchange(MSG_READ, ID_STATUS, (uint16_t) ((ch ? 0x01 : 0) | (dhw ? 0x02 : 0)) << 8);
        } else if ((step & 3) == 1) {
            if (!isnan(tset)) exchange(MSG_WRITE, ID_TSET, (uint16_t) (int16_t) lroundf(tset * 256.0f));
        } else {
            exchange(MSG_READ, reads[ri], 0);
            ri = (ri + 1) % sizeof(reads);
        }
        step++;
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_st.comm_ok = s_last_ok_us != 0 && esp_timer_get_time() - s_last_ok_us < 10000000;
        xSemaphoreGive(s_lock);
    }
}

esp_err_t ot_master_start(int tx_gpio, int rx_gpio)
{
    s_lock = xSemaphoreCreateMutex();
    s_st = (ot_state_t){.t_boiler = NAN, .t_return = NAN, .t_dhw = NAN, .t_outside = NAN, .rel_mod = NAN,
                        .ch_pressure = NAN, .oem_fault_code = NAN, .t_set = NAN};
    esp_err_t err = ot_phy_init(tx_gpio, rx_gpio);
    if (err != ESP_OK) return err;
    return xTaskCreatePinnedToCore(ot_task, "opentherm", 3072, NULL, 12, NULL, 1) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

void ot_master_get(ot_state_t *out)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_st;
    xSemaphoreGive(s_lock);
}

#define SETTER(name, field, type) \
    void name(type v) { xSemaphoreTake(s_lock, portMAX_DELAY); s_st.field = v; xSemaphoreGive(s_lock); }
SETTER(ot_master_set_ch_enable, ch_enable, bool)
SETTER(ot_master_set_dhw_enable, dhw_enable, bool)
SETTER(ot_master_set_t_set, t_set, float)
