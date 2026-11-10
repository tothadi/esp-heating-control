/*
 * ESP Heating Control - Zigbee coordinator for the ESP32-H2 co-processor.
 *
 * Forms a Zigbee network, lets devices join on command from the ESP32-S3 (permit join), interviews every device
 * that announces itself (match descriptor -> simple descriptor -> Basic model id), binds its Temperature
 * Measurement (0x0402), Relative Humidity (0x0405) and Thermostat (0x0201) server clusters to the coordinator,
 * configures attribute reporting and forwards every report to the S3 as "$CP,RD,..." lines (cp_link).
 *
 * Built on esp-zigbee-lib 2.x; the stack calls follow the SDK's home_automation_devices/thermostat example (a
 * coordinator that binds and configures a remote temperature sensor). Untested on hardware.
 */
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"

#include "esp_zigbee.h"
#include "ezbee/zha.h"

#include "cp_devtable.h"
#include "cp_link.h"

static const char *TAG = "ZB_COORD";

#define FW_VERSION "0.1.0"

#define LOCAL_EP 1 /* coordinator endpoint that devices bind and report to */

#define ESP_ZIGBEE_STORAGE_PARTITION_NAME "nvs"
#define ESP_MANUFACTURER_NAME             "\x0f" "esp-heating-ctl" /* ZCL string: length byte + text */
#define ESP_MODEL_IDENTIFIER              "\x0e" "h2-coordinator"

#define CL_TEMP     EZB_ZCL_CLUSTER_ID_TEMPERATURE_MEASUREMENT /* 0x0402 */
#define CL_HUMIDITY EZB_ZCL_CLUSTER_ID_REL_HUMIDITY_MEASUREMENT /* 0x0405 */
#define CL_TSTAT    EZB_ZCL_CLUSTER_ID_THERMOSTAT /* 0x0201 */

/* Network state for "$CP,NET" */
static volatile bool     s_net_up;
static volatile int64_t  s_permit_until_us;
static volatile int      s_channel; /* cached: net_status() runs in other tasks and must not take the stack lock */
static volatile uint16_t s_pan_id;

/* --------------------------------------------------------------------------------------------------------------- */
/* Value conversion: ZCL attribute -> physical value forwarded to the S3. Returns false for "invalid" values.       */

static bool convert_value(uint16_t cluster, uint16_t attr, uint8_t type, const void *v, float *out)
{
    if (!v) {
        return false;
    }
    if (cluster == CL_TEMP && attr == EZB_ZCL_ATTR_TEMPERATURE_MEASUREMENT_MEASURED_VALUE_ID &&
        type == EZB_ZCL_ATTR_TYPE_INT16) {
        int16_t raw = *(const int16_t *)v; /* 0.01 °C, 0x8000 = invalid */
        *out = raw / 100.0f;
        return raw != INT16_MIN;
    }
    if (cluster == CL_HUMIDITY && attr == EZB_ZCL_ATTR_REL_HUMIDITY_MEASUREMENT_MEASURED_VALUE_ID &&
        type == EZB_ZCL_ATTR_TYPE_UINT16) {
        uint16_t raw = *(const uint16_t *)v; /* 0.01 %RH, 0xffff = invalid */
        *out = raw / 100.0f;
        return raw != 0xFFFF;
    }
    if (cluster == CL_TSTAT && (attr == EZB_ZCL_ATTR_THERMOSTAT_LOCAL_TEMPERATURE_ID ||
                                attr == EZB_ZCL_ATTR_THERMOSTAT_OCCUPIED_HEATING_SETPOINT_ID) &&
        type == EZB_ZCL_ATTR_TYPE_INT16) {
        int16_t raw = *(const int16_t *)v; /* 0.01 °C, 0x8000 = invalid */
        *out = raw / 100.0f;
        return raw != INT16_MIN;
    }
    if (cluster == CL_TSTAT && attr == EZB_ZCL_ATTR_THERMOSTAT_PI_HEATING_DEMAND_ID && type == EZB_ZCL_ATTR_TYPE_UINT8) {
        *out = *(const uint8_t *)v; /* % */
        return true;
    }
    return false;
}

static uint8_t cluster_flag(uint16_t cluster)
{
    switch (cluster) {
    case CL_TEMP:
        return CP_DEV_F_TEMP;
    case CL_HUMIDITY:
        return CP_DEV_F_HUMIDITY;
    case CL_TSTAT:
        return CP_DEV_F_TSTAT;
    default:
        return 0;
    }
}

