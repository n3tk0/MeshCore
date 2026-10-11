#pragma once

#include <stdint.h>

// Fixed-width bitmap font for UTF-8 text: ASCII 0x20..0x7E, then U+0401, U+0410..U+044F, U+0451.
// Each glyph is h rows of (w + 7) / 8 bytes, MSB first (the Adafruit_GFX::drawBitmap layout).
struct TextFont {
  uint8_t w, h;
  const uint8_t* bits;
};

// glyph index of a code point, -1 when the font has none
static inline int textFontIndex(uint16_t cp) {
  if (cp >= 0x20 && cp <= 0x7E) return cp - 0x20;
  if (cp == 0x0401) return 95;
  if (cp >= 0x0410 && cp <= 0x044F) return 96 + (cp - 0x0410);
  if (cp == 0x0451) return 160;
  return -1;
}

static inline const uint8_t* textFontGlyph(const TextFont* f, int idx) {
  return f->bits + idx * f->h * ((f->w + 7) / 8);
}
