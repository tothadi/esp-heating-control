// JSON view of the settings and the state (used by the MQTT interface and the USB console).
// Setting names and ranges follow the entities of the ESPHome build.
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "hc_priv.h"
#include "hc_drivers.h"

void hc_settings_side_effects(void);

typedef enum { K_FLOAT, K_U8BOOL, K_ENUM, K_TIME, K_STR, K_U32 } ktype_t;
enum { F_SCHED_ALL = 1, F_SCHED_ZONE = 2, F_SIDE = 4 };

typedef struct {
    const char *key;
    ktype_t type;
    size_t off;
    float min, max;
    const char *const *opts;   // K_ENUM: NULL-terminated option names (index = stored value)
    uint8_t flags;
    size_t size;               // K_STR buffer size
} hc_key_t;

static const char *const OPT_AUTO_OFF[] = {"auto", "off", NULL};
static const char *const OPT_AUTO_MANUAL[] = {"auto", "manual", NULL};
static const char *const OPT_PUMP[] = {"auto", "on", "off", NULL};
static const char *const OPT_MODE[] = {"off", "heat", NULL};
static const char *const OPT_SOURCE[] = {"room", "wired", "floor", NULL};
static const char *const OPT_FLOOR[] = {"tiles", "laminate", "none", NULL};
// Aliases accepted on input (the ESPHome option names)
static const char *const ALIAS_SOURCE[] = {"room sensor", "wired thermostat", "floor only", NULL};
static const char *const ALIAS_FLOOR[] = {"tiles", "laminate", "no probe", NULL};

#define G(k, t, f, mn, mx, o, fl) {k, t, offsetof(hc_settings_t, f), mn, mx, o, fl, sizeof(((hc_settings_t *) 0)->f)}
static const hc_key_t GLOBAL_KEYS[] = {
    G("heating_mode", K_ENUM, heating_mode, 0, 0, OPT_AUTO_OFF, 0),
    G("flow_mode", K_ENUM, flow_mode, 0, 0, OPT_AUTO_MANUAL, 0),
    G("pump1_mode", K_ENUM, pump_mode[0], 0, 0, OPT_PUMP, 0),
    G("pump2_mode", K_ENUM, pump_mode[1], 0, 0, OPT_PUMP, 0),
    G("hot_water", K_U8BOOL, dhw_enable, 0, 0, NULL, F_SIDE),
    G("curve_ref", K_FLOAT, curve_ref, 15, 28, NULL, 0),
    G("curve_slope", K_FLOAT, curve_slope, 0.1f, 3.0f, NULL, 0),
    G("curve_offset", K_FLOAT, curve_offset, -10, 20, NULL, 0),
    G("curve_min", K_FLOAT, curve_min, 20, 50, NULL, 0),
    G("curve_max", K_FLOAT, curve_max, 25, 60, NULL, 0),
    G("flow_max", K_FLOAT, flow_max, 25, 60, NULL, 0),
    G("boost_max", K_FLOAT, boost_max, 0, 15, NULL, 0),
    G("flow_manual", K_FLOAT, flow_manual, 20, 60, NULL, 0),
    G("floor_offset_tiles", K_FLOAT, floor_offset_tiles, 0, 6, NULL, 0),
    G("floor_offset_laminate", K_FLOAT, floor_offset_laminate, 0, 8, NULL, 0),
    G("night_floor_target", K_FLOAT, night_floor_target, 18, 30, NULL, 0),
    G("pump_overrun", K_FLOAT, pump_overrun, 0, 30, NULL, 0),
    G("day_start", K_TIME, sched_day_start, 0, 0, NULL, F_SCHED_ALL),
    G("night_start", K_TIME, sched_night_start, 0, 0, NULL, F_SCHED_ALL),
    G("boost_start", K_TIME, sched_boost_start, 0, 0, NULL, F_SCHED_ALL),
    G("boost_end", K_TIME, sched_boost_end, 0, 0, NULL, F_SCHED_ALL),
    G("ntc_supply_a", K_FLOAT, ntc_supply[0], 3.0f, 3.6f, NULL, 0),
    G("ntc_supply_b", K_FLOAT, ntc_supply[1], 3.0f, 3.6f, NULL, 0),
    G("ntc_ref_resistor", K_FLOAT, ntc_ref, 1000, 100000, NULL, 0),
    G("ntc_nominal", K_FLOAT, ntc_nominal, 1000, 100000, NULL, 0),
    G("ntc_b_constant", K_FLOAT, ntc_b, 2000, 5000, NULL, 0),
    G("ntc_open_v", K_FLOAT, ntc_open_v, 1, 4, NULL, 0),
    G("ntc_short_v", K_FLOAT, ntc_short_v, 0, 1, NULL, 0),
    G("fallback_outdoor_temp", K_FLOAT, fallback_outdoor, -30, 30, NULL, 0),
    G("room_timeout", K_U32, room_timeout_s, 30, 86400, NULL, 0),
    G("sensor_timeout", K_U32, sensor_timeout_s, 300, 86400, NULL, 0),
    G("oled_rotate180", K_U8BOOL, oled_rotate180, 0, 0, NULL, F_SIDE),
    G("timezone", K_STR, timezone, 0, 0, NULL, F_SIDE),
};

