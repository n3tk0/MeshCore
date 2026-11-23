#pragma once

#include <stdint.h>
#include <stddef.h>
#include "CyrillicFont5x8.h"
#include "TextFont.h"

// Wraps any Adafruit_GFX subclass (Adafruit_SSD1306, Adafruit_SH1106G, Adafruit_SH1107,
// Adafruit_ST7789, ...) to decode UTF-8 in write() and draw Cyrillic (U+0401, U+0410..U+044F,
// U+0451) from the built-in 5x8 table (or an optional custom TextFont); other non-ASCII code
// points render as a CP437 solid block.
template <typename BaseGfx>
class AdafruitGfxUtf8 : public BaseGfx {
  uint8_t _lead = 0;              // pending UTF-8 lead byte
  uint8_t _skip = 0;              // continuation bytes still to skip (3/4-byte sequences)
  const TextFont* _font = NULL;   // NULL: the built-in 5x8 font (scaled by the text size)

  void drawGlyph(const uint8_t* cols) {
    if (this->wrap && (this->cursor_x + this->textsize_x * 6) > this->_width) {
      this->cursor_x = 0;
      this->cursor_y += this->textsize_y * 8;
    }
    for (int8_t i = 0; i < 5; i++) {
      uint8_t line = cols[i];
      for (int8_t j = 0; j < 8; j++, line >>= 1) {
        if (line & 1) {
          if (this->textsize_x == 1 && this->textsize_y == 1) {
            this->drawPixel(this->cursor_x + i, this->cursor_y + j, this->textcolor);
          } else {
            this->fillRect(this->cursor_x + i * this->textsize_x, this->cursor_y + j * this->textsize_y,
                           this->textsize_x, this->textsize_y, this->textcolor);
          }
        } else if (this->textbgcolor != this->textcolor) {
          if (this->textsize_x == 1 && this->textsize_y == 1) {
            this->drawPixel(this->cursor_x + i, this->cursor_y + j, this->textbgcolor);
          } else {
            this->fillRect(this->cursor_x + i * this->textsize_x, this->cursor_y + j * this->textsize_y,
                           this->textsize_x, this->textsize_y, this->textbgcolor);
          }
        }
      }
    }
    this->cursor_x += this->textsize_x * 6;
  }

  void drawBlock() {
    if (!_font) { BaseGfx::write(0xDB); return; }
    if (this->wrap && this->cursor_x + _font->w > this->_width) {
      this->cursor_x = 0;
      this->cursor_y += _font->h;
    }
    this->fillRect(this->cursor_x, this->cursor_y + 1, _font->w - 1, _font->h - 2, this->textcolor);
    this->cursor_x += _font->w;
  }

  // one code point in the custom font
  void drawCodepoint(uint16_t cp) {
    int idx = textFontIndex(cp);
    if (idx < 0) { drawBlock(); return; }
    if (this->wrap && this->cursor_x + _font->w > this->_width) {
      this->cursor_x = 0;
      this->cursor_y += _font->h;
    }
    const uint8_t* bits = textFontGlyph(_font, idx);
    if (this->textbgcolor != this->textcolor) {
      this->drawBitmap(this->cursor_x, this->cursor_y, bits, _font->w, _font->h, this->textcolor, this->textbgcolor);
    } else {
      this->drawBitmap(this->cursor_x, this->cursor_y, bits, _font->w, _font->h, this->textcolor);
    }
    this->cursor_x += _font->w;
  }

public:
  using BaseGfx::BaseGfx;
  using BaseGfx::write;

  size_t write(uint8_t c) override {
    if (_skip) {
      if ((c & 0xC0) == 0x80) { _skip--; return 1; }
      _skip = 0;
    }
    if (_lead) {
      if ((c & 0xC0) == 0x80) {
        uint16_t cp = ((uint16_t)(_lead & 0x1F) << 6) | (c & 0x3F);
        _lead = 0;
        if (_font) { drawCodepoint(cp); return 1; }
        int idx = cyrillicGlyphIndex(cp);
        if (idx >= 0) drawGlyph(cyrillic_font_5x8[idx]);
        else drawBlock();
        return 1;
      }
      _lead = 0;
      drawBlock();   // lone lead byte
    }
    if (c < 0x80) {
      if (!_font) return BaseGfx::write(c);
      if (c == '\n') { this->cursor_x = 0; this->cursor_y += _font->h; }
      else if (c != '\r') drawCodepoint(c);
      return 1;
    }
    if (c == 0xDB) { drawBlock(); return 1; }   // CP437 solid block from translateUTF8ToBlocks
    if ((c & 0xE0) == 0xC0) { _lead = c; return 1; }
    if ((c & 0xF0) == 0xE0) { _skip = 2; drawBlock(); return 1; }
    if ((c & 0xF8) == 0xF0) { _skip = 3; drawBlock(); return 1; }
    drawBlock();     // stray continuation byte
    return 1;
  }

  void flushUtf8() { if (_lead) { _lead = 0; drawBlock(); } _skip = 0; }
  void setTextFont(const TextFont* f) { _font = f; }
  const TextFont* textFont() const { return _font; }
};
