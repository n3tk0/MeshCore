#pragma once

// Ported from Dutch-MeshCore (github.com/Dutch-MeshCore/MeshCore, dmc-dev), MIT.
// Copyright (c) 2025 Scott Powell / rippleradios.com, and the Dutch-MeshCore contributors
// (Elektr0Vodka, Marco, efiten). MIT licence: see license.txt in the repository root.
// Modified for this fork (see docs/packet_filter.md).


#include <stdint.h>
#include <string.h>

// Per-origin advert rate limiter: each node's flood advert is forwarded at
// most once per window, so a node that re-advertises every few minutes stops
// being re-flooded by this repeater while it still stays reachable through it
// once per window. Complements the per-type rate limiter, which caps the total
// advert budget and would otherwise drop legitimate nodes together with the
// chatty one.
//
// Origins are keyed on the first 4 bytes of the advert's public key. The cache
// is a ring of fixed size: once full, the oldest entry is overwritten. Time is
// kept as seconds of a non-wrapping uptime counter fed by millis(), so an old entry never looks
// recent after the 49-day millis wrap. Header-only and dependency-free so it runs on the `native` env.

#ifndef ADVERT_CACHE_SIZE
  #if defined(STM32_PLATFORM)
    #define ADVERT_CACHE_SIZE 64    // small RAM
  #else
    #define ADVERT_CACHE_SIZE 256
  #endif
#endif
#define ADVERT_KEY_LEN    4
#define ADVERT_MAX_HOURS  720   // 30 days; keeps hours*3600*1000 inside uint32

class AdvertLimiter {
  struct Entry {
    uint8_t key[ADVERT_KEY_LEN];
    uint32_t seen_s;   // seconds of the limiter's own uptime counter
  };

  Entry _cache[ADVERT_CACHE_SIZE];
  uint16_t _count;   // entries in use
  uint16_t _head;    // next entry to overwrite once the cache is full
  uint16_t _hours;   // 0 = off

  // A 64-bit uptime that does not wrap, built from the 32-bit millis() the caller passes in. An
  // entry stamped in raw millis would look recent again after the 49.7 day wrap; seconds of this
  // counter do not. It must see a time at least once every 49 days (tick() is called per packet).
  uint64_t _up_ms;
  uint32_t _last_ms;
  bool _have_last;

  uint32_t upSeconds() const { return (uint32_t)(_up_ms / 1000ULL); }

public:
  AdvertLimiter() : _count(0), _head(0), _hours(0), _up_ms(0), _last_ms(0), _have_last(false) {
    memset(_cache, 0, sizeof(_cache));
  }

  void setWindowHours(uint16_t hours) { _hours = hours > ADVERT_MAX_HOURS ? ADVERT_MAX_HOURS : hours; }
  uint16_t getWindowHours() const { return _hours; }
  int getCount() const { return _count; }
  int getCapacity() const { return ADVERT_CACHE_SIZE; }

  void clear() {
    _count = 0;
    _head = 0;
  }

  // Feeds the clock. Cheap; call with every millis() reading that is at hand.
  void tick(uint32_t now_ms) {
    if (_have_last) _up_ms += (uint32_t)(now_ms - _last_ms);   // unsigned: wrap-safe
    _last_ms = now_ms;
    _have_last = true;
  }

  // True when the advert from `key` may be forwarded now, false when the same origin already passed
  // inside the current window. `stamp` false only looks (nothing is recorded); the caller records
  // with a second call once the packet is really forwarded.
  bool allow(const uint8_t* key, uint32_t now_ms, bool stamp = true) {
    tick(now_ms);
    if (_hours == 0 || key == nullptr) return true;

    uint32_t window_s = (uint32_t)_hours * 3600UL;
    uint32_t now_s = upSeconds();

    for (int i = 0; i < _count; i++) {
      Entry& e = _cache[i];
      if (memcmp(e.key, key, ADVERT_KEY_LEN) != 0) continue;
      if (now_s - e.seen_s < window_s) return false;
      if (stamp) e.seen_s = now_s;   // window elapsed: pass, and start a new window
      return true;
    }
    if (!stamp) return true;

    Entry* e;
    if (_count < ADVERT_CACHE_SIZE) {
      e = &_cache[_count++];
    } else {
      e = &_cache[_head];   // overwrite the oldest entry
      _head = (uint16_t)((_head + 1) % ADVERT_CACHE_SIZE);
    }
    memcpy(e->key, key, ADVERT_KEY_LEN);
    e->seen_s = now_s;
    return true;
  }
};