#define Z(k, t, f, mn, mx, o, fl) {k, t, offsetof(hc_zone_cfg_t, f), mn, mx, o, fl, sizeof(((hc_zone_cfg_t *) 0)->f)}
static const hc_key_t ZONE_KEYS[] = {
    Z("name", K_STR, name, 0, 0, NULL, 0),
    Z("mode", K_ENUM, mode_heat, 0, 0, OPT_MODE, 0),
    Z("target", K_FLOAT, target, 10, 28, NULL, 0),
    Z("comfort", K_FLOAT, comfort, 10, 28, NULL, F_SCHED_ZONE),
    Z("night", K_FLOAT, night, 10, 28, NULL, F_SCHED_ZONE),
    Z("boost", K_FLOAT, boost, 10, 28, NULL, F_SCHED_ZONE),
    Z("night_setback", K_U8BOOL, night_on, 0, 0, NULL, F_SCHED_ZONE),
    Z("evening_boost", K_U8BOOL, boost_on, 0, 0, NULL, F_SCHED_ZONE),
    Z("source", K_ENUM, source, 0, 0, OPT_SOURCE, 0),
    Z("floor_type", K_ENUM, floor_type, 0, 0, OPT_FLOOR, 0),
    Z("floor_limit", K_FLOAT, floor_limit, 20, 45, NULL, 0),
    Z("floor_calibration", K_FLOAT, floor_cal, -5, 5, NULL, 0),
};

static int find_opt(const char *const *opts, const char *s)
{
    for (int i = 0; opts && opts[i]; i++)
        if (!strcasecmp(opts[i], s)) return i;
    return -1;
}

static bool parse_time(const char *s, uint16_t *out)
{
    int h, m;
    if (sscanf(s, "%d:%d", &h, &m) != 2 || h < 0 || h > 23 || m < 0 || m > 59) return false;
    *out = (uint16_t) (h * 60 + m);
    return true;
}

// Applies one value; returns false if it is invalid.
static bool apply_key(const hc_key_t *k, void *base, const cJSON *v)
{
    uint8_t *p = (uint8_t *) base + k->off;
    switch (k->type) {
    case K_FLOAT: {
        if (!cJSON_IsNumber(v)) return false;
        float f = (float) v->valuedouble;
        if (isnan(f)) return false;
        *(float *) p = f < k->min ? k->min : f > k->max ? k->max : f;
        return true;
    }
    case K_U32: {
        if (!cJSON_IsNumber(v)) return false;
        double d = v->valuedouble;
        *(uint32_t *) p = (uint32_t) (d < k->min ? k->min : d > k->max ? k->max : d);
        return true;
    }
    case K_U8BOOL:
        if (cJSON_IsBool(v)) *p = cJSON_IsTrue(v);
        else if (cJSON_IsNumber(v)) *p = v->valueint != 0;
        else if (cJSON_IsString(v)) *p = !strcasecmp(v->valuestring, "on") || !strcasecmp(v->valuestring, "true");
        else return false;
        return true;
    case K_ENUM: {
        int idx = -1;
        if (cJSON_IsNumber(v)) idx = v->valueint;
        else if (cJSON_IsString(v)) {
            idx = find_opt(k->opts, v->valuestring);
            if (idx < 0 && k->opts == OPT_SOURCE) idx = find_opt(ALIAS_SOURCE, v->valuestring);
            if (idx < 0 && k->opts == OPT_FLOOR) idx = find_opt(ALIAS_FLOOR, v->valuestring);
        }
        int n = 0;
        while (k->opts[n]) n++;
        if (idx < 0 || idx >= n) return false;
        *p = (uint8_t) idx;
        return true;
    }
    case K_TIME:
        return cJSON_IsString(v) && parse_time(v->valuestring, (uint16_t *) p);
    case K_STR:
        if (!cJSON_IsString(v) || !v->valuestring[0] || strlen(v->valuestring) >= k->size) return false;
        strlcpy((char *) p, v->valuestring, k->size);
        return true;
    }
    return false;
}

