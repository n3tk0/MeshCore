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
    SWITCH_TAPS_MS = 2000,   // encoder mode: two one-pin presses this close are two buttons
    SWITCH_STEPS_MS = 1000,  // buttons mode: two full Gray-code steps the same way this close are an encoder
  };

  void reset() { _low = 0; _edge = false; _down = false; _tap_seen = false; _step_dir = 0; }

  // Encoder mode: a press that pulled only one pin low and let it go again (poll() with
  // emit_on_press false gave non-zero). True on the second one within SWITCH_TAPS_MS: two buttons.
  // A single one can be an encoder nudged half a click and let back, and an encoder that rests
  // between detents never lets go, so neither switches.
  bool buttonTap(uint32_t now) {
    if (_tap_seen && (uint32_t)(now - _tap_ms) <= SWITCH_TAPS_MS) { _tap_seen = false; return true; }
    _tap_seen = true;
    _tap_ms = now;
    return false;
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
  bool _tap_seen = false;       // buttonTap(): a one-pin press was seen at _tap_ms
  uint32_t _tap_ms = 0;
  int8_t _step_dir = 0;         // encoderStep(): the last step, 0 = none
  uint32_t _step_ms = 0;
};
