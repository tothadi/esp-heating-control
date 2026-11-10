/*
 * ESP Heating Control - co-processor link (ESP32-H2 side). See cp_link.h and docs/coprocessor-protocol.md.
 */
#include <ctype.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "cp_devtable.h"
#include "cp_link.h"

static const char *TAG = "cp_link";

#define CP_UART UART_NUM_0

static cp_link_config_t  s_cfg;
static SemaphoreHandle_t s_tx_mutex;

static uint8_t checksum(const char *s, size_t len)
{
    uint8_t cs = 0;
    for (size_t i = 0; i < len; i++) {
        cs ^= (uint8_t)s[i];
    }
    return cs;
}

static void send_raw(const char *body, size_t len)
{
    char tail[6];
    snprintf(tail, sizeof(tail), "*%02X\r\n", checksum(body, len));
    xSemaphoreTake(s_tx_mutex, portMAX_DELAY);
    uart_write_bytes(CP_UART, "$", 1);
    uart_write_bytes(CP_UART, body, len);
    uart_write_bytes(CP_UART, tail, strlen(tail));
    xSemaphoreGive(s_tx_mutex);
    ESP_LOGD(TAG, "TX $%.*s", (int)len, body);
}

void cp_link_send(const char *type, const char *fmt, ...)
{
    char   body[CP_LINE_MAX];
    size_t max = CP_LINE_MAX - 6; /* room for '$', "*XX\r\n" */
    int    n   = snprintf(body, max, "CP,%s", type);
    if (fmt && n > 0 && (size_t)n < max - 1) {
        body[n++] = ',';
        va_list ap;
        va_start(ap, fmt);
        int m = vsnprintf(body + n, max - n, fmt, ap);
        va_end(ap);
        n = (m < 0) ? n : n + m;
    }
    if (n < 0) {
        return;
    }
    if ((size_t)n >= max) {
        ESP_LOGW(TAG, "Line truncated: %s", body);
        n = max - 1;
    }
    send_raw(body, n);
}

void cp_link_send_ok(const char *cmd) { cp_link_send("OK", "%s", cmd); }

void cp_link_send_err(const char *cmd, int code, const char *text) { cp_link_send("ERR", "%s,%d,%s", cmd, code, text); }

void cp_link_send_net(void)
{
    cp_net_status_t st = {.state = "down"};
    if (s_cfg.net_status) {
        s_cfg.net_status(&st);
    }
    cp_link_send("NET", "%s,%d,%04x,%d", st.state, st.channel, st.pan_id, st.permit_s);
}

static bool field_char_ok(char c) { return isalnum((unsigned char)c) || strchr(" _.-+/():", c) != NULL; }

void cp_link_sanitize(char *dst, size_t size, const char *src, size_t src_len)
{
    size_t i = 0;
    for (; i < src_len && i + 1 < size && src[i]; i++) {
        dst[i] = field_char_ok(src[i]) ? src[i] : '_';
    }
    dst[i] = '\0';
}

void cp_link_format_id(uint64_t id, char out[17]) { snprintf(out, 17, "%016" PRIx64, id); }

bool cp_link_parse_id(const char *s, uint64_t *id)
{
    if (strlen(s) != 16) {
        return false;
    }
    char *end = NULL;
    *id       = strtoull(s, &end, 16);
    return end && *end == '\0';
}

/* --- Generic commands (ping, network status, device table) ------------------------------------------- */

static bool name_ok(const char *name)
{
    size_t len = strlen(name);
    if (len == 0 || len > CP_NAME_MAX) {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        if (!isalnum((unsigned char)name[i]) && !strchr(" _.-", name[i])) {
            return false;
        }
    }
    return true;
}

