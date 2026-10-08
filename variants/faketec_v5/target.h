#pragma once

#define RADIOLIB_STATIC_ONLY 1
#include <RadioLib.h>
#include <helpers/radiolib/RadioLibWrappers.h>
#include <FaketecBoard.h>
#include <helpers/radiolib/CustomSX1262Wrapper.h>
#include <helpers/AutoDiscoverRTCClock.h>
#ifdef DISPLAY_CLASS
  #include <helpers/ui/SSD1306Display.h>
  #include <helpers/ui/MomentaryButton.h>
#endif
#if defined(UI_HAS_ROTARY_INPUT)
  #include <helpers/ui/QuadratureRotaryInput.h>
#endif

#include <helpers/sensors/EnvironmentSensorManager.h>

extern FaketecBoard board;
extern WRAPPER_CLASS radio_driver;
extern AutoDiscoverRTCClock rtc_clock;
extern EnvironmentSensorManager sensors;

#ifdef DISPLAY_CLASS
  extern DISPLAY_CLASS display;
  extern MomentaryButton user_btn;
#endif
#if defined(UI_HAS_ROTARY_INPUT)
  extern RotaryInput& rotary_input;
  #if defined(PIN_ENCODER_BTN)
    extern MomentaryButton encoder_btn;
  #endif
#endif

bool radio_init();
mesh::LocalIdentity radio_new_identity();