static void add_key(cJSON *o, const hc_key_t *k, const void *base)
{
    const uint8_t *p = (const uint8_t *) base + k->off;
    char buf[8];
    switch (k->type) {
    case K_FLOAT: cJSON_AddNumberToObject(o, k->key, roundf(*(const float *) p * 1000.0f) / 1000.0f); break;
    case K_U32: cJSON_AddNumberToObject(o, k->key, *(const uint32_t *) p); break;
    case K_U8BOOL: cJSON_AddBoolToObject(o, k->key, *p); break;
    case K_ENUM: cJSON_AddStringToObject(o, k->key, k->opts[*p]); break;
    case K_TIME:
        snprintf(buf, sizeof(buf), "%02u:%02u", *(const uint16_t *) p / 60, *(const uint16_t *) p % 60);
        cJSON_AddStringToObject(o, k->key, buf);
        break;
    case K_STR: cJSON_AddStringToObject(o, k->key, (const char *) p); break;
    }
}

esp_err_t hc_settings_apply_json(int zone, const cJSON *obj, char **err)
{
    if (!cJSON_IsObject(obj) || zone < -1 || zone >= HC_ZONES) return ESP_ERR_INVALID_ARG;
    const hc_key_t *keys = zone < 0 ? GLOBAL_KEYS : ZONE_KEYS;
    const size_t nkeys = zone < 0 ? sizeof(GLOBAL_KEYS) / sizeof(hc_key_t) : sizeof(ZONE_KEYS) / sizeof(hc_key_t);
    void *base = zone < 0 ? (void *) &g_cfg : (void *) &g_cfg.zone[zone];
    char bad[160] = "";
    bool changed = false, side = false;
    hc_lock();
    const cJSON *v;
    cJSON_ArrayForEach(v, obj) {
        // inputs that are not settings
        if (zone >= 0 && !strcmp(v->string, "room_temperature")) {
            if (cJSON_IsNumber(v)) hc_input_external_room(zone, (float) v->valuedouble);
            else if (cJSON_IsNull(v)) hc_input_external_room(zone, NAN);
            continue;
        }
        if (zone < 0 && !strcmp(v->string, "outdoor_temperature")) {
            if (cJSON_IsNumber(v)) hc_input_external_outdoor((float) v->valuedouble);
            else if (cJSON_IsNull(v)) hc_input_external_outdoor(NAN);
            continue;
        }
        const hc_key_t *k = NULL;
        for (size_t i = 0; i < nkeys; i++)
            if (!strcmp(keys[i].key, v->string)) { k = &keys[i]; break; }
        if (!k || !apply_key(k, base, v)) {
            if (strlen(bad) + strlen(v->string) + 2 < sizeof(bad)) {
                if (bad[0]) strcat(bad, ",");
                strcat(bad, v->string);
            }
            continue;
        }
        changed = true;
        if (k->flags & F_SIDE) side = true;
        if (k->flags & F_SCHED_ALL)
            for (int i = 0; i < HC_ZONES; i++) g_rt.sched_period[i] = -2;
        if ((k->flags & F_SCHED_ZONE) && hc_millis() > 30000) g_rt.sched_period[zone] = -2;
    }
    if (changed) hc_settings_mark_dirty();
    if (side) {
        hc_settings_side_effects();
        hc_oled_init(g_cfg.oled_rotate180);
    }
    hc_unlock();
    if (err) *err = bad[0] ? strdup(bad) : NULL;
    return bad[0] ? ESP_ERR_INVALID_ARG : ESP_OK;
}

cJSON *hc_settings_to_json(void)
{
    cJSON *o = cJSON_CreateObject();
    hc_lock();
    for (size_t i = 0; i < sizeof(GLOBAL_KEYS) / sizeof(hc_key_t); i++) add_key(o, &GLOBAL_KEYS[i], &g_cfg);
    cJSON *zones = cJSON_AddArrayToObject(o, "zones");
    for (int z = 0; z < HC_ZONES; z++) {
        cJSON *zo = cJSON_CreateObject();
        for (size_t i = 0; i < sizeof(ZONE_KEYS) / sizeof(hc_key_t); i++) add_key(zo, &ZONE_KEYS[i], &g_cfg.zone[z]);
        cJSON_AddItemToArray(zones, zo);
    }
    hc_unlock();
    return o;
}