static void handle_command(int argc, char **argv)
{
    const char *cmd = argv[0];

    if (strcmp(cmd, "PING") == 0) {
        cp_link_send("PONG", "%s,%s,%s,%d,%" PRIu32, argc > 1 ? argv[1] : "", s_cfg.fw_name, s_cfg.fw_version,
                     CP_PROTO_VERSION, (uint32_t)(esp_timer_get_time() / 1000000));
    } else if (strcmp(cmd, "NET") == 0) {
        cp_link_send_net();
    } else if (strcmp(cmd, "LIST") == 0) {
        cp_dev_send_list();
    } else if (strcmp(cmd, "DUMP") == 0) {
        cp_dev_send_dump();
    } else if (strcmp(cmd, "NAME") == 0) {
        uint64_t id;
        if (argc != 3 || !cp_link_parse_id(argv[1], &id) || !name_ok(argv[2])) {
            cp_link_send_err(cmd, CP_ERR_BAD_ARGS, "usage NAME,<id>,<name>");
            return;
        }
        int err = cp_dev_set_name(id, argv[2]);
        if (err) {
            cp_link_send_err(cmd, err, "unknown device");
        } else {
            cp_link_send_ok(cmd);
        }
    } else if (strcmp(cmd, "FORGET") == 0) {
        uint64_t id;
        if (argc != 2 || !cp_link_parse_id(argv[1], &id)) {
            cp_link_send_err(cmd, CP_ERR_BAD_ARGS, "usage FORGET,<id>");
            return;
        }
        /* Give the firmware a chance to act on the network first (e.g. Zigbee leave request). */
        if (s_cfg.cmd_handler && s_cfg.cmd_handler(argc, argv)) {
            return;
        }
        int err = cp_dev_forget(id);
        if (err) {
            cp_link_send_err(cmd, err, "unknown device");
        } else {
            cp_link_send_ok(cmd);
        }
    } else if (!s_cfg.cmd_handler || !s_cfg.cmd_handler(argc, argv)) {
        cp_link_send_err(cmd, CP_ERR_UNKNOWN_CMD, "unknown command");
    }
}

/* One received line, without CR/LF. Garbage before the last '$' is skipped (ROM output, line noise). */
static void process_line(char *line)
{
    char *start = strrchr(line, '$');
    if (!start || strncmp(start, "$CP,", 4) != 0) {
        return;
    }
    char *star = strrchr(start, '*');
    if (!star || strlen(star) != 3 || !isxdigit((unsigned char)star[1]) || !isxdigit((unsigned char)star[2])) {
        return;
    }
    unsigned rx_cs = (unsigned)strtoul(star + 1, NULL, 16);
    if (rx_cs != checksum(start + 1, star - start - 1)) {
        ESP_LOGW(TAG, "Checksum error: %s", start);
        cp_link_send_err("?", CP_ERR_CHECKSUM, "checksum");
        return;
    }
    *star = '\0';
    ESP_LOGD(TAG, "RX %s", start);

    char *argv[CP_MAX_FIELDS];
    int   argc = 0;
    char *p    = start + 4; /* after "$CP," */
    while (p && argc < CP_MAX_FIELDS) {
        argv[argc++] = p;
        p            = strchr(p, ',');
        if (p) {
            *p++ = '\0';
        }
    }
    if (argc == 0 || argv[0][0] == '\0') {
        return;
    }
    handle_command(argc, argv);
}

static void rx_task(void *arg)
{
    static char line[CP_LINE_MAX + 1];
    size_t      len      = 0;
    bool        overflow = false;
    uint8_t     buf[64];

    for (;;) {
        int n = uart_read_bytes(CP_UART, buf, sizeof(buf), pdMS_TO_TICKS(100));
        for (int i = 0; i < n; i++) {
            char c = (char)buf[i];
            if (c == '\n' || c == '\r') {
                if (!overflow && len > 0) {
                    line[len] = '\0';
                    process_line(line);
                }
                len      = 0;
                overflow = false;
            } else if (len < CP_LINE_MAX) {
                line[len++] = c;
            } else {
                overflow = true; /* drop the rest of an over-long line */
            }
        }
    }
}

esp_err_t cp_link_init(const cp_link_config_t *config)
{
    s_cfg      = *config;
    s_tx_mutex = xSemaphoreCreateMutex();
    if (!s_tx_mutex) {
        return ESP_ERR_NO_MEM;
    }

    const uart_config_t uart_config = {
        .baud_rate  = CONFIG_CP_LINK_BAUD_RATE,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(CP_UART, 1024, 2048, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(CP_UART, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(CP_UART, CONFIG_CP_LINK_TX_GPIO, CONFIG_CP_LINK_RX_GPIO, UART_PIN_NO_CHANGE,
                                 UART_PIN_NO_CHANGE));

    ESP_ERROR_CHECK(cp_dev_init());

    if (xTaskCreate(rx_task, "cp_link_rx", 4096, NULL, 4, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    /* Leading CR/LF terminates whatever the ROM printed on UART0 before the app started. */
    uart_write_bytes(CP_UART, "\r\n", 2);
    cp_link_send("HELLO", "%s,%s,%d", s_cfg.fw_name, s_cfg.fw_version, CP_PROTO_VERSION);
    ESP_LOGI(TAG, "Co-processor link on UART0 (TX GPIO%d, RX GPIO%d, %d baud)", CONFIG_CP_LINK_TX_GPIO,
             CONFIG_CP_LINK_RX_GPIO, CONFIG_CP_LINK_BAUD_RATE);
    return ESP_OK;
}