/* Device id for a short address: device table first, then the stack's address map. */
static bool id_for_short(uint16_t short_addr, uint64_t *id)
{
    if (cp_dev_id_by_addr(short_addr, id)) {
        return true;
    }
    ezb_extaddr_t ext;
    if (ezb_address_extended_by_short(short_addr, &ext) == EZB_ERR_NONE) {
        *id = ext.u64;
        return cp_dev_touch(*id, short_addr, NULL);
    }
    return false;
}

static void forward_value(uint16_t short_addr, uint8_t ep, uint16_t cluster, uint16_t attr, uint8_t type,
                          const void *value)
{
    float    f;
    uint64_t id;
    if (!convert_value(cluster, attr, type, value, &f)) {
        ESP_LOGD(TAG, "Ignored 0x%04hx ep %u cluster 0x%04x attr 0x%04x type 0x%02x", short_addr, ep, cluster, attr, type);
        return;
    }
    if (!id_for_short(short_addr, &id)) {
        ESP_LOGW(TAG, "Value from unknown device 0x%04hx dropped", short_addr);
        return;
    }
    cp_dev_touch(id, short_addr, NULL);
    cp_dev_reading(id, ep, cluster, attr, f);
}

/* --------------------------------------------------------------------------------------------------------------- */
/* Interview: match descriptor -> simple descriptor -> read model id, bind, configure reporting, read values.       */

typedef struct {
    uint16_t short_addr;
    uint8_t  ep;
    uint16_t cluster;
} bind_ctx_t;

static void config_reporting(uint16_t short_addr, uint8_t ep, uint16_t cluster)
{
    /* Report intervals in seconds; reportable change in the attribute's unit (0.01 °C, 0.01 %RH, %). Battery
     * sensors may clamp these to their own limits. */
    ezb_zcl_config_report_record_t rec[3];
    uint16_t                       n = 0;
    memset(rec, 0, sizeof(rec));

    if (cluster == CL_TEMP) {
        rec[n].direction                    = EZB_ZCL_REPORTING_SEND;
        rec[n].attr_id                      = EZB_ZCL_ATTR_TEMPERATURE_MEASUREMENT_MEASURED_VALUE_ID;
        rec[n].client.attr_type             = EZB_ZCL_ATTR_TYPE_INT16;
        rec[n].client.min_interval          = 30;
        rec[n].client.max_interval          = 600;
        rec[n].client.reportable_change.s16 = 10;
        n++;
    } else if (cluster == CL_HUMIDITY) {
        rec[n].direction                    = EZB_ZCL_REPORTING_SEND;
        rec[n].attr_id                      = EZB_ZCL_ATTR_REL_HUMIDITY_MEASUREMENT_MEASURED_VALUE_ID;
        rec[n].client.attr_type             = EZB_ZCL_ATTR_TYPE_UINT16;
        rec[n].client.min_interval          = 60;
        rec[n].client.max_interval          = 900;
        rec[n].client.reportable_change.u16 = 100;
        n++;
    } else if (cluster == CL_TSTAT) {
        rec[n].direction                    = EZB_ZCL_REPORTING_SEND;
        rec[n].attr_id                      = EZB_ZCL_ATTR_THERMOSTAT_LOCAL_TEMPERATURE_ID;
        rec[n].client.attr_type             = EZB_ZCL_ATTR_TYPE_INT16;
        rec[n].client.min_interval          = 30;
        rec[n].client.max_interval          = 600;
        rec[n].client.reportable_change.s16 = 10;
        n++;
        rec[n].direction                    = EZB_ZCL_REPORTING_SEND;
        rec[n].attr_id                      = EZB_ZCL_ATTR_THERMOSTAT_OCCUPIED_HEATING_SETPOINT_ID;
        rec[n].client.attr_type             = EZB_ZCL_ATTR_TYPE_INT16;
        rec[n].client.min_interval          = 1;
        rec[n].client.max_interval          = 3600;
        rec[n].client.reportable_change.s16 = 10;
        n++;
        /* PIHeatingDemand is optional; devices without it answer this record with UNSUPPORTED_ATTRIBUTE. */
        rec[n].direction                    = EZB_ZCL_REPORTING_SEND;
        rec[n].attr_id                      = EZB_ZCL_ATTR_THERMOSTAT_PI_HEATING_DEMAND_ID;
        rec[n].client.attr_type             = EZB_ZCL_ATTR_TYPE_UINT8;
        rec[n].client.min_interval          = 10;
        rec[n].client.max_interval          = 900;
        rec[n].client.reportable_change.u8  = 5;
        n++;
    } else {
        return;
    }

    ezb_zcl_config_report_cmd_t cmd = {
        .cmd_ctrl =
            {
                .dst_addr.addr_mode    = EZB_ADDR_MODE_SHORT,
                .dst_addr.u.short_addr = short_addr,
                .dst_ep                = ep,
                .src_ep                = LOCAL_EP,
                .cluster_id            = cluster,
            },
        .payload.record_number = n,
        .payload.record_field  = rec,
    };
    ezb_err_t ret = ezb_zcl_config_report_cmd_req(&cmd);
    if (ret != EZB_ERR_NONE) {
        ESP_LOGE(TAG, "Configure reporting 0x%04hx/%u cluster 0x%04x failed (0x%04x)", short_addr, ep, cluster, ret);
    }
}

