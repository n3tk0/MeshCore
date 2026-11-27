#pragma once

#include <stdint.h>
#include <string.h>
#include "Utf8DisplayText.h"

using ColorVal = uint16_t;

struct TextFont;

class UIColor {
public:
  // color definitions (by element _type_)
  static ColorVal window_bkg, title_bkg, title_txt, primary_txt, secondary_txt, warning_txt, popup_bkg, popup_txt, corp_blue;
};

class DisplayDriver {
  int _w, _h;
protected:
  DisplayDriver(int w, int h) { _w = w; _h = h; }
public:
  //enum Color { DARK=0, LIGHT, RED, GREEN, BLUE, YELLOW, ORANGE }; // on b/w screen, colors will be !=0 synonym of light

  int width() const { return _w; }
  int height() const { return _h; }

  virtual bool isOn() = 0;
  virtual bool isEink() { return false; } // default to non-eink, override in eink drivers
  virtual void forceFullRefresh() {} // next refresh will be full for eink
  virtual void setBusyHook(void (*hook)()) {} // eink: hook is called repeatedly while waiting for panel refresh
  virtual void turnOn() = 0;
  virtual void turnOn(int max_partial) { turnOn(); }
  virtual void turnOff() = 0;
  virtual void turnOff(bool blank) { turnOff(); }
  virtual void setContrast(uint8_t level) { }   // 0..255, where the panel supports it
  // panel drive below what contrast reaches, for night use: 0 = normal, 1 = dim, 2 = dimmest
  virtual void setDim(uint8_t level) { }
  virtual void clear() = 0;
  virtual void startFrame(ColorVal bkg = UIColor::window_bkg) = 0;
  virtual void setTextSize(int sz) = 0;
  // NULL = the built-in font; setTextSize() also resets it. Drivers without custom fonts stay at size 1.
  virtual void setTextFont(const TextFont* font) { setTextSize(1); }
  virtual void setColor(ColorVal c) = 0;
  virtual void setCursor(int x, int y) = 0;
  virtual void print(const char* str) = 0;
  virtual void printWordWrap(const char* str, int max_width) { print(str); }   // fallback to basic print() if no override
  virtual void fillRect(int x, int y, int w, int h) = 0;
  virtual void drawRect(int x, int y, int w, int h) = 0;
  virtual void drawXbm(int x, int y, const uint8_t* bits, int w, int h) = 0;
  virtual uint16_t getTextWidth(const char* str) = 0;
  virtual void drawTextCentered(int mid_x, int y, const char* str) {   // helper method (override to optimise)
    int w = getTextWidth(str);
    setCursor(mid_x - w/2, y);
    print(str);
  }
  virtual void drawTextRightAlign(int x_anch, int y, const char* str) {
    int w = getTextWidth(str);
    setCursor(x_anch - w, y);
    print(str);
  }
  virtual void drawTextLeftAlign(int x_anch, int y, const char* str) {
    setCursor(x_anch, y);
    print(str);
  }
  
  // convert UTF-8 characters to displayable ASCII/block characters for compatibility
  virtual void translateUTF8ToBlocks(char* dest, const char* src, size_t dest_size) {
    translateUTF8ForDisplay(dest, src, dest_size, false);
  }
  
  // draw text with ellipsis if it exceeds max_width
  virtual void drawTextEllipsized(int x, int y, int max_width, const char* str) {
    char temp_str[256];  // reasonable buffer size
    size_t len = strlen(str);
    bool truncated = false;
    // Leave headroom for ellipsis ("... " = 4 bytes + NUL)
    const size_t max_copy = sizeof(temp_str) - 5;
    if (len > max_copy) {
      len = max_copy;
      while (len > 0 && ((unsigned char)str[len] & 0xC0) == 0x80) len--;
      truncated = true;
    }
    memcpy(temp_str, str, len);
    temp_str[len] = 0;
    
    if (!truncated && getTextWidth(temp_str) <= max_width) {
      setCursor(x, y);
      print(temp_str);
      return;
    }
    
    // for variable-width fonts (GxEPD), add space after ellipsis
    // for fixed-width fonts (OLED), keep tight spacing to save precious characters
    const char* ellipsis;
    // use a simple heuristic: if 'i' and 'l' have different widths, it's variable-width
    int i_width = getTextWidth("i");
    int l_width = getTextWidth("l");
    if (i_width != l_width) {
      ellipsis = "... ";  // variable-width fonts: add space
    } else {
      ellipsis = "...";   // fixed-width fonts: no space
    }
    
    int ellipsis_width = getTextWidth(ellipsis);
    int str_len = strlen(temp_str);
    
    while (str_len > 0 && getTextWidth(temp_str) > max_width - ellipsis_width) {
      while (str_len > 1 && ((unsigned char)temp_str[str_len - 1] & 0xC0) == 0x80) {
        str_len--;
      }
      temp_str[--str_len] = 0;
    }
    strcat(temp_str, ellipsis);
    
    setCursor(x, y);
    print(temp_str);
  }
  
  virtual void endFrame() = 0;
};
