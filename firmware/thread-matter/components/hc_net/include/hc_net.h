#pragma once
// ESP Heating Control - network selection: W5500 Ethernet and Wi-Fi station, both brought up; the default route,
// the Thread border router backbone and Matter use Ethernet while its link is up (mode "auto"), Wi-Fi otherwise.
// Modes: auto (default), ethernet (Wi-Fi station off), wifi (Ethernet off). Stored in NVS.
#include <stdbool.h>
#include "cJSON.h"
#include "esp_err.h"
#include "esp_netif.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { HC_NET_AUTO = 0, HC_NET_ETHERNET = 1, HC_NET_WIFI = 2 } hc_net_mode_t;
typedef void (*hc_net_cb_t)(esp_netif_t *active);

esp_err_t hc_net_start(void);   // after esp_matter::start() (which initialises the Wi-Fi stack)
esp_err_t hc_net_set_mode(hc_net_mode_t mode);
hc_net_mode_t hc_net_get_mode(void);
const char *hc_net_mode_name(hc_net_mode_t mode);
bool hc_net_parse_mode(const char *s, hc_net_mode_t *mode);
esp_err_t hc_net_set_wifi(const char *ssid, const char *password);
bool hc_net_get_wifi(char *ssid, size_t ssid_len, char *password, size_t pw_len);
esp_netif_t *hc_net_active(void);           // netif carrying the default route (NULL = none)
void hc_net_register_cb(hc_net_cb_t cb);    // called (event task context) when the active netif changes
cJSON *hc_net_to_json(void);

#ifdef __cplusplus
}
#endif
