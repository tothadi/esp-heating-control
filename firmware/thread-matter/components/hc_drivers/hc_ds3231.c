// DS3231M real-time clock (register compatible with the DS1307 layout used by ESPHome). Holds UTC.
#include "hc_drivers.h"
#include "hc_board.h"
#include "hc_i2c_priv.h"

static uint8_t bcd2bin(uint8_t v) { return (v & 0x0F) + 10 * (v >> 4); }
static uint8_t bin2bcd(uint8_t v) { return (uint8_t) (((v / 10) << 4) | (v % 10)); }

esp_err_t hc_rtc_read(time_t *utc)
{
    uint8_t reg = 0x00, r[7];
    esp_err_t err = hc_i2c_write_read(HC_ADDR_RTC, &reg, 1, r, 7);
    if (err != ESP_OK) return err;
    struct tm t = {
        .tm_sec = bcd2bin(r[0] & 0x7F), .tm_min = bcd2bin(r[1] & 0x7F), .tm_hour = bcd2bin(r[2] & 0x3F),
        .tm_mday = bcd2bin(r[4] & 0x3F), .tm_mon = bcd2bin(r[5] & 0x1F) - 1, .tm_year = bcd2bin(r[6]) + 100,
    };
    if (t.tm_mon < 0 || t.tm_mon > 11 || t.tm_mday < 1 || t.tm_hour > 23) return ESP_ERR_INVALID_RESPONSE;
    // timegm() equivalent (days from civil date)
    int y = t.tm_year + 1900, m = t.tm_mon + 1;
    y -= m <= 2;
    const int era = y / 400, yoe = y - era * 400;
    const int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + t.tm_mday - 1;
    const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const long days = era * 146097L + doe - 719468L;
    *utc = (time_t) days * 86400 + t.tm_hour * 3600 + t.tm_min * 60 + t.tm_sec;
    return ESP_OK;
}

esp_err_t hc_rtc_write(time_t utc)
{
    struct tm t;
    gmtime_r(&utc, &t);
    uint8_t w[8] = {0x00, bin2bcd(t.tm_sec), bin2bcd(t.tm_min), bin2bcd(t.tm_hour), (uint8_t) (t.tm_wday + 1),
                    bin2bcd(t.tm_mday), bin2bcd(t.tm_mon + 1), bin2bcd(t.tm_year % 100)};
    esp_err_t err = hc_i2c_write(HC_ADDR_RTC, w, sizeof(w));
    if (err == ESP_OK) {
        uint8_t st[2] = {0x0F, 0x00};   // clear OSF (oscillator stop flag)
        err = hc_i2c_write(HC_ADDR_RTC, st, 2);
    }
    return err;
}
