#pragma once
// MQTT interface: state, settings and commands (see README.md for the topics). Home Assistant is not required;
// any broker works, and the controller runs normally without one.
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif
esp_err_t hc_mqtt_start(void);
esp_err_t hc_mqtt_configure(const char *uri, const char *user, const char *password, const char *base_topic);
void hc_mqtt_print_config(void);
// Shared command dispatcher (MQTT topics and the USB console): topic suffix + JSON payload. Returns a malloc'd
// result string (or NULL).
char *hc_dispatch(const char *suffix, const char *payload);
#ifdef __cplusplus
}
#endif
