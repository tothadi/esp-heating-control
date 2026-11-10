#pragma once
// ESP Heating Control - heating control core (port of firmware/controller, the ESPHome configuration).
//
// Runs on its own: zones, floor protection, boiler (OpenTherm), flow temperature, schedules, pumps, LEDs, display
// and button need no network. Room and outdoor temperatures come from mapped Matter sensors (hc_input_matter_*),
// from MQTT (hc_input_external_*) or, for the outdoor temperature, from the boiler; all of them are optional.
#include <stdbool.h>
#include <stdint.h>
#include "cJSON.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HC_ZONES 8

esp_err_t hc_start(void);   // load settings, init hardware, start the control task
void hc_time_sntp_start(void);   // once the network stack is up: SNTP, which also sets the DS3231

// Inputs (thread safe). NaN clears a value.
void hc_input_matter_room(int zone, float celsius);       // zone 0..7; drops out after the sensor timeout
void hc_input_matter_outdoor(float celsius);
void hc_input_external_room(int zone, float celsius);     // e.g. from MQTT; like the HA sensors of the ESPHome build
void hc_input_external_outdoor(float celsius);
void hc_set_remote_status(bool configured, bool connected); // MQTT link, for the room timeout and the status LED
void hc_set_network_status(const char *text);              // shown on the OLED system page, e.g. "eth 192.168.1.5"

// Settings: apply a JSON object of changes; zone = -1 for the global settings, 0..7 for a zone.
// Unknown keys are reported in *err (malloc'd string, caller frees) but do not stop the others.
esp_err_t hc_settings_apply_json(int zone, const cJSON *obj, char **err);
cJSON *hc_settings_to_json(void);         // all settings (global + zones)
cJSON *hc_state_to_json(void);            // runtime state of the controller (boiler, flow, pumps, ...)
cJSON *hc_zone_state_to_json(int zone);   // runtime state of one zone
const char *hc_zone_name(int zone);
bool hc_settings_changed_since(uint32_t *stamp);   // true if settings changed since *stamp (and updates it)
const char *hc_timezone(void);

#ifdef __cplusplus
}
#endif
