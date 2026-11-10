#pragma once
// ESP Heating Control - drivers for the I2C devices of the controller (all calls are thread safe).
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t hc_i2c_init(void);

// MCP23017 pair. Relays/pump outputs on 0x20, TERM inputs (GPA) and zone LEDs (GPB) on 0x21.
esp_err_t hc_mcp_init(void);
esp_err_t hc_mcp_write_pin(uint8_t addr, uint8_t pin, bool level);   // pin 0..15 (GPA0..7, GPB0..7)
esp_err_t hc_mcp_read_gpioa(uint8_t addr, uint8_t *value);
esp_err_t hc_mcp_refresh(void);                                      // rewrite IODIR/OLAT (after a glitch)

// ADS1115 single-ended single-shot read, gain +/-4.096 V, 860 SPS. channel 0..3 = AIN0..AIN3. Returns volts.
esp_err_t hc_ads_read(uint8_t addr, uint8_t channel, float *volts);

// DS3231M: time kept in UTC.
esp_err_t hc_rtc_read(time_t *utc);
esp_err_t hc_rtc_write(time_t utc);

// SSD1306 128x32 with the built-in proportional font.
esp_err_t hc_oled_init(bool rotate180);
void hc_oled_clear(void);
typedef enum { HC_ALIGN_LEFT, HC_ALIGN_RIGHT } hc_align_t;
int hc_oled_text_width(const char *utf8);
void hc_oled_print(int x, int y, hc_align_t align, const char *utf8);
esp_err_t hc_oled_flush(void);

#ifdef __cplusplus
}
#endif