static void read_values(uint16_t short_addr, uint8_t ep, uint16_t cluster)
{
    uint16_t attrs[3];
    uint16_t n = 0;
    if (cluster == CL_TEMP) {
        attrs[n++] = EZB_ZCL_ATTR_TEMPERATURE_MEASUREMENT_MEASURED_VALUE_ID;
    } else if (cluster == CL_HUMIDITY) {
        attrs[n++] = EZB_ZCL_ATTR_REL_HUMIDITY_MEASUREMENT_MEASURED_VALUE_ID;
    } else if (cluster == CL_TSTAT) {
        attrs[n++] = EZB_ZCL_ATTR_THERMOSTAT_LOCAL_TEMPERATURE_ID;
        attrs[n++] = EZB_ZCL_ATTR_THERMOSTAT_OCCUPIED_HEATING_SETPOINT_ID;
        attrs[n++] = EZB_ZCL_ATTR_THERMOSTAT_PI_HEATING_DEMAND_ID;
    } else {
        return;
    }
    ezb_zcl_read_attr_cmd_t cmd = {
        .cmd_ctrl =
            {
                .dst_addr.addr_mode    = EZB_ADDR_MODE_SHORT,
                .dst_addr.u.short_addr = short_addr,
                .dst_ep                = ep,
                .src_ep                = LOCAL_EP,
                .cluster_id            = cluster,
            },
        .payload.attr_number = n,
        .payload.attr_field  = attrs,
    };
    ezb_zcl_read_attr_cmd_req(&cmd);
}

static void read_model(uint16_t short_addr, uint8_t ep)
{
    uint16_t attrs[] = {EZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID};
    ezb_zcl_read_attr_cmd_t cmd = {
        .cmd_ctrl =
            {
                .dst_addr.addr_mode    = EZB_ADDR_MODE_SHORT,
                .dst_addr.u.short_addr = short_addr,
                .dst_ep                = ep,
                .src_ep                = LOCAL_EP,
                .cluster_id            = EZB_ZCL_CLUSTER_ID_BASIC,
            },
        .payload.attr_number = 1,
        .payload.attr_field  = attrs,
    };
    ezb_zcl_read_attr_cmd_req(&cmd);
}

static void bind_result(const ezb_zdp_bind_req_result_t *result, void *user_ctx)
{
    bind_ctx_t *ctx = (bind_ctx_t *)user_ctx;
    if (result->error == EZB_ERR_NONE && result->rsp && result->rsp->status == EZB_ZDP_STATUS_SUCCESS) {
        ESP_LOGI(TAG, "Bound 0x%04hx/%u cluster 0x%04x to the coordinator", ctx->short_addr, ctx->ep, ctx->cluster);
        config_reporting(ctx->short_addr, ctx->ep, ctx->cluster);
        read_values(ctx->short_addr, ctx->ep, ctx->cluster);
    } else {
        ESP_LOGW(TAG, "Bind 0x%04hx/%u cluster 0x%04x failed (error 0x%04x, status 0x%02x)", ctx->short_addr, ctx->ep,
                 ctx->cluster, result->error, result->rsp ? result->rsp->status : 0xff);
        /* Some devices report to the coordinator without a binding; try anyway. */
        config_reporting(ctx->short_addr, ctx->ep, ctx->cluster);
        read_values(ctx->short_addr, ctx->ep, ctx->cluster);
    }
    free(ctx);
}