static void add_num(cJSON *o, const char *k, float v)
{
    if (isnan(v)) cJSON_AddNullToObject(o, k);
    else cJSON_AddNumberToObject(o, k, roundf(v * 100.0f) / 100.0f);
}

cJSON *hc_state_to_json(void)
{
    cJSON *o = cJSON_CreateObject();
    hc_lock();
    struct tm t;
    char buf[32];
    if (hc_local_time(&t)) {
        strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &t);
        cJSON_AddStringToObject(o, "local_time", buf);
    }
    add_num(o, "outdoor_temperature", g_rt.outdoor_temp);
    add_num(o, "flow_calculated", g_rt.flow_calc);
    add_num(o, "flow_setpoint", g_cfg.ot_t_set);
    add_num(o, "flow_sustained_deficit_boost", g_rt.flow_integral);
    cJSON_AddBoolToObject(o, "evening_preheat", g_rt.preheat_active);
    cJSON_AddBoolToObject(o, "boiler_heating_demand", g_rt.ch_demand);
    cJSON_AddBoolToObject(o, "pump1", g_rt.pump[0]);
    cJSON_AddBoolToObject(o, "pump2", g_rt.pump[1]);
    cJSON_AddBoolToObject(o, "night_window", g_rt.in_night_window);
    cJSON_AddStringToObject(o, "network", g_rt.net_text);
    cJSON *b = cJSON_AddObjectToObject(o, "boiler");
    cJSON_AddBoolToObject(b, "communication", g_rt.ot.comm_ok);
    add_num(b, "flow_temperature", g_rt.ot.t_boiler);
    add_num(b, "return_temperature", g_rt.ot.t_return);
    add_num(b, "hot_water_temperature", g_rt.ot.t_dhw);
    add_num(b, "outdoor_temperature", g_rt.ot.t_outside);
    add_num(b, "modulation", g_rt.ot.rel_mod);
    add_num(b, "water_pressure", g_rt.ot.ch_pressure);
    add_num(b, "fault_code", g_rt.ot.oem_fault_code);
    cJSON_AddBoolToObject(b, "flame", g_rt.ot.flame);
    cJSON_AddBoolToObject(b, "heating", g_rt.ot.ch_active);
    cJSON_AddBoolToObject(b, "hot_water", g_rt.ot.dhw_active);
    cJSON_AddBoolToObject(b, "fault", g_rt.ot.fault);
    hc_unlock();
    return o;
}

cJSON *hc_zone_state_to_json(int i)
{
    static const char *const names[] = {"Off", "Idle", "Heating", "Floor tempering", "Floor overheat lockout",
                                        "Waiting (2 min relay guard)"};
    if (i < 0 || i >= HC_ZONES) return NULL;
    cJSON *o = cJSON_CreateObject();
    hc_lock();
    const hc_zone_cfg_t *z = &g_cfg.zone[i];
    cJSON_AddNumberToObject(o, "zone", i + 1);
    cJSON_AddStringToObject(o, "name", z->name);
    cJSON_AddStringToObject(o, "mode", z->mode_heat ? "heat" : "off");
    cJSON_AddStringToObject(o, "action", !z->mode_heat ? "off" : g_rt.heat_action[i] ? "heating" : "idle");
    add_num(o, "target", z->target);
    add_num(o, "room_temperature", g_rt.room[i]);
    add_num(o, "floor_temperature", g_rt.floor[i]);
    add_num(o, "floor_target", g_rt.floor_tgt[i]);
    cJSON_AddBoolToObject(o, "valve", g_rt.relay[i]);
    cJSON_AddBoolToObject(o, "wired_thermostat", g_rt.wired[i]);
    cJSON_AddBoolToObject(o, "floor_overheat", g_rt.lockout[i]);
    cJSON_AddStringToObject(o, "state", names[g_rt.zone_state[i] % 6]);
    cJSON_AddStringToObject(o, "room_source", hc_input_fresh(&g_rt.mx_room[i]) ? "matter"
                                              : !isnan(g_rt.room[i]) ? "external" : "none");
    hc_unlock();
    return o;
}
