// Matter controller (esp-matter controller/commissioner, no Matter server) - see hc_matter.h.
//
// Commissioning: setup code (QR "MT:..." or manual pairing code); transport "thread" (BLE, with this border
// router's active dataset), "wifi" (BLE, with the controller's Wi-Fi credentials) or "network" (device already
// reachable on IP). After success the device is subscribed to Temperature Measurement MeasuredValue (0x0402/0x0000),
// Relative Humidity MeasuredValue (0x0405/0x0000) and Thermostat LocalTemperature (0x0201/0x0000) on all endpoints.
// Readings are mapped by (node, endpoint) to a zone room temperature or the outdoor temperature.
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "hc_matter.h"
#include "hc.h"
#include "hc_net.h"
#include "hc_thread.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#include <app/data-model/Decode.h>
#include <app/data-model/Nullable.h>
#include <esp_matter.h>
#include <esp_matter_controller_client.h>
#include <esp_matter_controller_pairing_command.h>
#include <esp_matter_controller_subscribe_command.h>

using namespace esp_matter::controller;
using chip::app::AttributePathParams;
using chip::app::EventPathParams;
using chip::Platform::ScopedMemoryBufferWithSize;

static const char *TAG = "hc_matter";

#define MAX_DEVS 24
#define MAX_MAPS 16
#define MAX_READINGS 48
#define TARGET_NONE -1
#define TARGET_OUTDOOR 8
#define EP_ANY 0xFFFF

typedef struct {
    uint64_t node;
    char label[24];
    uint8_t used;
    uint8_t transport;   // 0 thread, 1 wifi, 2 network
} dev_t_;
typedef struct {
    uint64_t node;
    uint16_t ep;
    int8_t target;       // 0..7 zone, 8 outdoor, -1 none
    uint8_t used;
} map_t_;
typedef struct {
    uint64_t node;
    uint16_t ep;
    float temp, local_temp, hum;
    int64_t temp_us, local_us, hum_us;
} reading_t;

static dev_t_ s_devs[MAX_DEVS];
static map_t_ s_maps[MAX_MAPS];
static reading_t s_rd[MAX_READINGS];
static bool s_subscribed[MAX_DEVS];
static uint64_t s_next_node = 0x10;
static char s_pending_label[24];
static uint8_t s_pending_transport;
static uint32_t s_stamp = 1;
static hc_matter_event_cb_t s_event_cb;
static esp_timer_handle_t s_retry_timer;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

static const char *const TRANSPORTS[] = {"thread", "wifi", "network"};

static void changed(void) { s_stamp++; }

bool hc_matter_changed_since(uint32_t *stamp)
{
    bool c = *stamp != s_stamp;
    *stamp = s_stamp;
    return c;
}

void hc_matter_set_event_cb(hc_matter_event_cb_t cb) { s_event_cb = cb; }

static void event(const char *type, uint64_t node, const char *detail)
{
    ESP_LOGI(TAG, "%s node 0x%llx %s", type, (unsigned long long) node, detail ? detail : "");
    if (!s_event_cb) return;
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "event", type);
    cJSON_AddNumberToObject(o, "node", (double) node);
    if (detail) cJSON_AddStringToObject(o, "detail", detail);
    char *s = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    if (s) { s_event_cb(s); cJSON_free(s); }
}

