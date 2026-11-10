// Schedule (the 30 s interval of heating-controller.yaml): night window night_start -> day_start for zones with
// night setback, evening boost window boost_start -> boost_end for zones with evening boost, comfort otherwise.
// The setpoint is written only when a zone's period changes; after a reboot the restored setpoint is kept until the
// next change. A manual setpoint change holds until then. Also the weekly pump exercise (Monday 10:00, 1 minute).
#include "hc_priv.h"
#include "esp_log.h"

static bool inside(int m, int a, int b) { return a <= b ? (m >= a && m < b) : (m >= a || m < b); }

void hc_schedule_run(void)
{
    struct tm t;
    if (!hc_local_time(&t)) return;
    const int m = t.tm_hour * 60 + t.tm_min;
    const bool night = inside(m, g_cfg.sched_night_start, g_cfg.sched_day_start);
    const bool boost = inside(m, g_cfg.sched_boost_start, g_cfg.sched_boost_end);
    g_rt.in_night_window = night;

    for (int i = 0; i < HC_ZONES; i++) {
        hc_zone_cfg_t *z = &g_cfg.zone[i];
        const int8_t p = (night && z->night_on) ? 1 : (boost && z->boost_on) ? 2 : 0;
        if (g_rt.sched_period[i] == -1) { g_rt.sched_period[i] = p; continue; }
        if (p == g_rt.sched_period[i]) continue;
        g_rt.sched_period[i] = p;
        const float sp = p == 1 ? z->night : p == 2 ? z->boost : z->comfort;
        if (isnan(sp)) continue;
        if (z->target != sp) {
            z->target = sp;
            hc_settings_mark_dirty();
        }
        ESP_LOGI("schedule", "Zone %d -> %.1f C", i + 1, sp);
    }
}

void hc_pump_exercise_check(void)
{
    static int last_yday = -1;
    struct tm t;
    if (!hc_local_time(&t)) return;
    if (t.tm_wday == 1 && t.tm_hour == 10 && t.tm_min == 0 && t.tm_yday != last_yday) {
        last_yday = t.tm_yday;
        g_rt.pump_exercise_until = hc_millis() + 60000;
        ESP_LOGI("pump", "Weekly pump exercise");
    }
}
