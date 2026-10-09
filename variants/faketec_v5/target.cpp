#include <Arduino.h>
#include "target.h"
#include <helpers/ArduinoHelpers.h>

FaketecBoard board;

RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, SPI);

WRAPPER_CLASS radio_driver(radio, board);

VolatileRTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);
#if ENV_INCLUDE_GPS
  #include <helpers/sensors/MicroNMEALocationProvider.h>
  MicroNMEALocationProvider nmea = MicroNMEALocationProvider(Serial1, &rtc_clock);
  EnvironmentSensorManager sensors = EnvironmentSensorManager(nmea);
#else
  EnvironmentSensorManager sensors;
#endif

#ifdef DISPLAY_CLASS
  DISPLAY_CLASS display;
  #if defined(UI_HAS_ROTARY_INPUT) && defined(PIN_ENCODER_BTN)
    MomentaryButton user_btn(PIN_USER_BTN, 800, true, true, false);   // Back: no multi-click delay
  #else
    MomentaryButton user_btn(PIN_USER_BTN, 1000, true, true);
  #endif
#endif

#if defined(UI_HAS_ROTARY_INPUT)
  #ifndef ENCODER_STEPS_PER_DETENT
    #define ENCODER_STEPS_PER_DETENT 4
  #endif
  #ifndef ENCODER_REVERSE
    #define ENCODER_REVERSE false
  #endif
  static QuadratureRotaryInput rotaryInputImpl(PIN_ENCODER_A, PIN_ENCODER_B, ENCODER_STEPS_PER_DETENT, ENCODER_REVERSE);
  RotaryInput& rotary_input = rotaryInputImpl;
  #if defined(PIN_ENCODER_BTN)
    // no multi-click: every press is reported at once; contact bounce is filtered by setDebounce()
    MomentaryButton encoder_btn(PIN_ENCODER_BTN, 800, true, true, false);
  #endif
#endif

bool radio_init() {
  rtc_clock.begin(Wire);
  
  return radio.std_init(&SPI);
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);  // create new random identity
}


