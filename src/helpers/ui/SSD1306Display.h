#pragma once

#include "DisplayDriver.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#define SSD1306_NO_SPLASH
#include <Adafruit_SSD1306.h>
#include <helpers/RefCountedDigitalPin.h>
#include "TextFont.h"

#ifndef PIN_OLED_RESET
  #define PIN_OLED_RESET        21 // Reset pin # (or -1 if sharing Arduino reset pin)
#endif

#ifndef DISPLAY_ADDRESS
  #define DISPLAY_ADDRESS   0x3C
#endif

#ifdef DISPLAY_CYRILLIC
#include "AdafruitGfxUtf8.h"
#endif

class SSD1306Display : public DisplayDriver {
#ifdef DISPLAY_CYRILLIC
  AdafruitGfxUtf8<Adafruit_SSD1306> display;
  uint8_t _text_size = 1;
#else
  Adafruit_SSD1306 display;
#endif
  bool _isOn;
  uint8_t _color;
  uint8_t _contrast = 0xCF;   // the Adafruit init values, applied again after a re-init
  uint8_t _dim = 0;
  RefCountedDigitalPin* _peripher_power;

  bool i2c_probe(TwoWire& wire, uint8_t addr);
  void applyDrive();
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
  void setContrast(uint8_t level) override;
  void setDim(uint8_t level) override;
  void clear() override;
  void startFrame(ColorVal bkg = UIColor::window_bkg) override;
  void setTextSize(int sz) override;
#ifdef DISPLAY_CYRILLIC
  void setTextFont(const TextFont* font) override;
#endif
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
