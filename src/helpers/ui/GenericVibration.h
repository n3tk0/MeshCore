#pragma once

#ifdef PIN_VIBRATION

#include <Arduino.h>

/*
 * Vibration motor control class
 *
 * Provides vibration feedback for events like new messages and new contacts
 * Features:
 * - trigger(): 1 s on / 1 s off for VIBRATION_TIMEOUT (default 5 s)
 * - pulse(): single short haptic tick (UI feedback)
 * - playPattern(): on/off sequence in ms, e.g. {150, 150, 150} = two short buzzes
 * - Non-blocking operation
 * - VIBRATION_PWM (0-255): optional duty cycle, e.g. to run a 3V motor from a 4.2V LiPo
 */

#ifndef VIBRATION_TIMEOUT
#define VIBRATION_TIMEOUT 5000 // 5 seconds default
#endif

#define VIBRATION_MAX_STEPS 8

class GenericVibration {
public:
  void begin();       // set up vibration pin
  void trigger();     // trigger vibration if cooldown has passed
  void loop();        // non-blocking timer handling
  bool isVibrating(); // returns true if the trigger() pattern is running
  void stop();        // stop vibration immediately
  void pulse(uint16_t millis_on);  // one short pulse, ignored while a longer one is running
  void playPattern(const uint16_t* steps, uint8_t count);  // on, off, on, ... (ms); replaces any pulse
  bool isBusy() { return isVibrating() || pulse_until != 0 || step_count != 0; }

private:
  void drive(bool on);
  unsigned long duration;
  unsigned long pulse_until = 0;
  bool pattern_on = false;
  uint16_t steps[VIBRATION_MAX_STEPS];
  uint8_t step_count = 0, step_idx = 0;
  unsigned long step_started = 0;
};

#endif // ifdef PIN_VIBRATION