/* Ask the remote device to bind its server cluster to our endpoint (remote -> coordinator), as in the SDK example. */
static void bind_cluster(uint16_t short_addr, uint8_t ep, uint16_t cluster)
{
    ezb_extaddr_t remote;
    if (ezb_address_extended_by_short(short_addr, &remote) != EZB_ERR_NONE) {
        ESP_LOGE(TAG, "No IEEE address for 0x%04hx", short_addr);
        return;
    }
    bind_ctx_t *ctx = malloc(sizeof(bind_ctx_t));
    if (!ctx) {
        return;
    }
    *ctx = (bind_ctx_t){.short_addr = short_addr, .ep = ep, .cluster = cluster};

    ezb_zdo_bind_req_t req = {0};
    req.dst_nwk_addr        = short_addr;
    req.field.src_addr      = remote;
    req.field.src_ep        = ep;
    req.field.cluster_id    = cluster;
    req.field.dst_addr_mode = EZB_ADDR_MODE_EXT;
    ezb_nwk_get_extended_address(&req.field.dst_addr.extended_addr);
    req.field.dst_ep = LOCAL_EP;
    req.cb           = bind_result;
    req.user_ctx     = ctx;

    if (ezb_zdo_bind_req(&req) != EZB_ERR_NONE) {
        ESP_LOGE(TAG, "Bind request to 0x%04hx failed", short_addr);
        free(ctx);
    }
}

static void simple_desc_result(const ezb_zdo_simple_desc_req_result_t *result, void *user_ctx)
{
    if (result->error != EZB_ERR_NONE || !result->rsp || result->rsp->status != EZB_ZDP_STATUS_SUCCESS) {
        ESP_LOGW(TAG, "Simple descriptor request failed (0x%04x)", result->error);
        return;
    }
    const ezb_af_simple_desc_t *d          = &result->rsp->desc;
    uint16_t                    short_addr = result->rsp->nwk_addr_of_interest;
    uint64_t                    id;
    if (!id_for_short(short_addr, &id)) {
        return;
    }
    bool has_basic = false;
    for (int i = 0; i < d->app_input_cluster_count; i++) {
        uint16_t c = d->app_cluster_list[i];
        if (c == EZB_ZCL_CLUSTER_ID_BASIC) {
            has_basic = true;
        }
        uint8_t f = cluster_flag(c);
        if (f) {
            ESP_LOGI(TAG, "0x%04hx ep %u has server cluster 0x%04x", short_addr, d->ep_id, c);
            cp_dev_add_flags(id, f);
            bind_cluster(short_addr, d->ep_id, c);
        }
    }
    if (has_basic) {
        read_model(short_addr, d->ep_id);
    }
}

static void match_desc_result(const ezb_zdo_match_desc_req_result_t *result, void *user_ctx)
{
    if (result->error != EZB_ERR_NONE || !result->rsp || result->rsp->status != EZB_ZDP_STATUS_SUCCESS) {
        ESP_LOGW(TAG, "Match descriptor request failed (0x%04x)", result->error);
        return;
    }
    for (size_t i = 0; i < result->rsp->match_length && result->rsp->match_list; i++) {
        ezb_zdo_simple_desc_req_t req = {
            .dst_nwk_addr = result->rsp->nwk_addr_of_interest,
            .field =
                {
                    .nwk_addr_of_interest = result->rsp->nwk_addr_of_interest,
                    .endpoint             = result->rsp->match_list[i],
                },
            .cb       = simple_desc_result,
            .user_ctx = NULL,
        };
        ezb_zdo_simple_desc_req(&req);
    }
    if (result->rsp->match_length == 0) {
        ESP_LOGI(TAG, "0x%04hx has no temperature/humidity/thermostat cluster (Tuya EF00 devices are not supported)",
                 result->rsp->nwk_addr_of_interest);
    }
}

static void interview(uint16_t short_addr)
{
    uint16_t clusters[] = {CL_TEMP, CL_HUMIDITY, CL_TSTAT};
    ezb_zdo_match_desc_req_t req = {
        .dst_nwk_addr = short_addr,
        .field =
            {
                .nwk_addr_of_interest = short_addr,
                .profile_id           = EZB_AF_HA_PROFILE_ID,
                .num_in_clusters      = sizeof(clusters) / sizeof(clusters[0]),
                .num_out_clusters     = 0,
                .cluster_list         = clusters,
            },
        .cb       = match_desc_result,
        .user_ctx = NULL,
    };
    if (ezb_zdo_match_desc_req(&req) != EZB_ERR_NONE) {
        ESP_LOGE(TAG, "Match descriptor request to 0x%04hx failed", short_addr);
    }
}

