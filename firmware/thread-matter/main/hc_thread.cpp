// Thread border router (OpenThread BR in ESP-IDF, as used by esp-thread-br) with the ESP32-H2 as radio
// co-processor over UART1: S3 IO10 TX -> H2 RXD0 (GPIO23), S3 IO11 RX <- H2 TXD0 (GPIO24), 460800 baud.
// RCP reset / download mode: S3 IO12 -> H2 EN, S3 IO13 -> H2 BOOT (GPIO9), used by esp_rcp_update, which compares
// the running RCP version with the image packed into the rcp_fw partition and reflashes the H2 if they differ.
#include <string.h>
#include "hc_thread.h"
#include "hc_board.h"
#include "hc_net.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_openthread.h"
#include "esp_openthread_border_router.h"
#include "esp_openthread_lock.h"
#include "esp_openthread_types.h"
#include "esp_rcp_update.h"
#include "esp_ot_rcp_update.h"
#include "esp_spiffs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <esp_matter.h>
#include <openthread/dataset.h>
#include <openthread/dataset_ftd.h>
#include <openthread/instance.h>
#include <openthread/ip6.h>
#include <openthread/thread.h>
#include <platform/CHIPDeviceLayer.h>
#include <platform/ESP32/OpenthreadLauncher.h>
#include <platform/ThreadStackManager.h>
#if CONFIG_ESP_COEX_EXTERNAL_COEXIST_ENABLE
#include "esp_coexist.h"
#endif

static const char *TAG = "hc_thread";
static QueueHandle_t s_queue;
static bool s_br_init;
static esp_netif_t *s_backbone;
static bool s_rcp_update_ok;

esp_err_t hc_thread_pre_start(void)
{
    esp_vfs_spiffs_conf_t fw = {.base_path = "/rcp_fw", .partition_label = "rcp_fw", .max_files = 10,
                                .format_if_mount_failed = false};
    if (esp_vfs_spiffs_register(&fw) == ESP_OK) {
        esp_rcp_update_config_t cfg = {};
        cfg.rcp_type = RCP_TYPE_UART;
        cfg.uart_rx_pin = HC_PIN_H2_RX;
        cfg.uart_tx_pin = HC_PIN_H2_TX;
        cfg.uart_port = 1;
        cfg.uart_baudrate = 115200;      // H2 ROM loader
        cfg.reset_pin = HC_PIN_H2_EN;
        cfg.boot_pin = HC_PIN_H2_BOOT;
        cfg.update_baudrate = 460800;
        strlcpy(cfg.firmware_dir, "/rcp_fw/ot_rcp", sizeof(cfg.firmware_dir));
        cfg.target_chip = ESP32H2_CHIP;
        s_rcp_update_ok = esp_rcp_update_init(&cfg) == ESP_OK;
        if (s_rcp_update_ok) esp_ot_register_rcp_handler();
    } else {
        ESP_LOGE(TAG, "rcp_fw partition not mountable: RCP auto-update disabled");
    }

    static esp_openthread_platform_config_t config = {};
    config.radio_config.radio_mode = RADIO_MODE_UART_RCP;
    config.radio_config.radio_uart_config.port = UART_NUM_1;
    config.radio_config.radio_uart_config.uart_config.baud_rate = 460800;
    config.radio_config.radio_uart_config.uart_config.data_bits = UART_DATA_8_BITS;
    config.radio_config.radio_uart_config.uart_config.parity = UART_PARITY_DISABLE;
    config.radio_config.radio_uart_config.uart_config.stop_bits = UART_STOP_BITS_1;
    config.radio_config.radio_uart_config.uart_config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    config.radio_config.radio_uart_config.uart_config.source_clk = UART_SCLK_DEFAULT;
    config.radio_config.radio_uart_config.rx_pin = HC_PIN_H2_RX;
    config.radio_config.radio_uart_config.tx_pin = HC_PIN_H2_TX;
    config.host_config.host_connection_mode = HOST_CONNECTION_MODE_NONE;
    config.port_config.storage_partition_name = "nvs";
    config.port_config.netif_queue_size = 10;
    config.port_config.task_queue_size = 10;
    set_openthread_platform_config(&config);
    return ESP_OK;
}

// Forms a network on first boot (random credentials, stored by OpenThread in NVS) and starts Thread. The Thread
// network runs whether or not a backbone is available: the sensors keep reporting to the controller.
static void thread_up(void)
{
    otInstance *ot = esp_openthread_get_instance();
    if (!otDatasetIsCommissioned(ot)) {
        otOperationalDataset ds;
        if (otDatasetCreateNewNetwork(ot, &ds) == OT_ERROR_NONE) {
            const char *name = "HeatingCtl";
            memset(&ds.mNetworkName, 0, sizeof(ds.mNetworkName));
            memcpy(ds.mNetworkName.m8, name, strlen(name));
            otDatasetSetActive(ot, &ds);
            ESP_LOGI(TAG, "New Thread network formed (channel %u, PAN 0x%04x)", ds.mChannel, ds.mPanId);
        }
    }
    otIp6SetEnabled(ot, true);
    otThreadSetEnabled(ot, true);
}

