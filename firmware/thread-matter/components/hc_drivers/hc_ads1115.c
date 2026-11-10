// ADS1115 single-shot conversions, +/-4.096 V full scale, 860 SPS, comparator off.
#include "hc_drivers.h"
#include "hc_i2c_priv.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

esp_err_t hc_ads_read(uint8_t addr, uint8_t channel, float *volts)
{
    // OS=1 | MUX=1xx (AINx vs GND) | PGA=001 (4.096 V) | MODE=1 (single) ; DR=111 (860 SPS) | COMP_QUE=11
    const uint16_t cfg = 0x8000 | ((0x4 | (channel & 3)) << 12) | (0x1 << 9) | 0x0100 | (0x7 << 5) | 0x0003;
    uint8_t w[3] = {0x01, cfg >> 8, cfg & 0xFF};
    esp_err_t err = hc_i2c_write(addr, w, 3);
    if (err != ESP_OK) return err;
    uint8_t reg = 0x01, r[2];
    for (int i = 0; i < 10; i++) {
        vTaskDelay(pdMS_TO_TICKS(2));
        err = hc_i2c_write_read(addr, &reg, 1, r, 2);
        if (err != ESP_OK) return err;
        if (r[0] & 0x80) break;   // conversion done
        if (i == 9) return ESP_ERR_TIMEOUT;
    }
    reg = 0x00;
    err = hc_i2c_write_read(addr, &reg, 1, r, 2);
    if (err != ESP_OK) return err;
    *volts = (int16_t) ((r[0] << 8) | r[1]) * (4.096f / 32768.0f);
    return ESP_OK;
}