/* --------------------------------------------------------------------------------------------------------------- */
/* ZCL callbacks                                                                                                   */

static void read_attr_rsp_handler(ezb_zcl_cmd_read_attr_rsp_message_t *message)
{
    if (!message || !message->in.header) {
        return;
    }
    uint16_t short_addr = message->in.header->src_addr.u.short_addr;
    uint8_t  ep         = message->in.header->src_ep;
    for (ezb_zcl_read_attr_rsp_variable_t *var = message->in.variables; var; var = var->next) {
        if (var->status != EZB_ZCL_STATUS_SUCCESS) {
            continue;
        }
        if (message->info.cluster_id == EZB_ZCL_CLUSTER_ID_BASIC &&
            var->attr_id == EZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID && var->attr_value) {
            uint64_t       id;
            const uint8_t *s = (const uint8_t *)var->attr_value; /* ZCL string: length byte + characters */
            if (id_for_short(short_addr, &id)) {
                ESP_LOGI(TAG, "0x%04hx model: %.*s", short_addr, s[0], (const char *)(s + 1));
                cp_dev_set_model(id, (const char *)(s + 1), s[0]);
            }
        } else {
            forward_value(short_addr, ep, message->info.cluster_id, var->attr_id, var->attr_type, var->attr_value);
        }
    }
}

static void report_attr_handler(ezb_zcl_cmd_report_attr_message_t *message)
{
    if (!message || !message->in.header) {
        return;
    }
    uint16_t short_addr = message->in.header->src_addr.u.short_addr;
    uint8_t  ep         = message->in.header->src_ep;
    for (ezb_zcl_report_attr_variable_t *var = message->in.variables; var; var = var->next) {
        forward_value(short_addr, ep, message->info.cluster_id, var->attr_id, var->attr_type, var->attr_value);
    }
}

static void config_report_rsp_handler(ezb_zcl_cmd_config_report_rsp_message_t *message)
{
    if (!message) {
        return;
    }
    for (ezb_zcl_config_report_rsp_variable_t *var = message->in.variables; var; var = var->next) {
        if (var->status != EZB_ZCL_STATUS_SUCCESS) {
            ESP_LOGW(TAG, "Reporting of cluster 0x%04x attr 0x%04x not configured (status 0x%02x)",
                     message->info.cluster_id, var->attr_id, var->status);
        }
    }
}

static void zcl_core_action_handler(ezb_zcl_core_action_callback_id_t callback_id, void *message)
{
    switch (callback_id) {
    case EZB_ZCL_CORE_READ_ATTR_RSP_CB_ID:
        read_attr_rsp_handler(message);
        break;
    case EZB_ZCL_CORE_REPORT_ATTR_CB_ID:
        report_attr_handler(message);
        break;
    case EZB_ZCL_CORE_CONFIG_REPORT_RSP_CB_ID:
        config_report_rsp_handler(message);
        break;
    default:
        ESP_LOGD(TAG, "ZCL core action 0x%04" PRIx32, (uint32_t)callback_id);
        break;
    }
}

/* --------------------------------------------------------------------------------------------------------------- */
/* Commands from the S3 (called from the cp_link RX task)                                                          */

static void net_status(cp_net_status_t *st)
{
    st->state   = s_net_up ? "up" : "down";
    st->channel = s_channel;
    st->pan_id  = s_pan_id;
    int64_t left = s_permit_until_us - esp_timer_get_time();
    st->permit_s = left > 0 ? (int)(left / 1000000) : 0;
}

