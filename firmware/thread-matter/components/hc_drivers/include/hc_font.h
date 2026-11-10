#pragma once
// Proportional 1-bpp font for the OLED: glyphs ' '..'~' then the degree sign; 16 rows, MSB = leftmost pixel.
#include <stdint.h>

#define HC_FONT_HEIGHT 16
#define HC_FONT_GLYPHS 96

typedef struct {
    uint8_t width;
    uint16_t rows[HC_FONT_HEIGHT];
} hc_glyph_t;

extern const hc_glyph_t hc_font_glyphs[HC_FONT_GLYPHS];
