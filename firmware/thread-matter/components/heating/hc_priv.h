#pragma once
// Internal state shared by the heating component's files. Everything here is protected by hc_lock().
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include "hc.h"
#include "opentherm.h"

#define TAG_HC "heating"

typedef enum { SRC_ROOM = 0, SRC_WIRED = 1, SRC_FLOOR = 2 } hc_source_t;
typedef enum { FLOOR_TILES = 0, FLOOR_LAMINATE = 1, FLOOR_NONE = 2 } hc_floor_t;
typedef enum { PUMP_AUTO = 0, PUMP_ON = 1, PUMP_OFF = 2 } hc_pump_mode_t;
enum { ZS_OFF, ZS_IDLE, ZS_HEATING, ZS_TEMPER, ZS_LOCKOUT, ZS_WAIT };

typedef struct {
    char name[24];
    uint8_t mode_heat;        // thermostat mode: 1 HEAT, 0 OFF
    float target;             // thermostat target temperature (restored after reboot)
    float comfort, night, boost;
    uint8_t night_on, boost_on;
    uint8_t source;           // hc_source_t
    uint8_t floor_type;       // hc_floor_t
    float floor_limit;
    float floor_cal;          // NTC calibration offset (the ESPHome "offset" filter)
} hc_zone_cfg_t;

typedef struct {
    uint32_t magic;
    hc_zone_cfg_t zone[HC_ZONES];
    uint8_t heating_mode;     // 0 Auto, 1 Off
    uint8_t flow_mode;        // 0 Auto, 1 Manual
    uint8_t pump_mode[2];     // hc_pump_mode_t
    uint8_t dhw_enable;
    uint8_t oled_rotate180;
    float curve_ref, curve_slope, curve_offset, curve_min, curve_max, flow_max, boost_max, flow_manual;
    float floor_offset_tiles, floor_offset_laminate, night_floor_target, pump_overrun;
    uint16_t sched_day_start, sched_night_start, sched_boost_start, sched_boost_end;   // minutes after midnight
    float ot_t_set;           // last flow setpoint written to the boiler (restore_value in ESPHome)
    float ntc_supply[2], ntc_ref, ntc_nominal, ntc_b, ntc_open_v, ntc_short_v;
    float fallback_outdoor;
    uint32_t room_timeout_s;  // remote (MQTT) link down this long -> external room/outdoor values are dropped
    uint32_t sensor_timeout_s; // a Matter or MQTT value older than this is dropped
    char timezone[48];        // POSIX TZ
} hc_settings_t;

typedef struct { float v; uint32_t ms; } hc_input_t;

typedef struct {
    // per zone
    float floor[HC_ZONES];            // filtered floor temperature (NaN unknown)
    float room[HC_ZONES];             // effective room temperature (template sensor, every 30 s)
    bool relay[HC_ZONES];
    bool wired[HC_ZONES];             // debounced thermostat input
    bool heat_action[HC_ZONES];       // thermostat action == HEATING
    uint32_t heat_on_ms[HC_ZONES], heat_off_ms[HC_ZONES];
    bool lockout[HC_ZONES];
    float floor_tgt[HC_ZONES];
    uint8_t zone_state[HC_ZONES];
    int8_t sched_period[HC_ZONES];    // 0 comfort, 1 night, 2 boost, -1 boot, -2 re-apply
    uint32_t zone_on_since[HC_ZONES], zone_changed[HC_ZONES];
    hc_input_t mx_room[HC_ZONES], ext_room[HC_ZONES], mx_outdoor, ext_outdoor;
    // global
    bool in_night_window;
    float outdoor_temp, flow_calc, flow_integral;
    uint32_t flow_integral_step;
    float outdoor_hist[13];
    int outdoor_hist_tick;
    bool preheat_active;
    uint32_t preheat_calm_since;
    bool ch_demand;                   // OpenTherm CH enable (owned by the zone loop)
    bool pump[2];
    uint32_t pump_idle_since[2];
    uint32_t pump_exercise_until;
    int display_page;
    uint32_t display_hold_until;
    bool remote_configured, remote_connected;
    int remote_offline_s;
    char net_text[32];
    ot_state_t ot;                    // last boiler snapshot
} hc_rt_t;

extern hc_settings_t g_cfg;
extern hc_rt_t g_rt;

void hc_lock(void);
void hc_unlock(void);
uint32_t hc_millis(void);
bool hc_local_time(struct tm *out);   // false until the clock is set (RTC or SNTP)
void hc_settings_defaults(hc_settings_t *s);
void hc_settings_load(void);
void hc_settings_mark_dirty(void);
void hc_settings_save_if_due(void);

// outputs (hardware)
void hc_relay_set(int zone, bool on);
void hc_pump_set(int pump, bool on);
void hc_status_led(bool on);

// periodic jobs (called with the lock held)
void hc_ntc_sample(void);            // every 1 s
void hc_inputs_poll(uint32_t dt_ms); // every 100 ms: TERM inputs (2 s debounce)
void hc_room_update(void);           // every 30 s
void hc_outdoor_update(void);        // every 60 s
void hc_zones_run(void);             // every 10 s
void hc_flow_run(void);              // every 60 s
void hc_schedule_run(void);          // every 30 s
void hc_pump_exercise_check(void);   // every 1 s
void hc_status_led_run(void);        // every 250 ms
void hc_display_rotate(void);        // every 5 s
void hc_display_render(void);        // every 1 s
void hc_button_poll(uint32_t dt_ms); // every 10 ms
bool hc_input_fresh(const hc_input_t *in);
