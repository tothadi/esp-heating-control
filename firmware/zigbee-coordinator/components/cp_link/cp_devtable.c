/*
 * ESP Heating Control - co-processor device table. See cp_devtable.h.
 */
#include <inttypes.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "sdkconfig.h"

#include "cp_devtable.h"
#include "cp_link.h"

static const char *TAG = "cp_dev";

#define NVS_NAMESPACE  "cp"
#define NVS_KEY        "devs"
#define TABLE_VERSION  1
#define MAX_DEVICES    CONFIG_CP_MAX_DEVICES
#define MAX_READINGS   6 /* per device: temperature, humidity, local temperature, setpoint, demand, spare */

/* Persistent part (stored as one NVS blob, versioned) */
typedef struct __attribute__((packed)) {
    uint64_t id;
    uint16_t addr;
    uint8_t  flags;
    char     name[CP_NAME_MAX + 1];
    char     model[CP_MODEL_MAX + 1];
} dev_persist_t;

typedef struct {
    uint8_t  ep;
    uint16_t cluster;
    uint16_t attr;
    float    value;
    int64_t  t_us; /* 0 = empty slot */
} reading_t;

typedef struct {
    bool          used;
    dev_persist_t p;
    int64_t       last_seen_us; /* 0 = not seen since boot */
    reading_t     rd[MAX_READINGS];
} dev_t;

static dev_t             s_dev[MAX_DEVICES];
static SemaphoreHandle_t s_mutex;

#define LOCK()   xSemaphoreTake(s_mutex, portMAX_DELAY)
#define UNLOCK() xSemaphoreGive(s_mutex)

static dev_t *find(uint64_t id)
{
    for (int i = 0; i < MAX_DEVICES; i++) {
        if (s_dev[i].used && s_dev[i].p.id == id) {
            return &s_dev[i];
        }
    }
    return NULL;
}

/* Called with the lock held. */
static void save(void)
{
    static dev_persist_t buf[MAX_DEVICES];
    size_t               n = 0;
    for (int i = 0; i < MAX_DEVICES; i++) {
        if (s_dev[i].used) {
            buf[n++] = s_dev[i].p;
        }
    }
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed");
        return;
    }
    uint8_t ver = TABLE_VERSION;
    esp_err_t err = nvs_set_u8(h, "ver", ver);
    if (err == ESP_OK) {
        err = n ? nvs_set_blob(h, NVS_KEY, buf, n * sizeof(dev_persist_t)) : nvs_erase_key(h, NVS_KEY);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            err = ESP_OK;
        }
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Saving the device table failed: %s", esp_err_to_name(err));
    }
}

esp_err_t cp_dev_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
        return ESP_ERR_NO_MEM;
    }
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        return ESP_OK; /* nothing stored yet */
    }
    uint8_t ver = 0;
    size_t  len = 0;
    if (nvs_get_u8(h, "ver", &ver) == ESP_OK && ver == TABLE_VERSION && nvs_get_blob(h, NVS_KEY, NULL, &len) == ESP_OK &&
        len % sizeof(dev_persist_t) == 0 && len <= sizeof(dev_persist_t) * MAX_DEVICES) {
        static dev_persist_t buf[MAX_DEVICES];
        if (nvs_get_blob(h, NVS_KEY, buf, &len) == ESP_OK) {
            size_t n = len / sizeof(dev_persist_t);
            for (size_t i = 0; i < n; i++) {
                s_dev[i].used = true;
                s_dev[i].p    = buf[i];
                s_dev[i].p.name[CP_NAME_MAX]   = '\0';
                s_dev[i].p.model[CP_MODEL_MAX] = '\0';
            }
            ESP_LOGI(TAG, "Loaded %u devices from NVS", (unsigned)n);
        }
    }
    nvs_close(h);
    return ESP_OK;
}

bool cp_dev_touch(uint64_t id, uint16_t addr, bool *is_new)
{
    bool created = false, changed = false, ok = true;
    LOCK();
    dev_t *d = find(id);
    if (!d) {
        for (int i = 0; i < MAX_DEVICES; i++) {
            if (!s_dev[i].used) {
                d = &s_dev[i];
                memset(d, 0, sizeof(*d));
                d->used = true;
                d->p.id = id;
                d->p.addr = CP_ADDR_UNKNOWN;
                created = changed = true;
                break;
            }
        }
    }
    if (d) {
        if (addr != CP_ADDR_UNKNOWN && d->p.addr != addr) {
            d->p.addr = addr;
            changed   = true;
        }
        d->last_seen_us = esp_timer_get_time();
        if (changed) {
            save();
        }
    } else {
        ok = false;
    }
    UNLOCK();
    if (!ok) {
        char ids[17];
        cp_link_format_id(id, ids);
        ESP_LOGW(TAG, "Device table full, %s ignored", ids);
        cp_link_send_err("DEV", CP_ERR_TABLE_FULL, "device table full");
    }
    if (is_new) {
        *is_new = created;
    }
    return ok;
}

bool cp_dev_id_by_addr(uint16_t addr, uint64_t *id)
{
    bool found = false;
    LOCK();
    for (int i = 0; i < MAX_DEVICES; i++) {
        if (s_dev[i].used && s_dev[i].p.addr == addr) {
            *id   = s_dev[i].p.id;
            found = true;
            break;
        }
    }
    UNLOCK();
    return found;
}

