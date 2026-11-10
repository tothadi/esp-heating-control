// Front panel: status LED, OLED pages and the button (as in heating-controller.yaml).
#include <stdio.h>
#include "hc_priv.h"
#include "hc_board.h"
#include "hc_drivers.h"
#include "driver/gpio.h"

// Status LED: steady = OK, slow blink = no MQTT broker (if one is configured), fast blink = floor lockout or
// boiler fault.
void hc_status_led_run(void)
{
    static uint8_t tick = 0;
    static int last = -1;
    tick++;
    bool fault = g_rt.ot.fault;
    for (int i = 0; i < HC_ZONES; i++) fault |= g_rt.lockout[i];
    bool on;
    if (fault) on = tick & 1;
    else if (g_rt.remote_configured && g_rt.remote_offline_s >= 60) on = (tick & 7) < 4;
    else on = true;
    if (on != last) { hc_status_led(on); last = on; }
}

// Page rotation every 5 s, skipping zones that are off; the button holds a page for 30 s.
void hc_display_rotate(void)
{
    if ((int32_t) (g_rt.display_hold_until - hc_millis()) > 0) return;
    int p = g_rt.display_page;
    for (int k = 0; k < 9; k++) {
        p = (p + 1) % 9;
        if (p == 0 || g_cfg.zone[p - 1].mode_heat) break;
    }
    g_rt.display_page = p;
}

static void fmt_temp(char *buf, size_t n, const char *fmt, float v)
{
    if (isnan(v)) snprintf(buf, n, "--");
    else snprintf(buf, n, fmt, v);
}

void hc_display_render(void)
{
    static const char *const states[6] = {"off", "idle", "HEAT", "TEMPER", "LOCK", "wait"};
    char l[40], r[24], a[12], b[12];
    hc_oled_clear();
    const int p = g_rt.display_page;
    if (p == 0) {
        struct tm t;
        if (hc_local_time(&t)) {
            strftime(l, sizeof(l), "%H:%M", &t);
            hc_oled_print(0, 0, HC_ALIGN_LEFT, l);
        }
        if (!isnan(g_rt.outdoor_temp)) {
            snprintf(r, sizeof(r), "out %.1f\xc2\xb0", g_rt.outdoor_temp);
            hc_oled_print(127, 0, HC_ALIGN_RIGHT, r);
        }
        fmt_temp(a, sizeof(a), "%.0f", g_rt.ot.t_boiler);
        fmt_temp(b, sizeof(b), "%.0f", g_cfg.ot_t_set);
        snprintf(l, sizeof(l), "flow %s\xc2\xb0 / %s\xc2\xb0", a, b);
        hc_oled_print(0, 16, HC_ALIGN_LEFT, l);
        hc_oled_print(127, 16, HC_ALIGN_RIGHT, g_rt.ot.dhw_active ? "DHW" : g_rt.ot.flame ? "FLAME" : "");
        return;
    }
    const int i = p - 1;
    snprintf(l, sizeof(l), "%d %s", p, g_cfg.zone[i].name);
    hc_oled_print(0, 0, HC_ALIGN_LEFT, l);
    hc_oled_print(127, 0, HC_ALIGN_RIGHT, states[g_rt.zone_state[i] % 6]);
    fmt_temp(a, sizeof(a), "%.1f", g_rt.room[i]);
    fmt_temp(b, sizeof(b), "%.1f", g_rt.floor[i]);
    snprintf(l, sizeof(l), "%s/%.1f\xc2\xb0  floor %s\xc2\xb0", a, g_cfg.zone[i].target, b);
    hc_oled_print(0, 16, HC_ALIGN_LEFT, l);
}

// Button on IO0 (BOOT, active low): 30 ms debounce; a press shows the next page and holds it for 30 s.
void hc_button_poll(uint32_t dt_ms)
{
    static uint32_t low_ms = 0;
    static bool pressed = false;
    const bool down = gpio_get_level(HC_PIN_BUTTON) == 0;
    if (!down) { low_ms = 0; pressed = false; return; }
    low_ms += dt_ms;
    if (!pressed && low_ms >= 30) {
        pressed = true;
        g_rt.display_hold_until = hc_millis() + 30000;
        g_rt.display_page = (g_rt.display_page + 1) % 9;
    }
}
