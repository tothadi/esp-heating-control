/*
 * ESP Heating Control - co-processor link (ESP32-H2 side).
 *
 * Line protocol to the ESP32-S3 over UART0 (H2 TXD0 GPIO24 -> S3 IO11, H2 RXD0 GPIO23 <- S3 IO10), 115200 8N1.
 * Every line is "$CP,<TYPE>[,<field>...]*<XX>\r\n", XX = XOR of all characters between '$' and '*' as two hex
 * digits. Anything else on the line (e.g. ROM boot messages on UART0) is ignored. Full specification:
 * docs/coprocessor-protocol.md. Kept apart from the Zigbee code in main/ so the protocol stays in one place.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CP_PROTO_VERSION 1
#define CP_LINE_MAX      192 /* including "$CP," and "*XX" */
#define CP_MAX_FIELDS    10

/* Error codes of "$CP,ERR,<cmd>,<code>,<text>" */
enum {
    CP_ERR_CHECKSUM    = 1,
    CP_ERR_UNKNOWN_CMD = 2,
    CP_ERR_BAD_ARGS    = 3,
    CP_ERR_NOT_FOUND   = 4,
    CP_ERR_NOT_READY   = 5,
    CP_ERR_TABLE_FULL  = 6,
    CP_ERR_UNSUPPORTED = 7,
    CP_ERR_INTERNAL    = 8,
};

/*
 * Firmware-specific command handler. argv[0] is the command (e.g. "PJ"), argv[1..] its fields. Return true if the
 * command was handled (the handler sends OK/ERR itself), false if it is unknown (cp_link answers ERR 2).
 * Called from the cp_link RX task, not from the radio stack task: take the stack lock before calling stack APIs.
 */
typedef bool (*cp_cmd_handler_t)(int argc, char **argv);

/* Network status reported in "$CP,NET,...". Filled by the firmware callback. */
typedef struct {
    const char *state;   /* "down", "forming", "up" */
    int         channel; /* 11..26, 0 if unknown */
    uint16_t    pan_id;
    int         permit_s; /* remaining permit-join / commissioning time in seconds, 0 = closed */
} cp_net_status_t;

typedef void (*cp_net_status_cb_t)(cp_net_status_t *out);

typedef struct {
    const char        *fw_name;    /* "zigbee" (no commas) */
    const char        *fw_version; /* e.g. "0.1.0" */
    cp_cmd_handler_t   cmd_handler;
    cp_net_status_cb_t net_status;
} cp_link_config_t;

/* Install the UART driver, load the device table from NVS, start the RX task and send HELLO. nvs_flash_init() must
 * have been called. */
esp_err_t cp_link_init(const cp_link_config_t *config);

/* Send one line: type + printf-style comma separated fields (fmt may be NULL). Thread safe. */
void cp_link_send(const char *type, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

void cp_link_send_ok(const char *cmd);
void cp_link_send_err(const char *cmd, int code, const char *text);

/* Send "$CP,NET,..." with the current status from the net_status callback. */
void cp_link_send_net(void);

/* Copy src to dst (size bytes incl. NUL), replacing characters that are not allowed in a protocol field with '_'. */
void cp_link_sanitize(char *dst, size_t size, const char *src, size_t src_len);

/* "%016llx" helpers for 64-bit device ids */
void cp_link_format_id(uint64_t id, char out[17]);
bool cp_link_parse_id(const char *s, uint64_t *id);

#ifdef __cplusplus
}
#endif
