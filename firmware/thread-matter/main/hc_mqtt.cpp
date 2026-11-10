// MQTT interface. Why MQTT and not a web UI: it works with any broker and any front end (Home Assistant through
// MQTT, Node-RED, a phone app, mosquitto_pub), keeps the firmware small next to the border router and the Matter
// controller, and needs no HTTP server or web assets. Topics (<base> defaults to "heating-controller"):
//
//   <base>/status                 online/offline (retained, last will)
//   <base>/state                  controller state, every 10 s
//   <base>/zone/<1..8>/state      zone state, every 10 s
//   <base>/settings               all settings (retained, on change)
//   <base>/net/state              network state (retained, on change / 60 s)
//   <base>/matter/devices         commissioned devices, readings, mappings (retained, on change)
//   <base>/matter/event           commissioning / subscription events
//   <base>/result                 answer to every command (ok / error text)
//   <base>/set                    JSON: global settings, "outdoor_temperature"
//   <base>/zone/<1..8>/set        JSON: zone settings, "room_temperature"
//   <base>/net/set                JSON: {"mode": "auto|ethernet|wifi", "wifi_ssid": .., "wifi_password": ..}
//   <base>/matter/cmd             JSON: {"cmd": "commission" | "remove" | "forget" | "map" | "label" | ...}
#include <initializer_list>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hc_mqtt.h"
#include "hc.h"
#include "hc_matter.h"
#include "hc_net.h"
#include "hc_thread.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt_client.h"
#include "nvs.h"

static const char *TAG = "hc_mqtt";
static char s_uri[128], s_user[64], s_pass[64], s_base[48] = "heating-controller";
static esp_mqtt_client_handle_t s_client;
static volatile bool s_connected;
static volatile bool s_republish;
static char *s_rx_topic, *s_rx_buf;
static int s_rx_len;

static void load(void)
{
    nvs_handle_t h;
    if (nvs_open("hcmqtt", NVS_READONLY, &h) != ESP_OK) return;
    size_t l;
    l = sizeof(s_uri); if (nvs_get_str(h, "uri", s_uri, &l) != ESP_OK) s_uri[0] = 0;
    l = sizeof(s_user); if (nvs_get_str(h, "user", s_user, &l) != ESP_OK) s_user[0] = 0;
    l = sizeof(s_pass); if (nvs_get_str(h, "pass", s_pass, &l) != ESP_OK) s_pass[0] = 0;
    l = sizeof(s_base); if (nvs_get_str(h, "base", s_base, &l) != ESP_OK) strcpy(s_base, "heating-controller");
    nvs_close(h);
}

static void publish(const char *suffix, const char *payload, bool retain)
{
    if (!s_client || !s_connected) return;
    char topic[96];
    snprintf(topic, sizeof(topic), "%s/%s", s_base, suffix);
    esp_mqtt_client_enqueue(s_client, topic, payload, 0, 0, retain, true);
}

static void publish_json(const char *suffix, cJSON *o, bool retain)
{
    if (!o) return;
    char *s = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    if (s) { publish(suffix, s, retain); cJSON_free(s); }
}

static void matter_event(const char *json) { publish("matter/event", json, false); }

char *hc_dispatch(const char *suffix, const char *payload)
{
    cJSON *in = cJSON_Parse(payload);
    if (!in) return strdup("error: invalid JSON");
    char *err = nullptr;
    esp_err_t e = ESP_ERR_NOT_FOUND;
    int zone;
    if (!strcmp(suffix, "set")) {
        e = hc_settings_apply_json(-1, in, &err);
    } else if (sscanf(suffix, "zone/%d/set", &zone) == 1) {
        e = hc_settings_apply_json(zone - 1, in, &err);
    } else if (!strcmp(suffix, "net/set")) {
        const cJSON *ssid = cJSON_GetObjectItem(in, "wifi_ssid"), *pw = cJSON_GetObjectItem(in, "wifi_password");
        const cJSON *mode = cJSON_GetObjectItem(in, "mode");
        e = ESP_OK;
        if (cJSON_IsString(ssid))
            e = hc_net_set_wifi(ssid->valuestring, cJSON_IsString(pw) ? pw->valuestring : "");
        hc_net_mode_t m;
        if (e == ESP_OK && mode) {
            if (cJSON_IsString(mode) && hc_net_parse_mode(mode->valuestring, &m)) e = hc_net_set_mode(m);
            else { e = ESP_ERR_INVALID_ARG; err = strdup("mode must be auto, ethernet or wifi"); }
        }
        s_republish = true;
    } else if (!strcmp(suffix, "matter/cmd")) {
        e = hc_matter_command(in, &err);
    } else if (!strcmp(suffix, "mqtt/set")) {
        const cJSON *u = cJSON_GetObjectItem(in, "uri"), *us = cJSON_GetObjectItem(in, "user"),
                    *p = cJSON_GetObjectItem(in, "password"), *b = cJSON_GetObjectItem(in, "base_topic");
        e = hc_mqtt_configure(cJSON_IsString(u) ? u->valuestring : s_uri, cJSON_IsString(us) ? us->valuestring : s_user,
                              cJSON_IsString(p) ? p->valuestring : s_pass, cJSON_IsString(b) ? b->valuestring : s_base);
    }
    cJSON_Delete(in);
    char *res = (char *) malloc(200);
    if (res) {
        if (e == ESP_OK) snprintf(res, 200, "ok %s", suffix);
        else snprintf(res, 200, "error %s: %s", suffix, err ? err : esp_err_to_name(e));
    }
    free(err);
    return res;
}

