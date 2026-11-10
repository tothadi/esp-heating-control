// Network selection (see hc_net.h). Ethernet: WIZnet W5500 on SPI2 (SCK IO39, MOSI IO40, MISO IO41, CS IO42,
// INT IO38, no reset line). The Ethernet netif gets route priority 200, above the Wi-Fi station's 100, so in
// "auto" lwIP's default netif is Ethernet whenever its link is up and it has an address, and Wi-Fi otherwise.
// The Wi-Fi station itself is owned by the Matter stack (CHIP's ConnectivityManager connects/reconnects it); this
// file only gives it credentials and enables or disables station mode.
#include <string.h>
#include <strings.h>
#include "hc_net.h"
#include "hc.h"
#include "hc_board.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_eth.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "nvs.h"
#include <esp_matter.h>
#include <platform/CHIPDeviceLayer.h>

static const char *TAG = "hc_net";

static hc_net_mode_t s_mode = HC_NET_AUTO;
static char s_ssid[33], s_pass[65];
static esp_eth_handle_t s_eth;
static esp_netif_t *s_eth_netif, *s_sta_netif, *s_active;
static bool s_eth_started, s_eth_link, s_eth_ip, s_sta_ip;
static hc_net_cb_t s_cbs[4];

static void save(void)
{
    nvs_handle_t h;
    if (nvs_open("hcnet", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, "mode", (uint8_t) s_mode);
    nvs_set_str(h, "ssid", s_ssid);
    nvs_set_str(h, "pass", s_pass);
    nvs_commit(h);
    nvs_close(h);
}

static void load(void)
{
    nvs_handle_t h;
    if (nvs_open("hcnet", NVS_READONLY, &h) != ESP_OK) return;
    uint8_t m;
    if (nvs_get_u8(h, "mode", &m) == ESP_OK && m <= HC_NET_WIFI) s_mode = (hc_net_mode_t) m;
    size_t l = sizeof(s_ssid);
    if (nvs_get_str(h, "ssid", s_ssid, &l) != ESP_OK) s_ssid[0] = 0;
    l = sizeof(s_pass);
    if (nvs_get_str(h, "pass", s_pass, &l) != ESP_OK) s_pass[0] = 0;
    nvs_close(h);
}

static void reevaluate(void)
{
    esp_netif_t *a = nullptr;
    if (s_mode != HC_NET_WIFI && s_eth_link && s_eth_ip) a = s_eth_netif;
    else if (s_mode != HC_NET_ETHERNET && s_sta_ip) a = s_sta_netif;
    char txt[32] = "offline";
    if (a) {
        esp_netif_ip_info_t ip;
        esp_netif_get_ip_info(a, &ip);
        snprintf(txt, sizeof(txt), "%s " IPSTR, a == s_eth_netif ? "eth" : "wifi", IP2STR(&ip.ip));
    }
    hc_set_network_status(txt);
    if (a == s_active) return;
    s_active = a;
    if (a) esp_netif_set_default_netif(a);
    ESP_LOGI(TAG, "Active network: %s", txt);
    for (auto cb : s_cbs)
        if (cb) cb(a);
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == ETH_EVENT) {
        if (id == ETHERNET_EVENT_CONNECTED) {
            s_eth_link = true;
            esp_netif_create_ip6_linklocal(s_eth_netif);
            ESP_LOGI(TAG, "Ethernet link up");
        } else if (id == ETHERNET_EVENT_DISCONNECTED || id == ETHERNET_EVENT_STOP) {
            s_eth_link = false;
            s_eth_ip = false;
            ESP_LOGI(TAG, "Ethernet link down");
        }
    } else if (base == IP_EVENT) {
        if (id == IP_EVENT_ETH_GOT_IP) s_eth_ip = true;
        else if (id == IP_EVENT_ETH_LOST_IP) s_eth_ip = false;
        else if (id == IP_EVENT_STA_GOT_IP) s_sta_ip = true;
        else if (id == IP_EVENT_STA_LOST_IP) s_sta_ip = false;
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_sta_ip = false;
    }
    reevaluate();
}

static esp_err_t eth_init(void)
{
    gpio_install_isr_service(0);   // may already be installed
    spi_bus_config_t bus = {};
    bus.mosi_io_num = HC_PIN_ETH_MOSI;
    bus.miso_io_num = HC_PIN_ETH_MISO;
    bus.sclk_io_num = HC_PIN_ETH_SCK;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO), TAG, "SPI bus");
    spi_device_interface_config_t dev = {};
    dev.mode = 0;
    dev.clock_speed_hz = 20 * 1000 * 1000;   // through the FFC to the power/IO board
    dev.queue_size = 20;
    dev.spics_io_num = HC_PIN_ETH_CS;
    eth_w5500_config_t w5500 = ETH_W5500_DEFAULT_CONFIG(SPI2_HOST, &dev);
    w5500.int_gpio_num = HC_PIN_ETH_INT;
    eth_mac_config_t mac_cfg = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_cfg = ETH_PHY_DEFAULT_CONFIG();
    phy_cfg.reset_gpio_num = -1;   // RC power-on reset only
    esp_eth_mac_t *mac = esp_eth_mac_new_w5500(&w5500, &mac_cfg);
    esp_eth_phy_t *phy = esp_eth_phy_new_w5500(&phy_cfg);
    esp_eth_config_t cfg = ETH_DEFAULT_CONFIG(mac, phy);
    ESP_RETURN_ON_ERROR(esp_eth_driver_install(&cfg, &s_eth), TAG, "W5500 not responding");
    uint8_t addr[6];
    esp_read_mac(addr, ESP_MAC_ETH);
    esp_eth_ioctl(s_eth, ETH_CMD_S_MAC_ADDR, addr);

    esp_netif_inherent_config_t base = ESP_NETIF_INHERENT_DEFAULT_ETH();
    base.route_prio = 200;   // above Wi-Fi STA (100): Ethernet wins while it is up
    esp_netif_config_t ncfg = {.base = &base, .driver = nullptr, .stack = ESP_NETIF_NETSTACK_DEFAULT_ETH};
    s_eth_netif = esp_netif_new(&ncfg);
    return esp_netif_attach(s_eth_netif, esp_eth_new_netif_glue(s_eth));
}

