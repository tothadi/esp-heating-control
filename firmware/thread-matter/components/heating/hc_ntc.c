// Floor NTC inputs: ADS1115 voltage -> resistance -> temperature, as in zone.yaml of the ESPHome build:
// sampled every second; the median of 60 raw voltages is passed on once a minute (first after 60 samples);
// open (> ntc_open_v) or shorted (< ntc_short_v) probes become NaN; divider 3V3 - Rref - input - NTC - GND
// (ESPHome resistance sensor, DOWNSTREAM); B-parameter NTC; calibration offset; plausibility 0..60 C; moving
// average over the last 5 values (NaN values are skipped, like ESPHome's sliding_window_moving_average).
#include <stdlib.h>
#include <string.h>
#include "hc_priv.h"
#include "hc_board.h"
#include "hc_drivers.h"

#define WIN 60
static float s_win[HC_ZONES][WIN];
static int s_count[HC_ZONES];
static float s_avg[HC_ZONES][5];
static int s_avg_n[HC_ZONES];

static int cmp_float(const void *a, const void *b)
{
    float x = *(const float *) a, y = *(const float *) b;
    return (x > y) - (x < y);
}

static float to_celsius(int i, float v)
{
    if (isnan(v) || v > g_cfg.ntc_open_v || v < g_cfg.ntc_short_v) return NAN;
    const float vref = g_cfg.ntc_supply[i < 4 ? 0 : 1];
    if (v >= vref) return NAN;
    const float r = g_cfg.ntc_ref * v / (vref - v);
    if (r <= 0.0f) return NAN;
    const float b = 1.0f / g_cfg.ntc_b;
    const float a = 1.0f / 298.15f - b * logf(g_cfg.ntc_nominal);
    float t = 1.0f / (a + b * logf(r)) - 273.15f;
    t += g_cfg.zone[i].floor_cal;
    if (t < 0.0f || t > 60.0f) return NAN;
    return t;
}

void hc_ntc_sample(void)
{
    float v[HC_ZONES];
    for (int i = 0; i < HC_ZONES; i++) {
        // zones 1-4 on 0x48, 5-8 on 0x49; AIN3..AIN0 = NTC1..NTC4 (NTC5..NTC8)
        const uint8_t addr = i < 4 ? HC_ADDR_ADS_A : HC_ADDR_ADS_B;
        if (hc_ads_read(addr, 3 - (i % 4), &v[i]) != ESP_OK) v[i] = NAN;   // failed read: no sample
    }
    hc_lock();
    for (int i = 0; i < HC_ZONES; i++) {
        if (isnan(v[i])) continue;
        s_win[i][s_count[i] % WIN] = v[i];
        if (++s_count[i] < WIN) continue;
        s_count[i] = 0;
        float sorted[WIN];
        memcpy(sorted, s_win[i], sizeof(sorted));
        qsort(sorted, WIN, sizeof(float), cmp_float);
        const float med = (sorted[WIN / 2] + sorted[WIN / 2 - 1]) / 2.0f;
        const float t = to_celsius(i, med);
        // sliding window moving average (5), NaN skipped
        s_avg[i][s_avg_n[i] % 5] = t;
        s_avg_n[i]++;
        const int n = s_avg_n[i] < 5 ? s_avg_n[i] : 5;
        float sum = 0;
        int valid = 0;
        for (int k = 0; k < n; k++)
            if (!isnan(s_avg[i][k])) { sum += s_avg[i][k]; valid++; }
        g_rt.floor[i] = valid ? sum / valid : NAN;
        if (s_avg_n[i] >= 1000) s_avg_n[i] = 5 + s_avg_n[i] % 5;
    }
    hc_unlock();
}
