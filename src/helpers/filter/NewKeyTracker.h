#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "MessageAge.h"

// "First seen" memory for public keys, and a small quota for keys that are still new.
// Written for this fork.
//
// A throw-away key costs an attacker nothing, so a flood of adverts or anonymous requests
// always comes from keys nobody has seen before. This remembers when each key (its first 4
// bytes) was first seen in an advert or an anonymous request, up to 256 keys. A key first
// seen less than `hours` ago is "new": its flood adverts are forwarded at most once per
// `mins` minutes and its anonymous requests at most once a minute. Older keys, and keys the
// node knows itself (its own ACL / contacts, decided by the caller), are never limited.
//
// The table is saved to flash by the caller (serialize / load), at most once an hour and only
// when it changed. Nothing is recorded while the clock is not set: the hour numbers would mean
// nothing once it is. Header-only and dependency-free so it runs on the `native` env.

#ifndef NEWKEY_CAP
  #if defined(STM32_PLATFORM)
    #define NEWKEY_CAP    64       // small RAM
  #else
    #define NEWKEY_CAP    256
  #endif
#endif
#define NEWKEY_KEY_LEN    4
#define NEWKEY_MAX_HOURS  720          // 30 days
#define NEWKEY_MAX_MINS   1440
#define NEWKEY_ANON_SECS  60           // anonymous requests of a new key: one per this many seconds
#define NEWKEY_SAVE_MS    3600000UL    // at most one flash write an hour
#define NEWKEY_FILE_MAGIC 0x314B4E46   // "FNK1"

class NewKeyTracker {
public:
  struct Entry {
    uint8_t key[NEWKEY_KEY_LEN];
    uint32_t first_hour;     // RTC seconds / 3600 of the first sighting (saved)
    uint32_t last_advert;    // RTC seconds of the last forwarded advert (RAM only, 0 = none)
    uint32_t last_anon;      // ... and of the last forwarded anonymous request
  };

  enum Kind { ADVERT, ANON };

private:
  Entry _e[NEWKEY_CAP];
  uint16_t _n;
  uint16_t _hours, _mins;    // hours 0 = off
  bool _dirty;
  uint32_t _last_save_ms;

  int find(const uint8_t* key) const {
    for (int i = 0; i < _n; i++) if (memcmp(_e[i].key, key, NEWKEY_KEY_LEN) == 0) return i;
    return -1;
  }

public:
  NewKeyTracker() : _n(0), _hours(0), _mins(60), _dirty(false), _last_save_ms(0) { memset(_e, 0, sizeof(_e)); }

  void configure(uint16_t hours, uint16_t mins) {
    _hours = hours > NEWKEY_MAX_HOURS ? NEWKEY_MAX_HOURS : hours;
    _mins = mins == 0 ? 60 : (mins > NEWKEY_MAX_MINS ? NEWKEY_MAX_MINS : mins);
  }
  uint16_t hours() const { return _hours; }
  uint16_t mins() const { return _mins; }
  bool active() const { return _hours != 0; }
  int count() const { return _n; }
  bool dirty() const { return _dirty; }

  void clear() {
    _n = 0;
    memset(_e, 0, sizeof(_e));
    _dirty = true;
  }

  // The caller says the node starts counting its hour now (boot): the first write is an hour away.
  void setSaveClock(uint32_t now_ms) { _last_save_ms = now_ms; }

  // True when a write is due: something changed and an hour passed since the last write.
  bool saveDue(uint32_t now_ms) const { return _dirty && (uint32_t)(now_ms - _last_save_ms) >= NEWKEY_SAVE_MS; }
  void markSaved(uint32_t now_ms) { _dirty = false; _last_save_ms = now_ms; }
  void retryLater(uint32_t now_ms) { _last_save_ms = now_ms; }   // a write failed: still dirty, try in an hour

  // How long ago the key was first seen, in hours; -1 = not in the table.
  int ageHours(const uint8_t* key, uint32_t now_secs) const {
    int i = find(key);
    if (i < 0) return -1;
    uint32_t h = now_secs / 3600UL;
    return h > _e[i].first_hour ? (int)(h - _e[i].first_hour) : 0;
  }