static bool cmd_handler(int argc, char **argv)
{
    const char *cmd = argv[0];

    if (strcmp(cmd, "PJ") == 0) {
        int s = argc > 1 ? atoi(argv[1]) : -1;
        if (argc != 2 || s < 0 || s > 254) {
            cp_link_send_err(cmd, CP_ERR_BAD_ARGS, "usage PJ,<0..254>");
            return true;
        }
        if (!s_net_up) {
            cp_link_send_err(cmd, CP_ERR_NOT_READY, "network not formed");
            return true;
        }
        esp_zigbee_lock_acquire(portMAX_DELAY);
        ezb_err_t ret = s ? ezb_bdb_open_network((uint8_t)s) : ezb_bdb_close_network();
        esp_zigbee_lock_release();
        if (ret == EZB_ERR_NONE) {
            /* The permit-join signal updates the remaining time; set it here too for the immediate NET line. */
            s_permit_until_us = s ? esp_timer_get_time() + (int64_t)s * 1000000 : 0;
            cp_link_send_ok(cmd);
            cp_link_send_net();
        } else {
            cp_link_send_err(cmd, CP_ERR_INTERNAL, "open/close network failed");
        }
        return true;
    }
    if (strcmp(cmd, "FORGET") == 0 && argc == 2) {
        /* Ask the device to leave (without rejoin), then let cp_link remove it from the table. */
        uint64_t id;
        uint16_t addr;
        if (cp_link_parse_id(argv[1], &id) && cp_dev_addr_by_id(id, &addr) && addr != CP_ADDR_UNKNOWN && s_net_up) {
            ezb_zdo_nwk_mgmt_leave_req_t req = {0};
            req.dst_nwk_addr                 = addr;
            req.field.device_addr.u64        = id;
            req.field.remove_children        = false;
            req.field.rejoin                 = false;
            esp_zigbee_lock_acquire(portMAX_DELAY);
            ezb_zdo_nwk_mgmt_leave_req(&req);
            esp_zigbee_lock_release();
        }
        return false;
    }
    if (strcmp(cmd, "RESETNET") == 0) {
        if (argc != 2 || strcmp(argv[1], "CONFIRM") != 0) {
            cp_link_send_err(cmd, CP_ERR_BAD_ARGS, "usage RESETNET,CONFIRM");
            return true;
        }
        cp_dev_clear_all();
        cp_link_send_ok(cmd);
        vTaskDelay(pdMS_TO_TICKS(100));
        esp_zigbee_lock_acquire(portMAX_DELAY);
        esp_zigbee_factory_reset(); /* erases the Zigbee NVRAM and restarts; a new network is formed on boot */
    }
    return false;
}

/* --------------------------------------------------------------------------------------------------------------- */
/* Stack signals (structure as in the SDK thermostat example)                                                      */

static void commissioning_retry_cb(void *arg)
{
    ezb_bdb_start_top_level_commissioning((ezb_bdb_comm_mode_mask_t)(uintptr_t)arg);
}

static void commissioning_retry(void *arg)
{
    /* Called from an esp_timer: hand over to the Zigbee task. */
    esp_zigbee_task_queue_post(commissioning_retry_cb, arg);
}

static void schedule_commissioning(ezb_bdb_comm_mode_mask_t mode, uint32_t delay_ms)
{
    esp_timer_handle_t             timer;
    const esp_timer_create_args_t args = {.callback = commissioning_retry, .arg = (void *)(uintptr_t)mode,
                                          .name = "zb_retry"};
    if (esp_timer_create(&args, &timer) == ESP_OK) {
        esp_timer_start_once(timer, (uint64_t)delay_ms * 1000); /* one-shot; the handle is leaked on purpose (rare) */
    }
}

