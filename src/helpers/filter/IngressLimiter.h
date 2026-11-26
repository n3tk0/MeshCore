#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "Limiter.h"

// Per entry-repeater limit on flood packets. Written for this fork.
//
// A flood packet's path lists the repeaters it went through, the first one being the one
// that took the packet from its originator. A node spamming the mesh is therefore always
// seen behind the same first path entry. This limits how many flood packets that entry
// repeater may inject per window: `limit` packets per `secs` seconds, with the same optional
// soft cutoff as the ported Limiter. A packet heard directly from its originator has an empty
// path: all of those share the key "direct".
//
// RAM only: a small table with least-recently-used replacement. Header-only and
// dependency-free so it runs on the `native` env.

#define INGRESS_TABLE_SIZE  32
#define INGRESS_KEY_MAX      4    // bytes of the first path entry (hash size is 1..4)
#define INGRESS_TOP_COUNT    5    // keys `filter stats ingress` lists

class IngressLimiter {
public:
  struct Entry {
    uint8_t key[INGRESS_KEY_MAX];
    uint8_t key_len;       // 0 = free slot, 0xFF = "direct" (empty path)
    uint16_t drops;
    uint32_t last_used;    // millis
    Limiter limiter;
  };
  enum : uint8_t { KEY_DIRECT = 0xFF };

private:
  Entry _t[INGRESS_TABLE_SIZE];
  uint16_t _limit, _secs, _soft;   // limit 0 = off

  int find(const uint8_t* key, uint8_t key_len) const {
    for (int i = 0; i < INGRESS_TABLE_SIZE; i++) {
      const Entry& e = _t[i];
      if (e.key_len != key_len || e.key_len == 0) continue;
      if (key_len == KEY_DIRECT || memcmp(e.key, key, key_len) == 0) return i;
    }
    return -1;
  }

public:
  IngressLimiter() : _limit(0), _secs(60), _soft(0) { memset(_t, 0, sizeof(_t)); }

  void configure(uint16_t limit, uint16_t secs, uint16_t soft) {
    _limit = limit;
    _secs = secs ? secs : 60;
    _soft = (soft != 0 && soft < limit) ? soft : 0;
    clear();
  }
  uint16_t limit() const { return _limit; }
  uint16_t secs() const { return _secs; }
  uint16_t soft() const { return _soft; }
  bool active() const { return _limit != 0; }

  void clear() {
    for (int i = 0; i < INGRESS_TABLE_SIZE; i++) {
      memset(&_t[i], 0, sizeof(Entry));
    }
  }

  int used() const {
    int n = 0;
    for (int i = 0; i < INGRESS_TABLE_SIZE; i++) if (_t[i].key_len) n++;
    return n;
  }

  // The key of a flood packet: first path entry, or "direct" for an empty path.
  // `path` has `count` entries of `hash_size` bytes each.
  static void keyOf(const uint8_t* path, uint8_t hash_size, uint8_t count, uint8_t* key, uint8_t* key_len) {
    memset(key, 0, INGRESS_KEY_MAX);
    if (count == 0 || path == nullptr || hash_size == 0) {
      *key_len = KEY_DIRECT;
      return;
    }
    uint8_t n = hash_size > INGRESS_KEY_MAX ? INGRESS_KEY_MAX : hash_size;
    memcpy(key, path, n);
    *key_len = n;
  }

  // True when the packet may be forwarded. now_ms: millis(); rnd: 0..255 for the soft band.
  bool allow(const uint8_t* key, uint8_t key_len, uint32_t now_ms, uint8_t rnd) {
    if (_limit == 0 || key_len == 0) return true;

    int i = find(key, key_len);
    if (i < 0) {
      // free slot, else the least recently used one
      int victim = 0;
      for (int k = 0; k < INGRESS_TABLE_SIZE; k++) {
        if (_t[k].key_len == 0) { victim = k; break; }
        if ((uint32_t)(now_ms - _t[k].last_used) > (uint32_t)(now_ms - _t[victim].last_used)) victim = k;
      }
      i = victim;
      Entry& e = _t[i];
      memset(&e, 0, sizeof(e));
      memcpy(e.key, key, key_len == KEY_DIRECT ? 0 : key_len);
      e.key_len = key_len;
      e.limiter.init(_limit, _secs, _soft);
    }
    Entry& e = _t[i];
    e.last_used = now_ms;
    bool ok = e.limiter.allow(now_ms / 1000UL, rnd);
    if (!ok && e.drops < 0xFFFF) e.drops++;
    return ok;
  }

  struct Top {
    uint8_t key[INGRESS_KEY_MAX];
    uint8_t key_len;
    uint16_t drops;
  };

  // The keys with the most drops, highest first (ties: table order). Returns how many.
  int top(Top* out, int max) const {
    if (max > INGRESS_TOP_COUNT) max = INGRESS_TOP_COUNT;
    bool taken[INGRESS_TABLE_SIZE] = {};
    int n = 0;
    while (n < max) {
      int best = -1;
      for (int i = 0; i < INGRESS_TABLE_SIZE; i++) {
        if (taken[i] || _t[i].key_len == 0 || _t[i].drops == 0) continue;
        if (best < 0 || _t[i].drops > _t[best].drops) best = i;
      }
      if (best < 0) break;
      taken[best] = true;
      memcpy(out[n].key, _t[best].key, INGRESS_KEY_MAX);
      out[n].key_len = _t[best].key_len;
      out[n].drops = _t[best].drops;
      n++;
    }
    return n;
  }
};
