#pragma once

#ifdef PIN_VIBRATION

#include <Arduino.h>

/*
 * Vibration motor control class
 *
 * Provides vibration feedback for events like new messages and new contacts
 * Features:
 * - 1-second vibration pulse
 * - 5-second nag timeout (cooldown between vibrations)
 * - Non-blocking operation
 * - pulse(): single short haptic tick (UI feedback)
 * - VIBRATION_PWM (0-255): optional duty cycle, e.g. to run a 3V motor from a 4.2V LiPo
 */

#ifndef VIBRATION_TIMEOUT
#define VIBRATION_TIMEOUT 5000 // 5 seconds default
#endif

class GenericVibration {
public:
  void begin();       // set up vibration pin
  void trigger();     // trigger vibration if cooldown has passed
  void loop();        // non-blocking timer handling
  bool isVibrating(); // returns true if currently vibrating
  void stop();        // stop vibration immediately
  void pulse(uint16_t millis_on);  // one short pulse, ignored while the notification pattern runs

private:
  void drive(bool on);
  unsigned long duration;
  unsigned long pulse_until = 0;
  bool pattern_on = false;
};

#endif // ifdef PIN_VIBRATION
