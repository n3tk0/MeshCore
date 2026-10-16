#pragma once

#include <stdint.h>

// Two push buttons wired to an encoder's A and B pins instead of the encoder, each to GND with
// the same internal pull-ups: A alone low = previous, B alone low = next. A turning encoder always
// pulls both pins low on its way through a detent, so a press during which only one pin went low
// is a button. No Arduino calls here, so the decoding can be unit tested on the host.
class NavButtonDecoder {
public:
  enum : uint8_t { PIN_A = 0x02, PIN_B = 0x01 };   // bits of state = (A << 1) | B, 1 = high
  enum : uint16_t {
    SETTLE_MS = 25,          // both pins high this long: the press is over
    REPEAT_DELAY_MS = 400,   // held: first repeat
    REPEAT_MS = 150,         // then this often
    REPEAT_FAST_MS = 70,     // and faster after a while
    REPEAT_FAST_AFTER = 8,
    SWITCH_HOLD_MS = 400,    // encoder mode: one pin held low alone this long is a button
    SWITCH_STEPS_MS = 1000,  // buttons mode: two full Gray-code steps the same way this close are an encoder
  };

  void reset() { _low = 0; _edge = false; _down = false; _alone = 0x03; _step_dir = 0; }

  // Encoder mode, from the main loop with the current pin state: true once one pin has stayed low
  // on its own for SWITCH_HOLD_MS. A detented encoder rests with both pins high (full step) or
  // both equal (half step), so only a held button keeps a single pin down.
  bool heldAlone(uint8_t state, uint32_t now) {
    if (state != PIN_A && state != PIN_B) { _alone = 0x03; return false; }
    if (state != _alone) { _alone = state; _alone_ms = now; return false; }
    return (uint32_t)(now - _alone_ms) >= SWITCH_HOLD_MS;
  }

  // Buttons mode: a full Gray-code step in direction dir (+1 / -1). True on the second step the
  // same way within SWITCH_STEPS_MS: a turning encoder. Two buttons pressed over each other make
  // at most one such step.
  bool encoderStep(int8_t dir, uint32_t now) {
    if (dir == _step_dir && (uint32_t)(now - _step_ms) <= SWITCH_STEPS_MS) { _step_dir = 0; return true; }
    _step_dir = dir;
    _step_ms = now;
    return false;
  }

  // a pin change, from the interrupt: remember which pins were low during this press
  void onEdge(uint8_t state) { _low |= (uint8_t)(~state) & 0x03; _edge = true; }

  // from the main loop with the current pin state; the caller keeps onEdge() out while it runs.
  // emit_on_press: buttons are known, so a press answers at once and repeats while held.
  // Otherwise (still finding out) a press counts only on release, when it is certain that no
  // encoder went on to pull the other pin too.
  // Returns -1 (button A, previous), +1 (button B, next) or 0.
  int8_t poll(uint8_t state, uint32_t now, bool emit_on_press) {
    if (_edge) { _edge = false; _edge_ms = now; }
    uint8_t low = _low;
    bool one = (low == PIN_A || low == PIN_B);   // a single pin went low: a button
    int8_t dir = (low == PIN_A) ? -1 : 1;

    if (state == 0x03 && (uint32_t)(now - _edge_ms) >= SETTLE_MS) {   // released and settled
      int8_t r = 0;
      if (one && (!emit_on_press || !_down)) r = dir;   // on release; or a tap the loop never saw held
      _low = 0;
      _down = false;
      return r;
    }
    if (!emit_on_press || !one) return 0;   // finding out, or both pins: an encoder or two buttons
    if (!_down) {   // first edge counts; bounce until it settles belongs to this press
      _down = true;
      _repeats = 0;
      _next_ms = now + REPEAT_DELAY_MS;
      return dir;
    }
    if (state != 0x03 && (int32_t)(now - _next_ms) >= 0) {   // still held
      if (_repeats < 255) _repeats++;
      _next_ms = now + (_repeats >= REPEAT_FAST_AFTER ? REPEAT_FAST_MS : REPEAT_MS);
      return dir;
    }
    return 0;
  }

private:
  volatile uint8_t _low = 0;    // pins seen low since the last release, written by the interrupt
  volatile bool _edge = false;  // a pin changed since the last poll
  uint32_t _edge_ms = 0;        // when poll() last noticed a change
  bool _down = false;           // this press was answered (buttons known)
  uint8_t _repeats = 0;
  uint32_t _next_ms = 0;
  uint8_t _alone = 0x03;        // heldAlone(): the single-pin-low state being timed
  uint32_t _alone_ms = 0;
  int8_t _step_dir = 0;         // encoderStep(): the last step, 0 = none
  uint32_t _step_ms = 0;
};
