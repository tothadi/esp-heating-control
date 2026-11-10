// SSD1306 128x32 over I2C with a 512-byte frame buffer and the proportional font from hc_font.c.
#include <string.h>
#include "hc_drivers.h"
#include "hc_board.h"
#include "hc_font.h"
#include "hc_i2c_priv.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define W 128
#define H 32
static uint8_t s_fb[W * H / 8];
static SemaphoreHandle_t s_lock;

static esp_err_t cmds(const uint8_t *c, size_t n)
{
    uint8_t buf[32];
    buf[0] = 0x00;
    memcpy(buf + 1, c, n);
    return hc_i2c_write(HC_ADDR_OLED, buf, n + 1);
}

esp_err_t hc_oled_init(bool rotate180)
{
    if (!s_lock) s_lock = xSemaphoreCreateMutex();
    const uint8_t init[] = {
        0xAE, 0xD5, 0x80, 0xA8, 0x1F, 0xD3, 0x00, 0x40, 0x8D, 0x14, 0x20, 0x00,
        rotate180 ? 0xA0 : 0xA1, rotate180 ? 0xC0 : 0xC8,      // segment remap / COM scan direction
        0xDA, 0x02, 0x81, 0x8F, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6, 0x2E, 0xAF,
    };
    hc_oled_clear();
    esp_err_t err = cmds(init, sizeof(init));
    return err == ESP_OK ? hc_oled_flush() : err;
}

void hc_oled_clear(void)
{
    if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY);
    memset(s_fb, 0, sizeof(s_fb));
    if (s_lock) xSemaphoreGive(s_lock);
}

// Next glyph index from UTF-8 (ASCII and the degree sign; anything else becomes '?').
static int next_glyph(const char **p)
{
    const unsigned char c = (unsigned char) **p;
    if (c == 0) return -1;
    if (c < 0x80) { (*p)++; return (c >= 32 && c <= 126) ? c - 32 : '?' - 32; }
    if (c == 0xC2 && (unsigned char) (*p)[1] == 0xB0) { *p += 2; return 95; }
    (*p)++;
    while ((**p & 0xC0) == 0x80) (*p)++;
    return '?' - 32;
}

int hc_oled_text_width(const char *s)
{
    int w = 0, g;
    while ((g = next_glyph(&s)) >= 0) w += hc_font_glyphs[g].width;
    return w;
}

void hc_oled_print(int x, int y, hc_align_t align, const char *s)
{
    if (align == HC_ALIGN_RIGHT) x = x + 1 - hc_oled_text_width(s);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int g;
    while ((g = next_glyph(&s)) >= 0) {
        const hc_glyph_t *gl = &hc_font_glyphs[g];
        for (int r = 0; r < HC_FONT_HEIGHT; r++) {
            const int py = y + r;
            if (py < 0 || py >= H) continue;
            for (int c = 0; c < gl->width; c++) {
                const int px = x + c;
                if (px < 0 || px >= W || !(gl->rows[r] & (0x8000 >> c))) continue;
                s_fb[(py / 8) * W + px] |= 1 << (py & 7);
            }
        }
        x += gl->width;
    }
    xSemaphoreGive(s_lock);
}

esp_err_t hc_oled_flush(void)
{
    const uint8_t win[] = {0x21, 0, W - 1, 0x22, 0, H / 8 - 1};
    esp_err_t err = cmds(win, sizeof(win));
    if (err != ESP_OK) return err;
    uint8_t buf[1 + 128];
    buf[0] = 0x40;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (int off = 0; off < (int) sizeof(s_fb) && err == ESP_OK; off += 128) {
        memcpy(buf + 1, s_fb + off, 128);
        err = hc_i2c_write(HC_ADDR_OLED, buf, sizeof(buf));
    }
    xSemaphoreGive(s_lock);
    return err;
}