static void apply_mode(void)
{
    // Ethernet
    if (s_eth) {
        if (s_mode == HC_NET_WIFI && s_eth_started) { esp_eth_stop(s_eth); s_eth_started = false; }
        else if (s_mode != HC_NET_WIFI && !s_eth_started) s_eth_started = esp_eth_start(s_eth) == ESP_OK;
    }
    // Wi-Fi station (CHIP owns the station state machine)
    const bool sta = s_mode != HC_NET_ETHERNET && s_ssid[0];
    if (s_ssid[0]) {
        wifi_config_t wc = {};
        strlcpy((char *) wc.sta.ssid, s_ssid, sizeof(wc.sta.ssid));
        strlcpy((char *) wc.sta.password, s_pass, sizeof(wc.sta.password));
        esp_wifi_set_config(WIFI_IF_STA, &wc);
    }
    {
        esp_matter::lock::ScopedChipStackLock lock(portMAX_DELAY);
        chip::DeviceLayer::ConnectivityMgr().SetWiFiStationMode(
            sta ? chip::DeviceLayer::ConnectivityManager::kWiFiStationMode_Enabled
                : chip::DeviceLayer::ConnectivityManager::kWiFiStationMode_Disabled);
    }
    if (sta) esp_wifi_connect();
    else esp_wifi_disconnect();
    reevaluate();
}

esp_err_t hc_net_start(void)
{
    load();
    s_sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, on_event, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, on_event, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, on_event, nullptr));
    if (eth_init() != ESP_OK) {
        ESP_LOGE(TAG, "Ethernet (W5500) not available; Wi-Fi only");
        s_eth = nullptr;
    }
    ESP_LOGI(TAG, "Network mode: %s", hc_net_mode_name(s_mode));
    apply_mode();
    return ESP_OK;
}

esp_err_t hc_net_set_mode(hc_net_mode_t mode)
{
    if (mode > HC_NET_WIFI) return ESP_ERR_INVALID_ARG;
    s_mode = mode;
    save();
    apply_mode();
    return ESP_OK;
}

esp_err_t hc_net_set_wifi(const char *ssid, const char *password)
{
    if (!ssid || strlen(ssid) >= sizeof(s_ssid) || (password && strlen(password) >= sizeof(s_pass)))
        return ESP_ERR_INVALID_ARG;
    strlcpy(s_ssid, ssid, sizeof(s_ssid));
    strlcpy(s_pass, password ? password : "", sizeof(s_pass));
    save();
    esp_wifi_disconnect();
    apply_mode();
    return ESP_OK;
}

bool hc_net_get_wifi(char *ssid, size_t ssid_len, char *password, size_t pw_len)
{
    strlcpy(ssid, s_ssid, ssid_len);
    strlcpy(password, s_pass, pw_len);
    return s_ssid[0] != 0;
}

hc_net_mode_t hc_net_get_mode(void) { return s_mode; }
esp_netif_t *hc_net_active(void) { return s_active; }

const char *hc_net_mode_name(hc_net_mode_t m)
{
    return m == HC_NET_ETHERNET ? "ethernet" : m == HC_NET_WIFI ? "wifi" : "auto";
}

bool hc_net_parse_mode(const char *s, hc_net_mode_t *m)
{
    for (int i = 0; i <= HC_NET_WIFI; i++)
        if (s && !strcasecmp(s, hc_net_mode_name((hc_net_mode_t) i))) { *m = (hc_net_mode_t) i; return true; }
    return false;
}

void hc_net_register_cb(hc_net_cb_t cb)
{
    for (auto &c : s_cbs)
        if (!c) { c = cb; return; }
}

cJSON *hc_net_to_json(void)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "mode", hc_net_mode_name(s_mode));
    cJSON_AddStringToObject(o, "active", s_active == nullptr ? "none" : s_active == s_eth_netif ? "ethernet" : "wifi");
    cJSON_AddBoolToObject(o, "ethernet_present", s_eth != nullptr);
    cJSON_AddBoolToObject(o, "ethernet_link", s_eth_link);
    cJSON_AddBoolToObject(o, "wifi_connected", s_sta_ip);
    cJSON_AddStringToObject(o, "wifi_ssid", s_ssid);
    for (int k = 0; k < 2; k++) {
        esp_netif_t *n = k == 0 ? s_eth_netif : s_sta_netif;
        esp_netif_ip_info_t ip;
        if (!n || esp_netif_get_ip_info(n, &ip) != ESP_OK || ip.ip.addr == 0) continue;
        char b[16];
        snprintf(b, sizeof(b), IPSTR, IP2STR(&ip.ip));
        cJSON_AddStringToObject(o, k == 0 ? "ethernet_ip" : "wifi_ip", b);
    }
    return o;
}
