#pragma once

#include "DisplayDriver.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#define SSD1306_NO_SPLASH
#include <Adafruit_SSD1306.h>
#include <helpers/RefCountedDigitalPin.h>

#ifndef PIN_OLED_RESET
  #define PIN_OLED_RESET        21 // Reset pin # (or -1 if sharing Arduino reset pin)
#endif

#ifndef DISPLAY_ADDRESS
  #define DISPLAY_ADDRESS   0x3C
#endif

#ifdef DISPLAY_CYRILLIC
// Adafruit_SSD1306 that decodes UTF-8 in write() and draws Cyrillic (U+0401, U+0410..U+044F,
// U+0451) from a built-in 5x8 table; other non-ASCII code points become a CP437 block.
class SSD1306Utf8 : public Adafruit_SSD1306 {
  uint8_t _lead = 0;       // pending UTF-8 lead byte
  uint8_t _skip = 0;       // continuation bytes still to skip (3/4-byte sequences)
  void drawGlyph(const uint8_t* cols);
  void drawBlock() { Adafruit_SSD1306::write(0xDB); }
public:
  SSD1306Utf8(uint8_t w, uint8_t h, TwoWire* twi, int8_t rst) : Adafruit_SSD1306(w, h, twi, rst) { }
  size_t write(uint8_t c) override;
  void flushUtf8() { if (_lead) { _lead = 0; drawBlock(); } _skip = 0; }
};
#endif

class SSD1306Display : public DisplayDriver {
#ifdef DISPLAY_CYRILLIC
  SSD1306Utf8 display;
  uint8_t _text_size = 1;
#else
  Adafruit_SSD1306 display;
#endif
  bool _isOn;
  uint8_t _color;
  RefCountedDigitalPin* _peripher_power;

  bool i2c_probe(TwoWire& wire, uint8_t addr);
public:
  SSD1306Display(RefCountedDigitalPin* peripher_power=NULL) : DisplayDriver(128, 64), 
      display(128, 64, &Wire, PIN_OLED_RESET),
      _peripher_power(peripher_power)
  {
    _isOn = false; 
  }
  bool begin();

  bool isOn() override { return _isOn; }
  void turnOn() override;
  void turnOff() override;
  void clear() override;
  void startFrame(ColorVal bkg = UIColor::window_bkg) override;
  void setTextSize(int sz) override;
  void setColor(ColorVal c) override;
  void setCursor(int x, int y) override;
  void print(const char* str) override;
  void fillRect(int x, int y, int w, int h) override;
  void drawRect(int x, int y, int w, int h) override;
  void drawXbm(int x, int y, const uint8_t* bits, int w, int h) override;
  uint16_t getTextWidth(const char* str) override;
#ifdef DISPLAY_CYRILLIC
  void translateUTF8ToBlocks(char* dest, const char* src, size_t dest_size) override;
#endif
  void endFrame() override;
};
