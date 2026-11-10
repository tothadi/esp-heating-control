// ESP Heating Control - "Thread/Matter mode" firmware for the ESP32-S3 (see ../README.md).
//
// Start order: the heating control first (it needs no network and must run even if everything else fails), then
// the Thread platform config + RCP update, the Matter stack (which brings up Wi-Fi and the OpenThread task), the
// network selection (Ethernet / Wi-Fi), the Thread border router, the Matter controller, SNTP and MQTT.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include <esp_matter.h>
#include <esp_matter_console.h>
#if CONFIG_ESP_MATTER_CONTROLLER_ENABLE
#include <esp_matter_controller_console.h>
#endif
#include "hc.h"
#include "hc_matter.h"
#include "hc_mqtt.h"
#include "hc_net.h"
#include "hc_thread.h"

static const char *TAG = "app_main";

// USB console (USB-C "MAIN"): "matter esp hc <command>" - bootstrap settings before MQTT is available.
static esp_err_t hc_console(int argc, char **argv)
{
    if (argc >= 1 && !strcmp(argv[0], "wifi") && argc >= 2) {
        return hc_net_set_wifi(argv[1], argc >= 3 ? argv[2] : "");
    }
    if (argc >= 2 && !strcmp(argv[0], "net")) {
        hc_net_mode_t m;
        if (!hc_net_parse_mode(argv[1], &m)) return ESP_ERR_INVALID_ARG;
        return hc_net_set_mode(m);
    }
    if (argc >= 2 && !strcmp(argv[0], "mqtt")) {
        return hc_mqtt_configure(argv[1], argc >= 3 ? argv[2] : "", argc >= 4 ? argv[3] : "",
                                 argc >= 5 ? argv[4] : "heating-controller");
    }
    if (argc >= 3 && !strcmp(argv[0], "send")) {
        // e.g.  matter esp hc send zone/3/set {"target":21.5}
        char *res = hc_dispatch(argv[1], argv[2]);
        printf("%s\n", res ? res : "error");
        free(res);
        return ESP_OK;
    }
    if (argc >= 1 && !strcmp(argv[0], "status")) {
        cJSON *parts[] = {hc_state_to_json(), hc_net_to_json(), hc_thread_to_json(), hc_matter_devices_to_json()};
        for (cJSON *p : parts) {
            char *s = cJSON_PrintUnformatted(p);
            printf("%s\n", s ? s : "");
            cJSON_free(s);
            cJSON_Delete(p);
        }
        hc_mqtt_print_config();
        return ESP_OK;
    }
    if (argc >= 1 && !strcmp(argv[0], "settings")) {
        cJSON *o = hc_settings_to_json();
        char *s = cJSON_Print(o);
        printf("%s\n", s ? s : "");
        cJSON_free(s);
        cJSON_Delete(o);
        return ESP_OK;
    }
    printf("hc wifi <ssid> [password] | hc net auto|ethernet|wifi | hc mqtt <uri> [user] [password] [base]\n"
           "hc send <topic-suffix> <json> | hc status | hc settings\n");
    return argc == 0 ? ESP_OK : ESP_ERR_INVALID_ARG;
}

extern "C" void app_main()
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    // 1. Heating control (independent of everything below)
    ESP_ERROR_CHECK(hc_start());

    // 2. Console
#if CONFIG_ENABLE_CHIP_SHELL
    esp_matter::console::diagnostics_register_commands();
    esp_matter::console::wifi_register_commands();
    esp_matter::console::factoryreset_register_commands();
    esp_matter::console::init();
#if CONFIG_ESP_MATTER_CONTROLLER_ENABLE
    esp_matter::console::controller_register_commands();
#endif
    esp_matter::console::otcli_register_commands();
    static const esp_matter::console::command_t hc_cmd = {"hc", "Heating controller: hc help", hc_console};
    esp_matter::console::add_commands(&hc_cmd, 1);
#endif

    // 3. Thread platform + RCP update, Matter stack (Wi-Fi, OpenThread task)
    hc_thread_pre_start();
    err = esp_matter::start(nullptr);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Matter stack failed to start (%s): heating control keeps running without network",
                 esp_err_to_name(err));
        return;
    }

    // 4. Network selection, border router, Matter controller, time, MQTT
    hc_net_start();
    hc_thread_post_start();
    hc_matter_start();
    hc_time_sntp_start();
    hc_mqtt_start();
}
