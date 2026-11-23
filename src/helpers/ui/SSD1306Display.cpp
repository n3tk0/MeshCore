#include "SSD1306Display.h"

#ifdef DISPLAY_CYRILLIC
void SSD1306Display::translateUTF8ToBlocks(char* dest, const char* src, size_t dest_size) {
  translateUTF8ForDisplay(dest, src, dest_size, true, true);
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
  // The SA0 strap selects 0x3C or 0x3D and differs between modules: fall back to the other address
  // of the pair (as SH1106Display does). The Adafruit init runs even when nothing answered, because
  // it allocates the frame buffer that halt() and turnOn() use regardless.
  uint8_t addr = 0;
  if (i2c_probe(Wire, DISPLAY_ADDRESS)) addr = DISPLAY_ADDRESS;
  else if (i2c_probe(Wire, DISPLAY_ADDRESS ^ 1)) addr = DISPLAY_ADDRESS ^ 1;
  bool ok = display.begin(SSD1306_SWITCHCAPVCC, addr ? addr : DISPLAY_ADDRESS, true, false);
  if (_dim || _contrast != 0xCF) { applyDrive(); setContrast(_contrast); }   // the brightness from before a re-init
  return addr != 0 && ok;
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

void SSD1306Display::setContrast(uint8_t level) {
  _contrast = level;
  display.ssd1306_command(SSD1306_SETCONTRAST);
  display.ssd1306_command(level);
}

// Night dimming beyond contrast: VCOMH (0xDB) down from ~0.77 to ~0.65 Vcc, and for the dimmest
// level also the shortest pre-charge phases (0xD9, 1 + 1 clocks instead of the charge-pump 0xF1).
void SSD1306Display::setDim(uint8_t level) {
  _dim = level > 2 ? 2 : level;
  applyDrive();
}

void SSD1306Display::applyDrive() {
  display.ssd1306_command(SSD1306_SETPRECHARGE);
  display.ssd1306_command(_dim >= 2 ? 0x11 : 0xF1);
  display.ssd1306_command(SSD1306_SETVCOMDETECT);
  display.ssd1306_command(_dim >= 1 ? 0x00 : 0x40);
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
  display.setTextFont(NULL);
#endif
  display.cp437(true);         // Use full 256 char 'Code Page 437' font
}

void SSD1306Display::setTextSize(int sz) {
  display.setTextSize(sz);
#ifdef DISPLAY_CYRILLIC
  _text_size = sz;
  display.setTextFont(NULL);
#endif
}

#ifdef DISPLAY_CYRILLIC
void SSD1306Display::setTextFont(const TextFont* font) {
  display.setTextSize(1);
  _text_size = 1;
  display.setTextFont(font);
}
#endif

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
  const TextFont* f = display.textFont();
  return utf8CharCount(str) * (f ? f->w : 6 * _text_size);
#else
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(str, 0, 0, &x1, &y1, &w, &h);
  return w;
#endif
}

void SSD1306Display::endFrame() {
  display.display();
}