static bool app_signal_handler(const ezb_app_signal_t *app_signal)
{
    ezb_app_signal_type_t type = ezb_app_signal_get_type(app_signal);

    switch (type) {
    case EZB_ZDO_SIGNAL_SKIP_STARTUP:
        ESP_LOGI(TAG, "Initialize Zigbee stack");
        ezb_bdb_start_top_level_commissioning(EZB_BDB_MODE_INITIALIZATION);
        break;
    case EZB_BDB_SIGNAL_DEVICE_FIRST_START:
    case EZB_BDB_SIGNAL_DEVICE_REBOOT: {
        ezb_bdb_comm_status_t status = *((ezb_bdb_comm_status_t *)ezb_app_signal_get_params(app_signal));
        if (status == EZB_BDB_STATUS_SUCCESS) {
            if (ezb_bdb_is_factory_new()) {
                ESP_LOGI(TAG, "Factory new: forming a network");
                ezb_bdb_start_top_level_commissioning(EZB_BDB_MODE_NETWORK_FORMATION);
            } else {
                ESP_LOGI(TAG, "Network restored: PAN 0x%04hx, channel %d", ezb_nwk_get_panid(),
                         ezb_nwk_get_current_channel());
                s_channel = ezb_nwk_get_current_channel();
                s_pan_id  = ezb_nwk_get_panid();
                s_net_up  = true;
                cp_link_send_net();
            }
        } else {
            ESP_LOGW(TAG, "%s failed (0x%02x), retrying", ezb_app_signal_to_string(type), status);
            schedule_commissioning(EZB_BDB_MODE_INITIALIZATION, 1000);
        }
    } break;
    case EZB_BDB_SIGNAL_FORMATION: {
        ezb_bdb_comm_status_t status = *((ezb_bdb_comm_status_t *)ezb_app_signal_get_params(app_signal));
        if (status == EZB_BDB_STATUS_SUCCESS) {
            ezb_extpanid_t ext;
            ezb_nwk_get_extended_panid(&ext);
            ESP_LOGI(TAG, "Formed network: PAN 0x%04hx, ext 0x%016" PRIx64 ", channel %d", ezb_nwk_get_panid(), ext.u64,
                     ezb_nwk_get_current_channel());
            s_channel = ezb_nwk_get_current_channel();
            s_pan_id  = ezb_nwk_get_panid();
            s_net_up  = true;
            cp_link_send_net();
            /* As in the SDK examples, steering follows formation (it opens the network for 180 s); see the
             * STEERING case. */
            ezb_bdb_start_top_level_commissioning(EZB_BDB_MODE_NETWORK_STEERING);
        } else {
            ESP_LOGW(TAG, "Network formation failed (0x%02x), retrying", status);
            schedule_commissioning(EZB_BDB_MODE_NETWORK_FORMATION, 1000);
        }
    } break;
    case EZB_BDB_SIGNAL_STEERING: {
        ezb_bdb_comm_status_t status = *((ezb_bdb_comm_status_t *)ezb_app_signal_get_params(app_signal));
        ESP_LOGI(TAG, "Network steering %s (0x%02x)", status == EZB_BDB_STATUS_SUCCESS ? "done" : "failed", status);
#if !CONFIG_ZB_COORD_OPEN_AFTER_FORMATION
        /* Joining is opened explicitly by the S3 ("PJ"), not after every formation. */
        ezb_bdb_close_network();
#endif
    } break;
    case EZB_ZDO_SIGNAL_DEVICE_ANNCE: {
        const ezb_zdo_signal_device_annce_params_t *p = ezb_app_signal_get_params(app_signal);
        bool                                        is_new = false;
        char                                        ids[17];
        cp_link_format_id(p->device_addr.u64, ids);
        ESP_LOGI(TAG, "Device announce: %s (0x%04hx)", ids, p->short_addr);
        if (cp_dev_touch(p->device_addr.u64, p->short_addr, &is_new)) {
            cp_link_send("JOIN", "%s,%04x,%d", ids, p->short_addr, is_new ? 1 : 0);
            interview(p->short_addr);
        }
    } break;
    case EZB_ZDO_SIGNAL_LEAVE_INDICATION: {
        const ezb_zdo_signal_leave_indication_params_t *p = ezb_app_signal_get_params(app_signal);
        if (p->leave_type == EZB_ZDO_LEAVE_TYPE_RESET) {
            char ids[17];
            cp_link_format_id(p->device_addr.u64, ids);
            ESP_LOGI(TAG, "Device left: %s (0x%04hx)", ids, p->short_addr);
            cp_link_send("LEAVE", "%s", ids);
        }
    } break;
    case EZB_NWK_SIGNAL_PERMIT_JOIN_STATUS: {
        uint8_t duration = *(uint8_t *)ezb_app_signal_get_params(app_signal);
        ESP_LOGI(TAG, "Permit join: %u s", duration);
        s_permit_until_us = duration ? esp_timer_get_time() + (int64_t)duration * 1000000 : 0;
        cp_link_send_net();
    } break;
    default:
        ESP_LOGI(TAG, "Zigbee signal: %s (0x%02x)", ezb_app_signal_to_string(type), type);
        break;
    }
    return true;
}

/* --------------------------------------------------------------------------------------------------------------- */
/* Device and stack setup                                                                                          */

