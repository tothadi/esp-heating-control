// Zones, floor protection, boiler CH demand and pumps: the 10 s control loop of heating-controller.yaml, plus the
// room thermostat of zone.yaml (ESPHome "thermostat" climate: heat_deadband 0.2, heat_overrun 0.0, minimum run /
// off time 120 s, minimum idle 30 s).
#include "hc_priv.h"
#include "esp_log.h"

static void thermostat_update(int i, uint32_t now)
{
    const hc_zone_cfg_t *z = &g_cfg.zone[i];
    if (!z->mode_heat) {
        if (g_rt.heat_action[i]) { g_rt.heat_action[i] = false; g_rt.heat_off_ms[i] = now; }
        return;
    }
    const float cur = g_rt.room[i], t = z->target;
    bool want;
    if (isnan(cur) || isnan(t)) want = false;
    else if (cur < t - 0.2f) want = true;
    else if (cur > t + 0.0f) want = false;
    else want = g_rt.heat_action[i];
    if (want && !g_rt.heat_action[i]) {
        if (g_rt.heat_off_ms[i] == 0 || now - g_rt.heat_off_ms[i] >= 120000) {
            g_rt.heat_action[i] = true;
            g_rt.heat_on_ms[i] = now;
        }
    } else if (!want && g_rt.heat_action[i]) {
        if (isnan(cur) || now - g_rt.heat_on_ms[i] >= 120000) {
            g_rt.heat_action[i] = false;
            g_rt.heat_off_ms[i] = now ? now : 1;
        }
    }
}

void hc_zones_run(void)
{
    const bool heating_on = g_cfg.heating_mode == 0;
    const float tile_off = g_cfg.floor_offset_tiles;
    const float lam_off = g_cfg.floor_offset_laminate;
    const float night_floor = g_cfg.night_floor_target;
    const uint32_t now = hc_millis();

    for (int i = 0; i < HC_ZONES; i++) {
        const hc_zone_cfg_t *z = &g_cfg.zone[i];
        thermostat_update(i, now);
        const float f = g_rt.floor[i];       // floor
        const float a = g_rt.room[i];        // room air (NaN without a sensor)
        const float t = z->target;
        const int ft = z->floor_type;        // 0 tiles, 1 laminate, 2 no probe
        const int s = z->source;             // 0 room sensor, 1 wired, 2 floor only
        const bool probe = ft != FLOOR_NONE;

        // Floor overheat lockout: on at the limit, off 2 C below. A probe that becomes unknown keeps the state.
        if (!probe) {
            g_rt.lockout[i] = false;
        } else if (!isnan(f)) {
            const float lim = z->floor_limit;
            if (!g_rt.lockout[i] && f >= lim) {
                g_rt.lockout[i] = true;
                ESP_LOGW("zone", "Zone %d floor %.1f C >= limit %.1f C: locked out", i + 1, f, lim);
            } else if (g_rt.lockout[i] && f <= lim - 2.0f) {
                g_rt.lockout[i] = false;
                ESP_LOGW("zone", "Zone %d floor %.1f C: lockout released", i + 1, f);
            }
        }

        // Floor target: setpoint + offset; the night floor target in the night window for zones with night
        // setback. Laminate: higher offset until the room is 0.3 C above setpoint, or when the room is unknown.
        float tgt = NAN;
        if (probe && !isnan(t)) {
            if (g_rt.in_night_window && z->night_on) tgt = night_floor;
            else if (ft == FLOOR_TILES) tgt = t + tile_off;
            else tgt = (isnan(a) || a < t + 0.3f) ? t + lam_off : t + tile_off;
        }
        g_rt.floor_tgt[i] = tgt;

        const bool zone_on = heating_on && z->mode_heat;
        const bool relay = g_rt.relay[i];
        // floor demand, 0.3 C hysteresis
        const bool fd = zone_on && !isnan(tgt) && !isnan(f) && f < tgt - (relay ? 0.0f : 0.3f);
        bool rd = false;
        if (zone_on && s == SRC_ROOM) rd = g_rt.heat_action[i];
        if (zone_on && s == SRC_WIRED) rd = g_rt.wired[i];

        const bool want = (rd || fd) && !g_rt.lockout[i];
        uint8_t st = !zone_on ? ZS_OFF : g_rt.lockout[i] ? ZS_LOCKOUT : rd ? ZS_HEATING : fd ? ZS_TEMPER : ZS_IDLE;
        if (want != relay) {
            // A relay switches at most every 2 minutes; closing for a lockout or "off" is immediate.
            const bool urgent = !want && (g_rt.lockout[i] || !zone_on);
            if (urgent || g_rt.zone_changed[i] == 0 || now - g_rt.zone_changed[i] >= 120000) {
                hc_relay_set(i, want);
                g_rt.zone_changed[i] = now ? now : 1;
            } else {
                st = ZS_WAIT;
            }
        }
        g_rt.zone_state[i] = st;
    }

    // Boiler CH demand: on once a zone has been open for 1 minute (thermal actuators are open by then), off as soon
    // as no zone is open, so the boiler pump never runs against closed valves.
    bool any_open = false, any_open_60 = false;
    for (int i = 0; i < HC_ZONES; i++) {
        if (!g_rt.relay[i]) continue;
        any_open = true;
        if (now - g_rt.zone_on_since[i] >= 60000) any_open_60 = true;
    }
    if (!any_open && g_rt.ch_demand) { g_rt.ch_demand = false; ot_master_set_ch_enable(false); }
    else if (any_open_60 && heating_on && !g_rt.ch_demand) { g_rt.ch_demand = true; ot_master_set_ch_enable(true); }

    // Pumps: Auto = run while any zone is open + overrun; weekly exercise; On / Off = forced.
    const bool exercise = (int32_t) (g_rt.pump_exercise_until - now) > 0;
    const uint32_t overrun = (uint32_t) g_cfg.pump_overrun * 60000;
    for (int p = 0; p < 2; p++) {
        const int m = g_cfg.pump_mode[p];
        bool run;
        if (m == PUMP_ON) run = true;
        else if (m == PUMP_OFF) run = false;
        else if (any_open) { run = true; g_rt.pump_idle_since[p] = 0; }
        else {
            if (g_rt.pump_idle_since[p] == 0) g_rt.pump_idle_since[p] = now ? now : 1;
            run = g_rt.pump[p] && now - g_rt.pump_idle_since[p] < overrun;
        }
        if (m != PUMP_OFF && exercise) run = true;
        if (run != g_rt.pump[p]) hc_pump_set(p, run);
    }
}
