// Weather-compensated flow temperature -> boiler (the 60 s interval of heating-controller.yaml).
#include "hc_priv.h"
#include "esp_log.h"

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

void hc_flow_run(void)
{
    const uint32_t now = hc_millis();
    const bool heating_on = g_cfg.heating_mode == 0;

    // Outdoor: Matter / MQTT -> boiler sensor -> fallback.
    const float out_meas = g_rt.outdoor_temp;
    const float out = isnan(out_meas) ? g_cfg.fallback_outdoor : out_meas;

    // Heating curve: flow = ref + offset + slope x (ref - outdoor), clamped to curve min/max.
    const float ref = g_cfg.curve_ref;
    float base = ref + g_cfg.curve_offset + g_cfg.curve_slope * (ref - out);
    base = fmaxf(g_cfg.curve_min, fminf(g_cfg.curve_max, base));

    // Demand boost: largest deficit (setpoint - room) of the "Room sensor" zones that are on and not locked out:
    // (deficit - 0.3) x 5, capped. Without room temperatures there is no boost.
    float dev = -99.0f;
    bool any_valid = false;
    for (int i = 0; i < HC_ZONES; i++) {
        const hc_zone_cfg_t *z = &g_cfg.zone[i];
        if (!heating_on || !z->mode_heat) continue;
        if (z->source != SRC_ROOM || g_rt.lockout[i]) continue;
        const float a = g_rt.room[i], t = z->target;
        if (isnan(a) || isnan(t)) continue;
        any_valid = true;
        dev = fmaxf(dev, t - a);
    }
    const float boost = any_valid ? fmaxf(0.0f, fminf(g_cfg.boost_max, (dev - 0.3f) * 5.0f)) : 0.0f;

    // Sustained-deficit term: +1 C every 30 min (max +8) while a room stays > 0.3 C below target, reset once every
    // room has reached it.
    if (!any_valid || dev <= 0.0f) {
        g_rt.flow_integral = 0;
        g_rt.flow_integral_step = now;
    } else if (now - g_rt.flow_integral_step >= 30 * 60000) {
        g_rt.flow_integral_step = now;
        if (dev > 0.3f && g_rt.flow_integral < 8.0f) g_rt.flow_integral += 1.0f;
    }

    // Evening preheat: outdoor temperature fell >= 3 C in 2 h between 16:00 and 23:00 -> +2 C until it has been
    // calm for 30 min. Measured values only.
    if (++g_rt.outdoor_hist_tick >= 10) {
        g_rt.outdoor_hist_tick = 0;
        float *h = g_rt.outdoor_hist;
        for (int k = 0; k < 12; k++) h[k] = h[k + 1];
        h[12] = out_meas;
    }
    const float *h = g_rt.outdoor_hist;
    const bool falling = !isnan(h[0]) && !isnan(h[12]) && h[12] - h[0] <= -3.0f;
    struct tm lt;
    const bool evening = hc_local_time(&lt) && lt.tm_hour >= 16 && lt.tm_hour < 23;
    if (falling) {
        g_rt.preheat_calm_since = 0;
        if (evening && !g_rt.preheat_active) {
            g_rt.preheat_active = true;
            ESP_LOGI("flow", "Fast outdoor temperature drop: evening preheat on");
        }
    } else if (g_rt.preheat_active) {
        if (g_rt.preheat_calm_since == 0) g_rt.preheat_calm_since = now ? now : 1;
        if (now - g_rt.preheat_calm_since >= 30 * 60000) g_rt.preheat_active = false;
    }

    const float calc = fminf(g_cfg.flow_max, base + boost + g_rt.flow_integral + (g_rt.preheat_active ? 2.0f : 0.0f));
    g_rt.flow_calc = calc;

    // Write to the boiler: Auto = calculated value in 0.5 C steps, only on a change of >= 1 C; Manual = the manual
    // setpoint. The flow setpoint number is limited to 20..60 C like the ESPHome entity.
    const bool manual = g_cfg.flow_mode == 1;
    const float want = clampf(manual ? g_cfg.flow_manual : roundf(calc * 2.0f) / 2.0f, 20.0f, 60.0f);
    const float cur = g_cfg.ot_t_set;
    if (isnan(cur) || fabsf(want - cur) >= (manual ? 0.5f : 1.0f)) {
        g_cfg.ot_t_set = want;
        ot_master_set_t_set(want);
        hc_settings_mark_dirty();
    }
}