bool cp_dev_addr_by_id(uint64_t id, uint16_t *addr)
{
    LOCK();
    dev_t *d = find(id);
    if (d) {
        *addr = d->p.addr;
    }
    UNLOCK();
    return d != NULL;
}

void cp_dev_set_model(uint64_t id, const char *model, size_t len)
{
    char tmp[CP_MODEL_MAX + 1];
    cp_link_sanitize(tmp, sizeof(tmp), model, len);
    LOCK();
    dev_t *d = find(id);
    if (d && strcmp(d->p.model, tmp) != 0) {
        strcpy(d->p.model, tmp);
        save();
    }
    UNLOCK();
}

void cp_dev_add_flags(uint64_t id, uint8_t flags)
{
    LOCK();
    dev_t *d = find(id);
    if (d && (d->p.flags | flags) != d->p.flags) {
        d->p.flags |= flags;
        save();
    }
    UNLOCK();
}

int cp_dev_set_name(uint64_t id, const char *name)
{
    int err = CP_ERR_NOT_FOUND;
    LOCK();
    dev_t *d = find(id);
    if (d) {
        strlcpy(d->p.name, name, sizeof(d->p.name));
        save();
        err = 0;
    }
    UNLOCK();
    return err;
}

int cp_dev_forget(uint64_t id)
{
    int err = CP_ERR_NOT_FOUND;
    LOCK();
    dev_t *d = find(id);
    if (d) {
        memset(d, 0, sizeof(*d));
        save();
        err = 0;
    }
    UNLOCK();
    return err;
}

void cp_dev_clear_all(void)
{
    LOCK();
    memset(s_dev, 0, sizeof(s_dev));
    save();
    UNLOCK();
}

void cp_dev_reading(uint64_t id, uint8_t ep, uint16_t cluster, uint16_t attr, float value)
{
    LOCK();
    dev_t *d = find(id);
    if (d) {
        /* same ep/cluster/attr, else a free slot, else the oldest one */
        reading_t *match = NULL, *free_slot = NULL, *oldest = &d->rd[0];
        for (int i = 0; i < MAX_READINGS; i++) {
            reading_t *r = &d->rd[i];
            if (r->t_us == 0) {
                if (!free_slot) {
                    free_slot = r;
                }
            } else if (r->ep == ep && r->cluster == cluster && r->attr == attr) {
                match = r;
                break;
            } else if (r->t_us < oldest->t_us) {
                oldest = r;
            }
        }
        reading_t *slot = match ? match : (free_slot ? free_slot : oldest);
        slot->ep      = ep;
        slot->cluster = cluster;
        slot->attr    = attr;
        slot->value   = value;
        slot->t_us    = esp_timer_get_time();
        d->last_seen_us = slot->t_us;
    }
    UNLOCK();
    if (d) {
        char ids[17];
        cp_link_format_id(id, ids);
        cp_link_send("RD", "%s,%u,%04x,%04x,%.2f,0", ids, ep, cluster, attr, value);
    }
}

static void flags_str(uint8_t f, char out[4])
{
    int n = 0;
    if (f & CP_DEV_F_TEMP) {
        out[n++] = 'T';
    }
    if (f & CP_DEV_F_HUMIDITY) {
        out[n++] = 'H';
    }
    if (f & CP_DEV_F_TSTAT) {
        out[n++] = 'S';
    }
    if (n == 0) {
        out[n++] = '-';
    }
    out[n] = '\0';
}

void cp_dev_send_list(void)
{
    /* Copy under the lock, send without it (UART writes may block). */
    static dev_t copy[MAX_DEVICES];
    LOCK();
    memcpy(copy, s_dev, sizeof(copy));
    UNLOCK();
    int64_t now   = esp_timer_get_time();
    int     count = 0;
    for (int i = 0; i < MAX_DEVICES; i++) {
        const dev_t *d = &copy[i];
        if (!d->used) {
            continue;
        }
        char ids[17], fl[4];
        cp_link_format_id(d->p.id, ids);
        flags_str(d->p.flags, fl);
        long age = d->last_seen_us ? (long)((now - d->last_seen_us) / 1000000) : -1;
        cp_link_send("DEV", "%s,%04x,%s,%ld,%s,%s", ids, d->p.addr, fl, age, d->p.name, d->p.model);
        count++;
    }
    cp_link_send("DEVEND", "%d", count);
}

void cp_dev_send_dump(void)
{
    static dev_t copy[MAX_DEVICES];
    LOCK();
    memcpy(copy, s_dev, sizeof(copy));
    UNLOCK();
    int64_t now   = esp_timer_get_time();
    int     count = 0;
    for (int i = 0; i < MAX_DEVICES; i++) {
        const dev_t *d = &copy[i];
        if (!d->used) {
            continue;
        }
        char ids[17];
        cp_link_format_id(d->p.id, ids);
        for (int j = 0; j < MAX_READINGS; j++) {
            const reading_t *r = &d->rd[j];
            if (r->t_us) {
                cp_link_send("RD", "%s,%u,%04x,%04x,%.2f,%ld", ids, r->ep, r->cluster, r->attr, r->value,
                             (long)((now - r->t_us) / 1000000));
                count++;
            }
        }
    }
    cp_link_send("DUMPEND", "%d", count);
}