  // The quota check. `key` is the first 4 bytes of the sender's public key, `known` says the
  // node itself knows the key. Returns true when the packet may be forwarded.
  bool allow(const uint8_t* key, Kind kind, bool known, uint32_t now_secs) {
    if (_hours == 0 || key == nullptr || known) return true;
    if (!MessageAge::clockSet(now_secs)) return true;   // no usable time: nothing to compare

    uint32_t hour = now_secs / 3600UL;
    int i = find(key);
    if (i < 0) {
      // new row; the oldest sighting makes room when the table is full
      if (_n < NEWKEY_CAP) {
        i = _n++;
      } else {
        i = 0;
        for (int k = 1; k < NEWKEY_CAP; k++) if (_e[k].first_hour < _e[i].first_hour) i = k;
      }
      memset(&_e[i], 0, sizeof(Entry));
      memcpy(_e[i].key, key, NEWKEY_KEY_LEN);
      _e[i].first_hour = hour;
      _dirty = true;
    } else if (_e[i].first_hour > hour) {   // the clock went back: count from now
      _e[i].first_hour = hour;
      _dirty = true;
    }

    Entry& e = _e[i];
    if (hour - e.first_hour >= _hours) return true;   // an old key

    // a new key: a small quota
    uint32_t& last = kind == ADVERT ? e.last_advert : e.last_anon;
    uint32_t gap = kind == ADVERT ? (uint32_t)_mins * 60UL : (uint32_t)NEWKEY_ANON_SECS;
    if (last != 0 && now_secs >= last && now_secs - last < gap) return false;
    last = now_secs;
    return true;
  }

  // ---- persistence: [magic u32][count u16] then count x ([key 4][first_hour u32]), little endian ----

  static size_t fileSize(int count) { return 6 + (size_t)count * 8; }
  size_t serializedSize() const { return fileSize(_n); }

  // Streaming form (a file is written and read in small pieces, no 2 KB buffer on the stack).
  void header(uint8_t out[6]) const {
    uint32_t magic = NEWKEY_FILE_MAGIC;
    out[0] = magic; out[1] = magic >> 8; out[2] = magic >> 16; out[3] = magic >> 24;
    out[4] = _n; out[5] = _n >> 8;
  }
  void entry(int i, uint8_t out[8]) const {
    memcpy(out, _e[i].key, NEWKEY_KEY_LEN);
    uint32_t h = _e[i].first_hour;
    out[4] = h; out[5] = h >> 8; out[6] = h >> 16; out[7] = h >> 24;
  }
  // Starts a load: empties the table and checks the header. Returns the entry count, or -1.
  int loadBegin(const uint8_t hdr[6]) {
    _n = 0;
    memset(_e, 0, sizeof(_e));
    _dirty = false;
    uint32_t magic = hdr[0] | (hdr[1] << 8) | (hdr[2] << 16) | ((uint32_t)hdr[3] << 24);
    uint32_t n = hdr[4] | (hdr[5] << 8);
    if (magic != NEWKEY_FILE_MAGIC || n > NEWKEY_CAP) return -1;
    return (int)n;
  }
  void loadEntry(const uint8_t raw[8]) {
    if (_n >= NEWKEY_CAP) return;
    memcpy(_e[_n].key, raw, NEWKEY_KEY_LEN);
    _e[_n].first_hour = raw[4] | (raw[5] << 8) | (raw[6] << 16) | ((uint32_t)raw[7] << 24);
    _n++;
  }

  // Whole-buffer forms (tests).
  size_t serialize(uint8_t* out) const {
    header(out);
    for (int i = 0; i < _n; i++) entry(i, &out[6 + (size_t)i * 8]);
    return fileSize(_n);
  }
  bool load(const uint8_t* in, size_t len) {
    if (in == nullptr || len < 6) { loadBegin((const uint8_t*)"\0\0\0\0\0\0"); return false; }
    int n = loadBegin(in);
    if (n < 0 || len < fileSize(n)) { _n = 0; memset(_e, 0, sizeof(_e)); return false; }
    for (int i = 0; i < n; i++) loadEntry(&in[6 + (size_t)i * 8]);
    return true;
  }
};
