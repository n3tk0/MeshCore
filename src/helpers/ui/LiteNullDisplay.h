#pragma once

// Light builds without a screen (tools/lite-flags.sh, WITH_DISPLAY not ticked): every C++ file gets this
// header with -include. A board with a display keeps its UI task, buttons, LED and buzzer, only the
// screen becomes a no-op, so the display driver code is not linked. Boards with no DISPLAY_CLASS stay as
// they are.
#if defined(__cplusplus) && defined(LITE_NO_DISPLAY) && defined(DISPLAY_CLASS)
  // relative include: library sources get this header too, without src/ on their include path
  #include "DisplayDriver.h"

  // NullDisplayDriver that takes whatever the board's real display constructor took
  class LiteNullDisplay : public DisplayDriver {
  public:
    template <typename... Args>
    explicit LiteNullDisplay(Args...) : DisplayDriver(128, 64) { }
    bool begin() { return false; }   // not present

    bool isOn() override { return false; }
    void turnOn() override { }
    void turnOff() override { }
    void clear() override { }
    void startFrame(ColorVal bkg = UIColor::window_bkg) override { }
    void setTextSize(int sz) override { }
    void setColor(ColorVal c) override { }
    void setCursor(int x, int y) override { }
    void print(const char* str) override { }
    void fillRect(int x, int y, int w, int h) override { }
    void drawRect(int x, int y, int w, int h) override { }
    void drawXbm(int x, int y, const uint8_t* bits, int w, int h) override { }
    uint16_t getTextWidth(const char* str) override { return 0; }
    void endFrame() { }
  };

  #undef DISPLAY_CLASS
  #define DISPLAY_CLASS LiteNullDisplay
#endif
