#pragma once
#include "driver/i2c_master.h"
i2c_master_dev_handle_t hc_i2c_dev(uint8_t addr);
esp_err_t hc_i2c_write(uint8_t addr, const uint8_t *data, size_t len);
esp_err_t hc_i2c_write_read(uint8_t addr, const uint8_t *w, size_t wlen, uint8_t *r, size_t rlen);
