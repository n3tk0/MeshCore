#pragma once

#include <Arduino.h>
#include <helpers/ui/RotaryInput.h>

// Mechanical quadrature encoder wired directly to two GPIOs (common pin to GND,
// internal pull-ups). Edges are decoded in interrupts with a transition table,
// so bounce is rejected and steps are not lost while the main loop is busy.
// Only one instance is supported.
class QuadratureRotaryInput : public RotaryInput {
public:
  QuadratureRotaryInput(int8_t pin_a, int8_t pin_b, uint8_t steps_per_detent = 4, bool reverse = false)
    : _pin_a(pin_a), _pin_b(pin_b), _steps_per_detent(steps_per_detent ? steps_per_detent : 4), _reverse(reverse) { }

  bool begin() override;
  RotaryInputEvent poll() override;
  bool isReady() const override { return _ready; }

private:
  static void onEdge();
  void handleEdge();
  uint8_t readState() const;

  int8_t _pin_a, _pin_b;
  uint8_t _steps_per_detent;
  bool _reverse;
  bool _ready = false;

  volatile uint8_t _state = 0x03;
  volatile int8_t _accum = 0;
  volatile int8_t _pending = 0;

  static QuadratureRotaryInput* _instance;
};