static void set_backbone(esp_netif_t *nif)
{
    if (!nif || nif == s_backbone) return;
    esp_openthread_lock_acquire(portMAX_DELAY);
    if (s_br_init) {
        esp_openthread_border_router_deinit();
        s_br_init = false;
    }
    esp_openthread_set_backbone_netif(nif);
    esp_err_t err = esp_openthread_border_router_init();
    esp_openthread_lock_release();
    if (err == ESP_OK) {
        s_br_init = true;
        s_backbone = nif;
        esp_openthread_set_meshcop_instance_name("Heating controller");
        ESP_LOGI(TAG, "Border router on backbone %s", esp_netif_get_ifkey(nif));
    } else {
        ESP_LOGE(TAG, "Border router init failed: %s", esp_err_to_name(err));
    }
}

static void net_changed(esp_netif_t *active) { xQueueOverwrite(s_queue, &active); }

static void br_task(void *arg)
{
    esp_netif_t *nif;
    for (;;)
        if (xQueueReceive(s_queue, &nif, portMAX_DELAY) == pdTRUE) set_backbone(nif);
}

esp_err_t hc_thread_post_start(void)
{
#if CONFIG_ESP_COEX_EXTERNAL_COEXIST_ENABLE
    // PTA, S3 = leader: REQUEST IO14 / PRIORITY IO21 from the H2, GRANT IO47 / TX_LINE IO48 to the H2.
    esp_external_coex_gpio_set_t pta = {};
    pta.request = HC_PIN_PTA_REQUEST;
    pta.priority = HC_PIN_PTA_PRIORITY;
    pta.grant = HC_PIN_PTA_GRANT;
    pta.tx_line = HC_PIN_PTA_TX_LINE;
    ESP_ERROR_CHECK(esp_external_coex_set_work_mode(EXTERNAL_COEX_LEADER_ROLE));
    ESP_ERROR_CHECK(esp_enable_extern_coex_gpio_pin(EXTERN_COEX_WIRE_4, pta));
#endif
    if (s_rcp_update_ok) {
        esp_matter::lock::ScopedChipStackLock lock(portMAX_DELAY);
        using namespace chip::DeviceLayer;
        const bool was = ThreadStackMgr().IsThreadEnabled();
        if (was) ThreadStackMgr().SetThreadEnabled(false);
        esp_ot_update_rcp_if_different();
        if (was) ThreadStackMgr().SetThreadEnabled(true);
    }
    esp_openthread_lock_acquire(portMAX_DELAY);
    thread_up();
    esp_openthread_lock_release();

    s_queue = xQueueCreate(1, sizeof(esp_netif_t *));
    xTaskCreate(br_task, "hc_br", 4096, nullptr, 5, nullptr);
    hc_net_register_cb(net_changed);
    if (esp_netif_t *a = hc_net_active()) net_changed(a);
    return ESP_OK;
}

bool hc_thread_get_dataset(uint8_t *tlvs, uint8_t *len)
{
    otOperationalDatasetTlvs ds;
    esp_openthread_lock_acquire(portMAX_DELAY);
    const otError err = otDatasetGetActiveTlvs(esp_openthread_get_instance(), &ds);
    esp_openthread_lock_release();
    if (err != OT_ERROR_NONE) return false;
    memcpy(tlvs, ds.mTlvs, ds.mLength);
    *len = ds.mLength;
    return true;
}

cJSON *hc_thread_to_json(void)
{
    cJSON *o = cJSON_CreateObject();
    esp_openthread_lock_acquire(portMAX_DELAY);
    otInstance *ot = esp_openthread_get_instance();
    otOperationalDataset ds;
    cJSON_AddStringToObject(o, "role", otThreadDeviceRoleToString(otThreadGetDeviceRole(ot)));
    if (otDatasetGetActive(ot, &ds) == OT_ERROR_NONE) {
        char buf[20];
        cJSON_AddStringToObject(o, "network_name", ds.mNetworkName.m8);
        cJSON_AddNumberToObject(o, "channel", ds.mChannel);
        snprintf(buf, sizeof(buf), "0x%04x", ds.mPanId);
        cJSON_AddStringToObject(o, "pan_id", buf);
        for (int i = 0; i < 8; i++) snprintf(buf + 2 * i, 3, "%02x", ds.mExtendedPanId.m8[i]);
        cJSON_AddStringToObject(o, "ext_pan_id", buf);
    }
    esp_openthread_lock_release();
    cJSON_AddBoolToObject(o, "border_router", s_br_init);
    cJSON_AddStringToObject(o, "backbone", s_backbone ? esp_netif_get_ifkey(s_backbone) : "none");
    return o;
}
