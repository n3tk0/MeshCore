#ifdef PIN_VIBRATION
#include "GenericVibration.h"

void GenericVibration::drive(bool on) {
#ifdef VIBRATION_PWM
  if (on) analogWrite(PIN_VIBRATION, VIBRATION_PWM);
  else { analogWrite(PIN_VIBRATION, 0); pinMode(PIN_VIBRATION, OUTPUT); digitalWrite(PIN_VIBRATION, LOW); }
#else
  digitalWrite(PIN_VIBRATION, on ? HIGH : LOW);
#endif
}

void GenericVibration::begin() {
  pinMode(PIN_VIBRATION, OUTPUT);
  digitalWrite(PIN_VIBRATION, LOW);
  duration = 0;
  pulse_until = 0;
}

void GenericVibration::trigger() {
  pulse_until = 0;
  duration = millis();
  pattern_on = true;
  drive(true);
}

void GenericVibration::pulse(uint16_t millis_on) {
  if (isVibrating() || millis_on == 0) return;
  unsigned long until = millis() + millis_on;
  if (until == 0) until = 1;
  if (pulse_until && (long)(pulse_until - until) >= 0) return;   // longer pulse already running
  pulse_until = until;
  drive(true);
}

void GenericVibration::loop() {
  if (pulse_until && (long)(millis() - pulse_until) >= 0) {
    pulse_until = 0;
    if (!isVibrating()) drive(false);
  }
  if (isVibrating()) {
    bool on = ((millis() - duration) / 1000) % 2 == 0;   // 1 s on / 1 s off from trigger()
    if (on != pattern_on) { drive(on); pattern_on = on; }

    if (millis() - duration > VIBRATION_TIMEOUT) {
      stop();
    }
  }
}

bool GenericVibration::isVibrating() {
  return duration > 0;
}

void GenericVibration::stop() {
  duration = 0;
  pulse_until = 0;
  drive(false);
}

#endif // ifdef PIN_VIBRATION
