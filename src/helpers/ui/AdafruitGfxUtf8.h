#pragma once

#include <stdint.h>
#include <stddef.h>
#include "CyrillicFont5x8.h"
#include "EmojiIcons5x8.h"
#include "TextFont.h"

// Wraps any Adafruit_GFX subclass (Adafruit_SSD1306, Adafruit_SH1106G, Adafruit_SH1107,
// Adafruit_ST7789, ...) to decode UTF-8 in write() and draw Cyrillic (U+0401, U+0410..U+044F,
// U+0451) and 5x8 emoji icons (PUA U+E000..) from built-in 5x8 tables (or an optional custom
// TextFont); other non-ASCII code points render as a CP437 solid block.
template <typename BaseGfx>
class AdafruitGfxUtf8 : public BaseGfx {
  uint16_t _u8_cp = 0;            // accumulated UTF-8 code point
  uint8_t _u8_rem = 0;            // continuation bytes remaining (1 for 2-byte, 2 for 3-byte)
  uint8_t _skip = 0;              // continuation bytes still to skip (4-byte sequences)
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

  void drawIconInFontCell(const uint8_t* cols) {
    if (this->wrap && this->cursor_x + _font->w > this->_width) {
      this->cursor_x = 0;
      this->cursor_y += _font->h;
    }
    if (this->textbgcolor != this->textcolor) {
      this->fillRect(this->cursor_x, this->cursor_y, _font->w, _font->h, this->textbgcolor);
    }
    int16_t ox = this->cursor_x + (_font->w > 5 ? (_font->w - 5) / 2 : 0);
    int16_t oy = this->cursor_y + (_font->h > 8 ? (_font->h - 8) / 2 : 0);
    for (int8_t i = 0; i < 5; i++) {
      uint8_t line = cols[i];
      for (int8_t j = 0; j < 8; j++, line >>= 1) {
        if (line & 1) this->drawPixel(ox + i, oy + j, this->textcolor);
      }
    }
    this->cursor_x += _font->w;
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

  void drawDecodedCodepoint(uint16_t cp) {
    int icon = emojiIconIndex(cp);
    if (icon >= 0) {
      if (!_font) drawGlyph(emoji_icons_5x8[icon]);
      else drawIconInFontCell(emoji_icons_5x8[icon]);
      return;
    }
    if (_font) { drawCodepoint(cp); return; }
    int idx = cyrillicGlyphIndex(cp);
    if (idx >= 0) drawGlyph(cyrillic_font_5x8[idx]);
    else drawBlock();
  }

public:
  using BaseGfx::BaseGfx;
  using BaseGfx::write;

  size_t write(uint8_t c) override {
    if (_skip) {
      if ((c & 0xC0) == 0x80) { _skip--; return 1; }
      _skip = 0;
    }
    if (_u8_rem) {
      if ((c & 0xC0) == 0x80) {
        _u8_cp = (uint16_t)((_u8_cp << 6) | (c & 0x3F));
        if (--_u8_rem == 0) {
          drawDecodedCodepoint(_u8_cp);
        }
        return 1;
      }
      _u8_rem = 0;
      drawBlock();   // interrupted multi-byte sequence
    }
    if (c < 0x80) {
      if (!_font) return BaseGfx::write(c);
      if (c == '\n') { this->cursor_x = 0; this->cursor_y += _font->h; }
      else if (c != '\r') drawCodepoint(c);
      return 1;
    }
    // 0xDB is both the CP437 block (from translateUTF8ForDisplay, followed by ASCII or end) and a valid
    // 2-byte lead (U+06C0..): as a lead it draws the block itself when no continuation byte follows.
    if ((c & 0xE0) == 0xC0) { _u8_cp = c & 0x1F; _u8_rem = 1; return 1; }
    if ((c & 0xF0) == 0xE0) { _u8_cp = c & 0x0F; _u8_rem = 2; return 1; }
    if ((c & 0xF8) == 0xF0) { _skip = 3; drawBlock(); return 1; }
    drawBlock();     // stray continuation byte
    return 1;
  }

  void flushUtf8() { if (_u8_rem) { _u8_rem = 0; drawBlock(); } _skip = 0; }
  void setTextFont(const TextFont* f) { _font = f; }
  const TextFont* textFont() const { return _font; }
};