static void save(void)
{
    nvs_handle_t h;
    if (nvs_open("hcm", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_blob(h, "devs", s_devs, sizeof(s_devs));
    nvs_set_blob(h, "maps", s_maps, sizeof(s_maps));
    nvs_set_u64(h, "next", s_next_node);
    nvs_commit(h);
    nvs_close(h);
}

static void load(void)
{
    nvs_handle_t h;
    if (nvs_open("hcm", NVS_READONLY, &h) != ESP_OK) return;
    size_t l = sizeof(s_devs);
    if (nvs_get_blob(h, "devs", s_devs, &l) != ESP_OK || l != sizeof(s_devs)) memset(s_devs, 0, sizeof(s_devs));
    l = sizeof(s_maps);
    if (nvs_get_blob(h, "maps", s_maps, &l) != ESP_OK || l != sizeof(s_maps)) memset(s_maps, 0, sizeof(s_maps));
    nvs_get_u64(h, "next", &s_next_node);
    nvs_close(h);
}

static int dev_index(uint64_t node)
{
    for (int i = 0; i < MAX_DEVS; i++)
        if (s_devs[i].used && s_devs[i].node == node) return i;
    return -1;
}

static reading_t *reading(uint64_t node, uint16_t ep, bool create)
{
    reading_t *free_slot = nullptr;
    for (auto &r : s_rd) {
        if (r.node == node && r.ep == ep) return &r;
        if (!r.node && !free_slot) free_slot = &r;
    }
    if (!create || !free_slot) return nullptr;
    *free_slot = {node, ep, NAN, NAN, NAN, 0, 0, 0};
    return free_slot;
}

// The temperature a (node, endpoint) provides: Temperature Measurement if it has reported one, else the
// thermostat's LocalTemperature.
static float reading_temp(const reading_t *r) { return r->temp_us ? r->temp : r->local_temp; }

static void route(const reading_t *r)
{
    const float t = reading_temp(r);
    for (const auto &m : s_maps) {
        if (!m.used || m.node != r->node || (m.ep != EP_ANY && m.ep != r->ep)) continue;
        if (m.target == TARGET_OUTDOOR) hc_input_matter_outdoor(t);
        else if (m.target >= 0 && m.target < HC_ZONES) hc_input_matter_room(m.target, t);
    }
}

// --- subscriptions -------------------------------------------------------------------------------------------

static void on_report(uint64_t node, const chip::app::ConcreteDataAttributePath &path, chip::TLV::TLVReader *data,
                      const chip::app::StatusIB &status)
{
    if (!data) return;
    const uint32_t c = path.mClusterId, a = path.mAttributeId;
    reading_t *r = reading(node, path.mEndpointId, true);
    if (!r || a != 0x0000) return;
    const int64_t now = esp_timer_get_time();
    if (c == 0x0402 || c == 0x0201) {
        chip::app::DataModel::Nullable<int16_t> v;
        if (chip::app::DataModel::Decode(*data, v) != CHIP_NO_ERROR) return;
        const float t = v.IsNull() ? NAN : v.Value() / 100.0f;
        if (c == 0x0402) { r->temp = t; r->temp_us = now; }
        else { r->local_temp = t; r->local_us = now; }
    } else if (c == 0x0405) {
        chip::app::DataModel::Nullable<uint16_t> v;
        if (chip::app::DataModel::Decode(*data, v) != CHIP_NO_ERROR) return;
        r->hum = v.IsNull() ? NAN : v.Value() / 100.0f;
        r->hum_us = now;
        changed();
        return;
    } else {
        return;
    }
    ESP_LOGD(TAG, "node 0x%llx ep %u cluster 0x%04lx: %.2f", (unsigned long long) node, path.mEndpointId,
             (unsigned long) c, reading_temp(r));
    route(r);
    changed();
}

static void subscribe(uint64_t node);

static void sub_ended(uint64_t node, uint32_t sub_id)
{
    int i = dev_index(node);
    if (i >= 0) s_subscribed[i] = false;
    event("subscription_ended", node, nullptr);
    esp_timer_start_once(s_retry_timer, 60 * 1000000ULL);
}

static void sub_established(uint64_t node, uint32_t sub_id)
{
    int i = dev_index(node);
    if (i >= 0) s_subscribed[i] = true;
    event("subscribed", node, nullptr);
    changed();
}

static void subscribe(uint64_t node)
{
    ScopedMemoryBufferWithSize<AttributePathParams> attrs;
    ScopedMemoryBufferWithSize<EventPathParams> events;
    attrs.Alloc(3);
    if (!attrs.Get()) return;
    attrs[0] = AttributePathParams(chip::ClusterId(0x0402), chip::AttributeId(0x0000));   // Temperature Measurement / MeasuredValue
    attrs[1] = AttributePathParams(chip::ClusterId(0x0405), chip::AttributeId(0x0000));   // Relative Humidity Measurement / MeasuredValue
    attrs[2] = AttributePathParams(chip::ClusterId(0x0201), chip::AttributeId(0x0000));   // Thermostat / LocalTemperature
    subscribe_command::subscription_established_cb_t est = sub_established;
    subscribe_command::subscription_terminated_cb_t term = sub_ended;
    on_connect_failure_cb_t fail = [](void *, const chip::ScopedNodeId &peer, CHIP_ERROR err) {
        event("unreachable", peer.GetNodeId(), chip::ErrorStr(err));
        esp_timer_start_once(s_retry_timer, 60 * 1000000ULL);
    };
    auto *cmd = chip::Platform::New<subscribe_command>(node, std::move(attrs), std::move(events), 10, 600, true,
                                                       on_report, nullptr, est, term, fail, true);
    if (cmd) cmd->send_command();   // deletes itself on failure
}

static void resubscribe_missing(void *)
{
    esp_matter::lock::ScopedChipStackLock lock(portMAX_DELAY);
    for (int i = 0; i < MAX_DEVS; i++)
        if (s_devs[i].used && !s_subscribed[i]) subscribe(s_devs[i].node);
}

// --- commissioning -------------------------------------------------------------------------------------------

static void on_pase(CHIP_ERROR err)
{
    if (err != CHIP_NO_ERROR) event("commissioning_failed", 0, chip::ErrorStr(err));
}

static void on_success(chip::ScopedNodeId peer)
{
    const uint64_t node = peer.GetNodeId();
    if (dev_index(node) < 0) {
        for (auto &d : s_devs)
            if (!d.used) {
                d = {};
                d.node = node;
                d.used = 1;
                d.transport = s_pending_transport;
                strlcpy(d.label, s_pending_label, sizeof(d.label));
                break;
            }
        save();
    }
    event("commissioned", node, s_pending_label);
    changed();
    subscribe(node);
}

static void on_failure(chip::ScopedNodeId peer, CHIP_ERROR err, chip::Controller::CommissioningStage stage,
                       std::optional<chip::Credentials::AttestationVerificationResult> info)
{
    event("commissioning_failed", peer.GetNodeId(), chip::ErrorStr(err));
}

static void forget(uint64_t node)
{
    int i = dev_index(node);
    if (i >= 0) { s_devs[i].used = 0; s_subscribed[i] = false; }
    for (auto &m : s_maps)
        if (m.used && m.node == node) m.used = 0;
    for (auto &r : s_rd)
        if (r.node == node) r = {};
    save();
    changed();
}

static void on_unpair(chip::NodeId node, CHIP_ERROR err)
{
    forget(node);
    event("removed", node, err == CHIP_NO_ERROR ? nullptr : chip::ErrorStr(err));
}

esp_err_t hc_matter_start(void)
{
    load();
    const esp_timer_create_args_t ta = {.callback = resubscribe_missing, .arg = nullptr,
                                        .dispatch_method = ESP_TIMER_TASK, .name = "hcm_retry",
                                        .skip_unhandled_events = true};
    esp_timer_create(&ta, &s_retry_timer);
    {
        esp_matter::lock::ScopedChipStackLock lock(portMAX_DELAY);
        auto &client = matter_controller_client::get_instance();
        client.init(112233, 1, 5580);   // controller node id, fabric id, listen port
        client.setup_commissioner();
        pairing_command::get_instance().set_callbacks({on_pase, on_success, on_failure, on_unpair});
    }
    // Give the network and Thread time to come up, then subscribe to every known device.
    esp_timer_start_once(s_retry_timer, 20 * 1000000ULL);
    return ESP_OK;
}

static int parse_target(const cJSON *v)
{
    if (cJSON_IsNumber(v)) return (v->valueint >= 1 && v->valueint <= HC_ZONES) ? v->valueint - 1 : -2;
    if (!cJSON_IsString(v)) return -2;
    const char *s = v->valuestring;
    if (!strcasecmp(s, "outdoor")) return TARGET_OUTDOOR;
    if (!strcasecmp(s, "none")) return TARGET_NONE;
    if (!strncasecmp(s, "zone", 4)) {
        int z = atoi(s + 4);
        return (z >= 1 && z <= HC_ZONES) ? z - 1 : -2;
    }
    for (int i = 0; i < HC_ZONES; i++)
        if (!strcasecmp(s, hc_zone_name(i))) return i;
    return -2;
}

static bool get_node(const cJSON *cmd, uint64_t *node)
{
    const cJSON *n = cJSON_GetObjectItem(cmd, "node");
    if (cJSON_IsNumber(n) && n->valuedouble > 0) { *node = (uint64_t) n->valuedouble; return true; }
    if (cJSON_IsString(n)) { *node = strtoull(n->valuestring, nullptr, 0); return *node != 0; }
    return false;
}

esp_err_t hc_matter_command(const cJSON *cmd, char **err)
{
    auto fail = [err](const char *m) { if (err) *err = strdup(m); return ESP_ERR_INVALID_ARG; };
    if (err) *err = nullptr;
    const cJSON *c = cJSON_GetObjectItem(cmd, "cmd");
    if (!cJSON_IsString(c)) return fail("missing cmd");
    const char *name = c->valuestring;
    uint64_t node = 0;

    if (!strcmp(name, "commission")) {
        const cJSON *code = cJSON_GetObjectItem(cmd, "code");
        const cJSON *tr = cJSON_GetObjectItem(cmd, "transport");
        const cJSON *label = cJSON_GetObjectItem(cmd, "label");
        if (!cJSON_IsString(code)) return fail("missing code (QR 'MT:...' or manual pairing code)");
        int t = 0;
        if (cJSON_IsString(tr)) {
            t = -1;
            for (int i = 0; i < 3; i++)
                if (!strcasecmp(tr->valuestring, TRANSPORTS[i])) t = i;
            if (t < 0) return fail("transport must be thread, wifi or network");
        }
        node = s_next_node++;
        save();
        strlcpy(s_pending_label, cJSON_IsString(label) ? label->valuestring : "", sizeof(s_pending_label));
        s_pending_transport = (uint8_t) t;
        esp_err_t e;
        esp_matter::lock::ScopedChipStackLock lock(portMAX_DELAY);
        if (t == 0) {
            uint8_t ds[254], len = 0;
            if (!hc_thread_get_dataset(ds, &len)) return fail("no active Thread dataset");
            e = pairing_code_thread(node, code->valuestring, ds, len);
        } else if (t == 1) {
            char ssid[33], pw[65];
            if (!hc_net_get_wifi(ssid, sizeof(ssid), pw, sizeof(pw))) return fail("no Wi-Fi credentials configured");
            e = pairing_code_wifi(node, ssid, pw, code->valuestring);
        } else {
            e = pairing_code(node, code->valuestring);
        }
        if (e != ESP_OK) return fail("commissioning could not be started");
        event("commissioning_started", node, TRANSPORTS[t]);
        return ESP_OK;
    }
    if (!strcmp(name, "remove") || !strcmp(name, "forget")) {
        if (!get_node(cmd, &node) || dev_index(node) < 0) return fail("unknown node");
        if (!strcmp(name, "forget")) { forget(node); event("forgotten", node, nullptr); return ESP_OK; }
        esp_matter::lock::ScopedChipStackLock lock(portMAX_DELAY);
        if (unpair_device(node) != ESP_OK) return fail("unpair failed; use forget to drop it locally");
        return ESP_OK;
    }
    if (!strcmp(name, "map")) {
        if (!get_node(cmd, &node)) return fail("missing node");
        const int target = parse_target(cJSON_GetObjectItem(cmd, "target"));
        if (target == -2) return fail("target must be zone1..zone8, a zone name, outdoor or none");
        const cJSON *e = cJSON_GetObjectItem(cmd, "endpoint");
        const uint16_t ep = cJSON_IsNumber(e) ? (uint16_t) e->valueint : EP_ANY;
        taskENTER_CRITICAL(&s_mux);
        map_t_ *slot = nullptr;
        for (auto &m : s_maps)
            if (m.used && m.node == node && m.ep == ep) slot = &m;
        if (!slot)
            for (auto &m : s_maps)
                if (!m.used) { slot = &m; break; }
        if (slot) {
            if (target == TARGET_NONE) slot->used = 0;
            else *slot = {node, ep, (int8_t) target, 1};
        }
        taskEXIT_CRITICAL(&s_mux);
        if (!slot) return fail("mapping table full");
        save();
        changed();
        for (const auto &r : s_rd)
            if (r.node == node) route(&r);
        return ESP_OK;
    }
    if (!strcmp(name, "label")) {
        const cJSON *l = cJSON_GetObjectItem(cmd, "label");
        int i;
        if (!get_node(cmd, &node) || (i = dev_index(node)) < 0 || !cJSON_IsString(l)) return fail("node/label");
        strlcpy(s_devs[i].label, l->valuestring, sizeof(s_devs[i].label));
        save();
        changed();
        return ESP_OK;
    }
    if (!strcmp(name, "resubscribe")) {
        for (bool &s : s_subscribed) s = false;
        resubscribe_missing(nullptr);
        return ESP_OK;
    }
    return fail("unknown cmd");
}

static void add_num(cJSON *o, const char *k, float v, int64_t us)
{
    if (!us || isnan(v)) return;
    cJSON_AddNumberToObject(o, k, roundf(v * 100.0f) / 100.0f);
}

cJSON *hc_matter_devices_to_json(void)
{
    cJSON *o = cJSON_CreateObject();
    cJSON *devs = cJSON_AddArrayToObject(o, "devices");
    const int64_t now = esp_timer_get_time();
    for (int i = 0; i < MAX_DEVS; i++) {
        if (!s_devs[i].used) continue;
        cJSON *d = cJSON_CreateObject();
        cJSON_AddNumberToObject(d, "node", (double) s_devs[i].node);
        cJSON_AddStringToObject(d, "label", s_devs[i].label);
        cJSON_AddStringToObject(d, "transport", TRANSPORTS[s_devs[i].transport % 3]);
        cJSON_AddBoolToObject(d, "subscribed", s_subscribed[i]);
        cJSON *eps = cJSON_AddArrayToObject(d, "endpoints");
        for (const auto &r : s_rd) {
            if (r.node != s_devs[i].node) continue;
            cJSON *e = cJSON_CreateObject();
            cJSON_AddNumberToObject(e, "endpoint", r.ep);
            add_num(e, "temperature", r.temp, r.temp_us);
            add_num(e, "local_temperature", r.local_temp, r.local_us);
            add_num(e, "humidity", r.hum, r.hum_us);
            const int64_t last = r.temp_us > r.local_us ? r.temp_us : r.local_us;
            if (last) cJSON_AddNumberToObject(e, "age_s", (double) ((now - last) / 1000000));
            cJSON_AddItemToArray(eps, e);
        }
        cJSON_AddItemToArray(devs, d);
    }
    cJSON *maps = cJSON_AddArrayToObject(o, "mappings");
    for (const auto &m : s_maps) {
        if (!m.used) continue;
        cJSON *j = cJSON_CreateObject();
        cJSON_AddNumberToObject(j, "node", (double) m.node);
        if (m.ep == EP_ANY) cJSON_AddStringToObject(j, "endpoint", "any");
        else cJSON_AddNumberToObject(j, "endpoint", m.ep);
        char t[12];
        if (m.target == TARGET_OUTDOOR) strcpy(t, "outdoor");
        else snprintf(t, sizeof(t), "zone%d", m.target + 1);
        cJSON_AddStringToObject(j, "target", t);
        cJSON_AddItemToArray(maps, j);
    }
    return o;
}