static esp_err_t create_endpoint(void)
{
    ezb_af_device_desc_t dev_desc = ezb_af_create_device_desc();
    ezb_af_ep_config_t   ep_cfg   = {
             .ep_id              = LOCAL_EP,
             .app_profile_id     = EZB_AF_HA_PROFILE_ID,
             .app_device_id      = EZB_ZHA_COMBINED_INTERFACE_DEVICE_ID,
             .app_device_version = 0,
    };
    ezb_af_ep_desc_t ep_desc = ezb_af_create_endpoint_desc(&ep_cfg);
    ESP_RETURN_ON_FALSE(ep_desc, ESP_FAIL, TAG, "endpoint");

    ezb_zcl_basic_cluster_server_config_t basic_cfg = {
        .zcl_version  = EZB_ZCL_BASIC_ZCL_VERSION_DEFAULT_VALUE,
        .power_source = EZB_ZCL_BASIC_POWER_SOURCE_SINGLE_PHASE_MAINS,
    };
    ezb_zcl_cluster_desc_t basic = ezb_zcl_basic_create_cluster_desc(&basic_cfg, EZB_ZCL_CLUSTER_SERVER);
    ezb_zcl_basic_cluster_desc_add_attr(basic, EZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID, (void *)ESP_MANUFACTURER_NAME);
    ezb_zcl_basic_cluster_desc_add_attr(basic, EZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID, (void *)ESP_MODEL_IDENTIFIER);
    ESP_ERROR_CHECK(ezb_af_endpoint_add_cluster_desc(ep_desc, basic));
    ESP_ERROR_CHECK(ezb_af_endpoint_add_cluster_desc(ep_desc, ezb_zcl_identify_create_cluster_desc(NULL, EZB_ZCL_CLUSTER_SERVER)));

    /* Client clusters: the remote servers bind to these and report to them. */
    ESP_ERROR_CHECK(ezb_af_endpoint_add_cluster_desc(ep_desc, ezb_zcl_basic_create_cluster_desc(NULL, EZB_ZCL_CLUSTER_CLIENT)));
    ESP_ERROR_CHECK(ezb_af_endpoint_add_cluster_desc(
        ep_desc, ezb_zcl_temperature_measurement_create_cluster_desc(NULL, EZB_ZCL_CLUSTER_CLIENT)));
    ESP_ERROR_CHECK(ezb_af_endpoint_add_cluster_desc(
        ep_desc, ezb_zcl_rel_humidity_measurement_create_cluster_desc(NULL, EZB_ZCL_CLUSTER_CLIENT)));
    ESP_ERROR_CHECK(ezb_af_endpoint_add_cluster_desc(ep_desc, ezb_zcl_thermostat_create_cluster_desc(NULL, EZB_ZCL_CLUSTER_CLIENT)));

    ESP_ERROR_CHECK(ezb_af_device_add_endpoint_desc(dev_desc, ep_desc));
    ESP_ERROR_CHECK(ezb_af_device_desc_register(dev_desc));
    ezb_zcl_core_action_handler_register(zcl_core_action_handler);
    return ESP_OK;
}

static void zigbee_task(void *arg)
{
    esp_zigbee_config_t config = {
        .device_config =
            {
                .device_type         = EZB_NWK_DEVICE_TYPE_COORDINATOR,
                .install_code_policy = false,
                .zczr_config         = {.max_children = CONFIG_ZB_COORD_MAX_CHILDREN},
            },
        .platform_config =
            {
                .storage_partition_name = ESP_ZIGBEE_STORAGE_PARTITION_NAME,
                .radio_config           = {.radio_mode = ESP_ZIGBEE_RADIO_MODE_NATIVE},
            },
    };
    ESP_ERROR_CHECK(esp_zigbee_init(&config));

    ezb_aps_secur_enable_distributed_security(false);
    ESP_ERROR_CHECK(ezb_bdb_set_primary_channel_set(1UL << CONFIG_ZB_COORD_CHANNEL));
    ESP_ERROR_CHECK(ezb_bdb_set_secondary_channel_set(CONFIG_ZB_COORD_SECONDARY_CHANNEL_MASK));
    ESP_ERROR_CHECK(ezb_app_signal_add_handler(app_signal_handler));
    ESP_ERROR_CHECK(create_endpoint());

    ESP_ERROR_CHECK(esp_zigbee_start(false));
    esp_zigbee_launch_mainloop();

    esp_zigbee_deinit();
    vTaskDelete(NULL);
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());

    const cp_link_config_t link = {
        .fw_name     = "zigbee",
        .fw_version  = FW_VERSION,
        .cmd_handler = cmd_handler,
        .net_status  = net_status,
    };
    ESP_ERROR_CHECK(cp_link_init(&link));

    ESP_LOGI(TAG, "Start ESP Zigbee stack");
    xTaskCreate(zigbee_task, "Zigbee_main", 6144, NULL, 5, NULL);
}
