/*
 * ESP Heating Control - co-processor device table.
 *
 * Devices are keyed by their 64-bit Zigbee IEEE address. Id, short address, capability flags, name and model are kept in NVS (namespace "cp");
 * the last readings are kept in RAM only. All functions are thread safe.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Capability flags (reported as letters in "$CP,DEV,...") */
#define CP_DEV_F_TEMP     0x01 /* T: Temperature Measurement 0x0402 */
#define CP_DEV_F_HUMIDITY 0x02 /* H: Relative Humidity 0x0405 */
#define CP_DEV_F_TSTAT    0x04 /* S: Thermostat 0x0201 */

#define CP_NAME_MAX  20 /* characters, without NUL */
#define CP_MODEL_MAX 32

#define CP_ADDR_UNKNOWN 0xFFFF

esp_err_t cp_dev_init(void);

/* Create the device if it is unknown, update its short address and last-seen time. Returns false if the table is
 * full. *is_new (may be NULL) is set if the device was created. */
bool cp_dev_touch(uint64_t id, uint16_t addr, bool *is_new);

bool cp_dev_id_by_addr(uint16_t addr, uint64_t *id);
bool cp_dev_addr_by_id(uint64_t id, uint16_t *addr);
void cp_dev_set_model(uint64_t id, const char *model, size_t len);
void cp_dev_add_flags(uint64_t id, uint8_t flags);

/* Returns 0 or a CP_ERR_* code */
int cp_dev_set_name(uint64_t id, const char *name);
int cp_dev_forget(uint64_t id);
void cp_dev_clear_all(void);

/* Store a reading and send "$CP,RD,..." */
void cp_dev_reading(uint64_t id, uint8_t ep, uint16_t cluster, uint16_t attr, float value);

/* "$CP,DEV,..." for every device, then "$CP,DEVEND,<count>" */
void cp_dev_send_list(void);
/* "$CP,RD,..." with the age of every stored reading, then "$CP,DUMPEND,<count>" */
void cp_dev_send_dump(void);

#ifdef __cplusplus
}
#endif
