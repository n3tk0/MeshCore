#pragma once

#include "DisplayDriver.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#define SH110X_NO_SPLASH
#include <Adafruit_SH110X.h>

#ifndef PIN_OLED_RESET
#define PIN_OLED_RESET -1
#endif

#ifndef DISPLAY_ADDRESS
#define DISPLAY_ADDRESS 0x3C
#endif

#ifdef DISPLAY_CYRILLIC
#include "AdafruitGfxUtf8.h"
#endif

class SH1106Display : public DisplayDriver
{
#ifdef DISPLAY_CYRILLIC
  AdafruitGfxUtf8<Adafruit_SH1106G> display;
  uint8_t _text_size = 1;
#else
  Adafruit_SH1106G display;
#endif
  bool _isOn;
  uint8_t _color;

  bool i2c_probe(TwoWire &wire, uint8_t addr);

public:
  SH1106Display() : DisplayDriver(128, 64), display(128, 64, &Wire, PIN_OLED_RESET) { _isOn = false; }
  bool begin();

  bool isOn() override { return _isOn; }
  void turnOn() override;
  void turnOff() override;
  void clear() override;
  void startFrame(ColorVal bkg = UIColor::window_bkg) override;
  void setTextSize(int sz) override;
#ifdef DISPLAY_CYRILLIC
  void setTextFont(const TextFont* font) override;
#endif
  void setColor(ColorVal c) override;
  void setCursor(int x, int y) override;
  void print(const char *str) override;
  void fillRect(int x, int y, int w, int h) override;
  void drawRect(int x, int y, int w, int h) override;
  void drawXbm(int x, int y, const uint8_t *bits, int w, int h) override;
  uint16_t getTextWidth(const char *str) override;
#ifdef DISPLAY_CYRILLIC
  void translateUTF8ToBlocks(char* dest, const char* src, size_t dest_size) override;
#endif
  void endFrame() override;
};
