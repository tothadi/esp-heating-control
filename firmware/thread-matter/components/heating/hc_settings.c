// Settings: defaults (those of the ESPHome build / reference installation) and persistence in NVS.
// Saved as one blob, 5 s after the last change, so a burst of changes costs one flash write.
#include <string.h>
#include "hc_priv.h"
#include "esp_log.h"
#include "nvs.h"

#define HC_CFG_MAGIC 0x48430002u   // bump when hc_settings_t changes layout (falls back to defaults)

hc_settings_t g_cfg;
static bool s_dirty;
static uint32_t s_dirty_ms;
static uint32_t s_change_stamp = 1;

typedef struct {
    const char *name; uint8_t heat; float comfort, night, boost; uint8_t night_on, boost_on, floor; float limit;
} zdef_t;
static const zdef_t ZDEF[HC_ZONES] = {
    {"Living room", 1, 24.0f, 23.0f, 24.0f, 1, 0, FLOOR_TILES, 28.0f},
    {"Hallway", 1, 24.0f, 24.0f, 24.0f, 0, 0, FLOOR_TILES, 28.0f},
    {"Bedroom", 1, 24.0f, 24.0f, 24.0f, 0, 0, FLOOR_LAMINATE, 34.0f},
    {"Kids room 1", 1, 23.5f, 23.0f, 23.5f, 1, 0, FLOOR_LAMINATE, 36.0f},
    {"Kids room 2", 1, 23.5f, 23.0f, 23.5f, 1, 0, FLOOR_LAMINATE, 34.0f},
    {"Bathroom", 1, 23.5f, 23.0f, 24.0f, 1, 1, FLOOR_TILES, 28.0f},
    {"Zone 7", 0, 22.0f, 20.0f, 22.0f, 0, 0, FLOOR_NONE, 28.0f},
    {"Zone 8", 0, 22.0f, 20.0f, 22.0f, 0, 0, FLOOR_NONE, 28.0f},
};

void hc_settings_defaults(hc_settings_t *s)
{
    memset(s, 0, sizeof(*s));
    s->magic = HC_CFG_MAGIC;
    for (int i = 0; i < HC_ZONES; i++) {
        hc_zone_cfg_t *z = &s->zone[i];
        strlcpy(z->name, ZDEF[i].name, sizeof(z->name));
        z->mode_heat = ZDEF[i].heat;
        z->target = ZDEF[i].comfort;
        z->comfort = ZDEF[i].comfort;
        z->night = ZDEF[i].night;
        z->boost = ZDEF[i].boost;
        z->night_on = ZDEF[i].night_on;
        z->boost_on = ZDEF[i].boost_on;
        z->source = SRC_ROOM;
        z->floor_type = ZDEF[i].floor;
        z->floor_limit = ZDEF[i].limit;
        z->floor_cal = 0.0f;
    }
    s->heating_mode = 0;
    s->flow_mode = 0;
    s->pump_mode[0] = s->pump_mode[1] = PUMP_OFF;
    s->dhw_enable = 1;
    s->oled_rotate180 = 0;
    s->curve_ref = 24.0f;
    s->curve_slope = 0.45f;
    s->curve_offset = 7.0f;
    s->curve_min = 30.0f;
    s->curve_max = 42.0f;
    s->flow_max = 45.0f;
    s->boost_max = 8.0f;
    s->flow_manual = 35.0f;
    s->floor_offset_tiles = 2.0f;
    s->floor_offset_laminate = 3.0f;
    s->night_floor_target = 25.0f;
    s->pump_overrun = 5.0f;
    s->sched_day_start = 5 * 60;
    s->sched_night_start = 23 * 60;
    s->sched_boost_start = 17 * 60 + 30;
    s->sched_boost_end = 20 * 60;
    s->ot_t_set = 30.0f;
    s->ntc_supply[0] = s->ntc_supply[1] = 3.30f;
    s->ntc_ref = 10000.0f;
    s->ntc_nominal = 10000.0f;
    s->ntc_b = 3950.0f;
    s->ntc_open_v = 3.2f;
    s->ntc_short_v = 0.05f;
    s->fallback_outdoor = 5.0f;
    s->room_timeout_s = 300;
    s->sensor_timeout_s = 7200;
    strlcpy(s->timezone, "CET-1CEST,M3.5.0,M10.5.0/3", sizeof(s->timezone));   // Europe/Budapest
}

void hc_settings_load(void)
{
    hc_settings_defaults(&g_cfg);
    nvs_handle_t h;
    if (nvs_open("hc", NVS_READONLY, &h) != ESP_OK) return;
    hc_settings_t tmp;
    size_t len = sizeof(tmp);
    if (nvs_get_blob(h, "cfg", &tmp, &len) == ESP_OK && len == sizeof(tmp) && tmp.magic == HC_CFG_MAGIC) {
        g_cfg = tmp;
        g_cfg.timezone[sizeof(g_cfg.timezone) - 1] = 0;
        for (int i = 0; i < HC_ZONES; i++) g_cfg.zone[i].name[sizeof(g_cfg.zone[i].name) - 1] = 0;
        ESP_LOGI(TAG_HC, "Settings restored from flash");
    } else {
        ESP_LOGW(TAG_HC, "No stored settings: using defaults");
    }
    nvs_close(h);
}

void hc_settings_mark_dirty(void)
{
    s_dirty = true;
    s_dirty_ms = hc_millis();
    s_change_stamp++;
}

bool hc_settings_changed_since(uint32_t *stamp)
{
    hc_lock();
    bool changed = *stamp != s_change_stamp;
    *stamp = s_change_stamp;
    hc_unlock();
    return changed;
}

void hc_settings_save_if_due(void)
{
    if (!s_dirty || hc_millis() - s_dirty_ms < 5000) return;
    s_dirty = false;
    nvs_handle_t h;
    esp_err_t err = nvs_open("hc", NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_blob(h, "cfg", &g_cfg, sizeof(g_cfg));
        if (err == ESP_OK) err = nvs_commit(h);
        nvs_close(h);
    }
    if (err != ESP_OK) ESP_LOGE(TAG_HC, "Saving settings failed: %s", esp_err_to_name(err));
}

const char *hc_timezone(void) { return g_cfg.timezone; }
const char *hc_zone_name(int zone) { return (zone >= 0 && zone < HC_ZONES) ? g_cfg.zone[zone].name : ""; }
