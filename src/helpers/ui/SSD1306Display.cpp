#include "SSD1306Display.h"

#ifdef DISPLAY_CYRILLIC
#include "CyrillicFont5x8.h"

void SSD1306Utf8::drawGlyph(const uint8_t* cols) {
  if (wrap && (cursor_x + textsize_x * 6) > _width) {
    cursor_x = 0;
    cursor_y += textsize_y * 8;
  }
  for (int8_t i = 0; i < 5; i++) {
    uint8_t line = cols[i];
    for (int8_t j = 0; j < 8; j++, line >>= 1) {
      if (line & 1) {
        if (textsize_x == 1 && textsize_y == 1) drawPixel(cursor_x + i, cursor_y + j, textcolor);
        else fillRect(cursor_x + i * textsize_x, cursor_y + j * textsize_y, textsize_x, textsize_y, textcolor);
      } else if (textbgcolor != textcolor) {
        if (textsize_x == 1 && textsize_y == 1) drawPixel(cursor_x + i, cursor_y + j, textbgcolor);
        else fillRect(cursor_x + i * textsize_x, cursor_y + j * textsize_y, textsize_x, textsize_y, textbgcolor);
      }
    }
  }
  cursor_x += textsize_x * 6;
}

size_t SSD1306Utf8::write(uint8_t c) {
  if (_skip) {
    if ((c & 0xC0) == 0x80) { _skip--; return 1; }
    _skip = 0;
  }
  if (_lead) {
    if ((c & 0xC0) == 0x80) {
      uint16_t cp = ((uint16_t)(_lead & 0x1F) << 6) | (c & 0x3F);
      _lead = 0;
      int idx = cyrillicGlyphIndex(cp);
      if (idx >= 0) drawGlyph(cyrillic_font_5x8[idx]);
      else drawBlock();
      return 1;
    }
    _lead = 0;
    drawBlock();   // lone lead byte (e.g. the CP437 block from translateUTF8ToBlocks)
  }
  if (c < 0x80) return Adafruit_SSD1306::write(c);
  if ((c & 0xE0) == 0xC0) { _lead = c; return 1; }
  if ((c & 0xF0) == 0xE0) { _skip = 2; drawBlock(); return 1; }
  if ((c & 0xF8) == 0xF0) { _skip = 3; drawBlock(); return 1; }
  drawBlock();     // stray continuation byte
  return 1;
}

void SSD1306Display::translateUTF8ToBlocks(char* dest, const char* src, size_t dest_size) {
  // keep 2-byte sequences the font can draw, replace everything else with a block
  size_t j = 0;
  for (size_t i = 0; src[i] != 0 && j < dest_size - 1; i++) {
    unsigned char c = (unsigned char)src[i];
    if (c >= 32 && c <= 126) {
      dest[j++] = c;
    } else if ((c & 0xE0) == 0xC0 && ((unsigned char)src[i+1] & 0xC0) == 0x80) {
      uint16_t cp = ((uint16_t)(c & 0x1F) << 6) | ((unsigned char)src[i+1] & 0x3F);
      if (cyrillicGlyphIndex(cp) >= 0 && j + 2 < dest_size) {
        dest[j++] = c;
        dest[j++] = src[i+1];
      } else {
        dest[j++] = '\xDB';
      }
      i++;
    } else if (c >= 0x80) {
      dest[j++] = '\xDB';
      while (src[i+1] && ((unsigned char)src[i+1] & 0xC0) == 0x80) i++;
    }
  }
  dest[j] = 0;
}
#endif

bool SSD1306Display::i2c_probe(TwoWire& wire, uint8_t addr) {
  wire.beginTransmission(addr);
  uint8_t error = wire.endTransmission();
  return (error == 0);
}

// Color scheme
ColorVal UIColor::window_bkg = SSD1306_BLACK;
ColorVal UIColor::title_bkg = SSD1306_BLACK;
ColorVal UIColor::title_txt = SSD1306_WHITE;
ColorVal UIColor::primary_txt = SSD1306_WHITE;
ColorVal UIColor::secondary_txt = SSD1306_WHITE;
ColorVal UIColor::warning_txt = SSD1306_WHITE;
ColorVal UIColor::popup_bkg = SSD1306_BLACK;
ColorVal UIColor::popup_txt = SSD1306_WHITE;
ColorVal UIColor::corp_blue = SSD1306_WHITE;

bool SSD1306Display::begin() {
  if (!_isOn) {
    if (_peripher_power) _peripher_power->claim();
    _isOn = true;
  }
  #ifdef DISPLAY_ROTATION
  display.setRotation(DISPLAY_ROTATION);
  #endif
  return display.begin(SSD1306_SWITCHCAPVCC, DISPLAY_ADDRESS, true, false) && i2c_probe(Wire, DISPLAY_ADDRESS);
}

void SSD1306Display::turnOn() {
  if (!_isOn) {
    if (_peripher_power) _peripher_power->claim();
    _isOn = true;  // set before begin() to prevent double claim
    if (_peripher_power) begin();  // re-init display after power was cut
  }
  display.ssd1306_command(SSD1306_DISPLAYON);
}

void SSD1306Display::turnOff() {
  display.ssd1306_command(SSD1306_DISPLAYOFF);
  if (_isOn) {
    if (_peripher_power) {
#if PIN_OLED_RESET >= 0
      digitalWrite(PIN_OLED_RESET, LOW);
#endif
      _peripher_power->release();
    }
    _isOn = false;
  }
}

void SSD1306Display::clear() {
  display.clearDisplay();
  display.display();
}

void SSD1306Display::startFrame(ColorVal bkg) {
  display.clearDisplay();  // TODO: apply 'bkg'
  _color = SSD1306_WHITE;
  display.setTextColor(_color);
  display.setTextSize(1);
#ifdef DISPLAY_CYRILLIC
  _text_size = 1;
#endif
  display.cp437(true);         // Use full 256 char 'Code Page 437' font
}

void SSD1306Display::setTextSize(int sz) {
  display.setTextSize(sz);
#ifdef DISPLAY_CYRILLIC
  _text_size = sz;
#endif
}

void SSD1306Display::setColor(ColorVal c) {
  _color = c;
  display.setTextColor(_color);
}

void SSD1306Display::setCursor(int x, int y) {
  display.setCursor(x, y);
}

void SSD1306Display::print(const char* str) {
  display.print(str);
#ifdef DISPLAY_CYRILLIC
  display.flushUtf8();   // draw a trailing lone lead byte
#endif
}

void SSD1306Display::fillRect(int x, int y, int w, int h) {
  display.fillRect(x, y, w, h, _color);
}

void SSD1306Display::drawRect(int x, int y, int w, int h) {
  display.drawRect(x, y, w, h, _color);
}

void SSD1306Display::drawXbm(int x, int y, const uint8_t* bits, int w, int h) {
  display.drawBitmap(x, y, bits, w, h, _color);
}

uint16_t SSD1306Display::getTextWidth(const char* str) {
#ifdef DISPLAY_CYRILLIC
  // fixed 6px cells: count characters, not bytes (UTF-8 continuation bytes don't count)
  uint16_t n = 0;
  for (const unsigned char* p = (const unsigned char*)str; *p; p++) {
    if ((*p & 0xC0) != 0x80) n++;
  }
  return n * 6 * _text_size;
#endif
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(str, 0, 0, &x1, &y1, &w, &h);
  return w;
}

void SSD1306Display::endFrame() {
  display.display();
}