static void handle_message(const char *topic, const char *data)
{
    const size_t bl = strlen(s_base);
    if (strncmp(topic, s_base, bl) || topic[bl] != '/') return;
    if (!strcmp(topic + bl + 1, "mqtt/set")) return;   // broker settings only from the USB console
    char *res = hc_dispatch(topic + bl + 1, data);
    if (res) { publish("result", res, false); ESP_LOGI(TAG, "%s", res); free(res); }
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    auto *ev = (esp_mqtt_event_handle_t) data;
    char t[96];
    switch ((esp_mqtt_event_id_t) id) {
    case MQTT_EVENT_CONNECTED:
        s_connected = true;
        hc_set_remote_status(true, true);
        publish("status", "online", true);
        for (const char *sfx : {"set", "zone/+/set", "net/set", "matter/cmd"}) {
            snprintf(t, sizeof(t), "%s/%s", s_base, sfx);
            esp_mqtt_client_subscribe(s_client, t, 1);
        }
        s_republish = true;
        ESP_LOGI(TAG, "Connected to %s", s_uri);
        break;
    case MQTT_EVENT_DISCONNECTED:
        s_connected = false;
        hc_set_remote_status(true, false);
        break;
    case MQTT_EVENT_DATA:
        // Messages can arrive in fragments: collect them.
        if (ev->current_data_offset == 0) {
            free(s_rx_topic);
            free(s_rx_buf);
            s_rx_topic = strndup(ev->topic, ev->topic_len);
            s_rx_len = ev->total_data_len;
            s_rx_buf = (s_rx_len < 4096) ? (char *) calloc(1, s_rx_len + 1) : nullptr;
        }
        if (s_rx_buf && ev->current_data_offset + ev->data_len <= s_rx_len) {
            memcpy(s_rx_buf + ev->current_data_offset, ev->data, ev->data_len);
            if (ev->current_data_offset + ev->data_len == s_rx_len && s_rx_topic) handle_message(s_rx_topic, s_rx_buf);
        }
        break;
    default:
        break;
    }
}

static void publish_task(void *arg)
{
    uint32_t settings_stamp = 0, matter_stamp = 0;
    int tick = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        if (!s_connected) continue;
        const bool all = s_republish;
        s_republish = false;
        if (hc_settings_changed_since(&settings_stamp) || all) publish_json("settings", hc_settings_to_json(), true);
        if (hc_matter_changed_since(&matter_stamp) || all) publish_json("matter/devices", hc_matter_devices_to_json(), true);
        if (all || tick % 60 == 0) publish_json("net/state", hc_net_to_json(), true);
        if (all || tick % 10 == 0) {
            cJSON *st = hc_state_to_json();
            cJSON_AddItemToObject(st, "thread", hc_thread_to_json());
            publish_json("state", st, false);
            for (int z = 0; z < HC_ZONES; z++) {
                char sfx[24];
                snprintf(sfx, sizeof(sfx), "zone/%d/state", z + 1);
                publish_json(sfx, hc_zone_state_to_json(z), false);
            }
        }
        tick++;
    }
}

esp_err_t hc_mqtt_start(void)
{
    load();
    hc_matter_set_event_cb(matter_event);
    static bool task_started;
    if (!task_started) {
        task_started = xTaskCreate(publish_task, "hc_mqtt_pub", 4096, nullptr, 4, nullptr) == pdPASS;
    }
    if (s_client) {
        esp_mqtt_client_stop(s_client);
        esp_mqtt_client_destroy(s_client);
        s_client = nullptr;
        s_connected = false;
    }
    if (!s_uri[0]) {
        ESP_LOGW(TAG, "No MQTT broker configured (console: matter esp hc mqtt <uri> [user] [password])");
        hc_set_remote_status(false, false);
        return ESP_OK;
    }
    static char lwt_topic[64];
    snprintf(lwt_topic, sizeof(lwt_topic), "%s/status", s_base);
    esp_mqtt_client_config_t cfg = {};
    cfg.broker.address.uri = s_uri;
    cfg.credentials.username = s_user[0] ? s_user : nullptr;
    cfg.credentials.authentication.password = s_pass[0] ? s_pass : nullptr;
    cfg.session.last_will.topic = lwt_topic;
    cfg.session.last_will.msg = "offline";
    cfg.session.last_will.retain = 1;
    cfg.session.last_will.qos = 1;
    cfg.buffer.size = 2048;
    cfg.task.stack_size = 6144;
    s_client = esp_mqtt_client_init(&cfg);
    if (!s_client) return ESP_FAIL;
    esp_mqtt_client_register_event(s_client, MQTT_EVENT_ANY, on_event, nullptr);
    hc_set_remote_status(true, false);
    return esp_mqtt_client_start(s_client);
}

esp_err_t hc_mqtt_configure(const char *uri, const char *user, const char *password, const char *base)
{
    if (strlen(uri) >= sizeof(s_uri) || strlen(user) >= sizeof(s_user) || strlen(password) >= sizeof(s_pass) ||
        !base[0] || strlen(base) >= sizeof(s_base))
        return ESP_ERR_INVALID_ARG;
    char u[128], us[64], p[64], b[48];
    strlcpy(u, uri, sizeof(u)); strlcpy(us, user, sizeof(us)); strlcpy(p, password, sizeof(p)); strlcpy(b, base, sizeof(b));
    nvs_handle_t h;
    if (nvs_open("hcmqtt", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_str(h, "uri", u);
        nvs_set_str(h, "user", us);
        nvs_set_str(h, "pass", p);
        nvs_set_str(h, "base", b);
        nvs_commit(h);
        nvs_close(h);
    }
    return hc_mqtt_start();
}

void hc_mqtt_print_config(void)
{
    printf("MQTT: uri=%s user=%s base=%s connected=%d\n", s_uri[0] ? s_uri : "(none)", s_user, s_base, s_connected);
}
