#pragma once
// Matter controller glue: commissioning of temperature / humidity sensors and thermostats (Matter over Thread via
// BLE, over Wi-Fi via BLE, or already on the network), subscriptions, and the runtime mapping of devices to zone
// room temperatures and the outdoor temperature. Devices and mappings are stored in NVS.
#include <stdbool.h>
#include <stdint.h>
#include "cJSON.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t hc_matter_start(void);   // after esp_matter::start(): controller + commissioner, resubscribe
// JSON command, see README.md ("commission", "remove", "forget", "map", "label", "resubscribe"). The result is
// reported through the event callback.
esp_err_t hc_matter_command(const cJSON *cmd, char **err);
cJSON *hc_matter_devices_to_json(void);
bool hc_matter_changed_since(uint32_t *stamp);
typedef void (*hc_matter_event_cb_t)(const char *json);
void hc_matter_set_event_cb(hc_matter_event_cb_t cb);

#ifdef __cplusplus
}
#endif
