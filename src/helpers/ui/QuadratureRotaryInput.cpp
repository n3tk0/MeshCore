#include "QuadratureRotaryInput.h"

#if defined(ESP32)
  #define QRI_ISR_ATTR IRAM_ATTR
#else
  #define QRI_ISR_ATTR
#endif

#define QRI_MAX_PENDING  8

QuadratureRotaryInput* QuadratureRotaryInput::_instance = nullptr;

// indexed by (prev_state << 2) | new_state, state = (A << 1) | B
// +1 / -1 for a valid Gray-code step, 0 for no change or an invalid (bounced) jump
static const int8_t QRI_TRANSITIONS[16] = {
   0, -1,  1,  0,
   1,  0,  0, -1,
  -1,  0,  0,  1,
   0,  1, -1,  0
};

bool QuadratureRotaryInput::begin() {
  if (_ready) return true;
  if (_pin_a < 0 || _pin_b < 0) return false;

  pinMode(_pin_a, INPUT_PULLUP);
  pinMode(_pin_b, INPUT_PULLUP);
  delay(1);

  _state = readState();
  _accum = 0;
  _pending = 0;
  _instance = this;

  attachInterrupt(digitalPinToInterrupt(_pin_a), onEdge, CHANGE);
  attachInterrupt(digitalPinToInterrupt(_pin_b), onEdge, CHANGE);

  _ready = true;
  return true;
}

uint8_t QuadratureRotaryInput::readState() const {
  return (digitalRead(_pin_a) ? 0x02 : 0) | (digitalRead(_pin_b) ? 0x01 : 0);
}

QRI_ISR_ATTR void QuadratureRotaryInput::onEdge() {
  if (_instance) _instance->handleEdge();
}

QRI_ISR_ATTR void QuadratureRotaryInput::handleEdge() {
  uint8_t s = readState();
  int8_t acc = _accum + QRI_TRANSITIONS[(_state << 2) | s];
  _state = s;

  int8_t step = 0;
  if (s == 0x03) {
    // detent / rest position: emit if we moved at least half a detent, then resync
    if (acc >= (int8_t)(_steps_per_detent / 2) && acc > 0) step = 1;
    else if (acc <= -(int8_t)(_steps_per_detent / 2) && acc < 0) step = -1;
    acc = 0;
  } else if (acc >= (int8_t)_steps_per_detent) {
    step = 1;
    acc -= _steps_per_detent;
  } else if (acc <= -(int8_t)_steps_per_detent) {
    step = -1;
    acc += _steps_per_detent;
  }
  _accum = acc;

  if (step != 0) {
    int8_t p = _pending + step;
    if (p > QRI_MAX_PENDING) p = QRI_MAX_PENDING;
    if (p < -QRI_MAX_PENDING) p = -QRI_MAX_PENDING;
    _pending = p;
  }
}

RotaryInputEvent QuadratureRotaryInput::poll() {
  if (!_ready) {
    begin();
    return RotaryInputEvent::None;
  }

  int8_t dir = 0;
  noInterrupts();
  if (_pending > 0) { _pending--; dir = 1; }
  else if (_pending < 0) { _pending++; dir = -1; }
  interrupts();

  if (dir == 0) return RotaryInputEvent::None;
  if (_reverse) dir = -dir;
  return dir > 0 ? RotaryInputEvent::Next : RotaryInputEvent::Prev;
}
