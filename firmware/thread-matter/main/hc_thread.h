#pragma once
// Thread border router on the S3 with the H2 as RCP: platform config, RCP auto-update, PTA, network formation and
// the backbone (Ethernet or Wi-Fi, from hc_net).
#include <stdint.h>
#include "cJSON.h"
#include "esp_err.h"

esp_err_t hc_thread_pre_start(void);    // before esp_matter::start()
esp_err_t hc_thread_post_start(void);   // after esp_matter::start() and hc_net_start()
bool hc_thread_get_dataset(uint8_t *tlvs, uint8_t *len);   // active operational dataset (TLVs), max 254 bytes
cJSON *hc_thread_to_json(void);
