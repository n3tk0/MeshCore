#pragma once

#include <Arduino.h>

enum class RotaryInputEvent : uint8_t {
  None,
  Next,
  Prev,
};

// what is on the encoder pins: a rotary encoder, or two push buttons (previous / next).
// Detect accepts both and settles on whichever is used first.
enum class RotaryInputMode : uint8_t {
  Detect,
  Encoder,
  Buttons,
};

class RotaryInput {
public:
  virtual ~RotaryInput() = default;

  virtual bool begin() = 0;
  virtual RotaryInputEvent poll() = 0;
  virtual bool isReady() const = 0;
  virtual void end() { }   // release pins/interrupts, e.g. before system off
  virtual void setReverse(bool reverse) { }
  virtual void setStepsPerDetent(uint8_t steps) { }
  virtual void setMode(RotaryInputMode mode) { }
  virtual RotaryInputMode mode() const { return RotaryInputMode::Encoder; }
};
