#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// Block list by public key prefix. Written for this fork (same style as the ported
// PathBlock.h). An entry is 4..32 bytes (8..64 hex digits) and matches every key that
// starts with it, so a short entry blocks a family of keys: keep entries long. Used by
// the repeater / room server filter and by the companion filter. Header-only and
// dependency-free so it runs on the `native` env.

#define FILTER_BLOCK_COUNT   32
#define FILTER_BLOCK_MIN_LEN 4    // bytes
#define FILTER_BLOCK_MAX_LEN 32   // bytes (a whole public key)

struct BlockEntry {
  uint8_t len;                          // 0 = empty slot
  uint8_t key[FILTER_BLOCK_MAX_LEN];
};

namespace FilterBlock {

  inline int hexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  }

  // 8..64 hex digits, an even number of them. Leaves `out` untouched on failure.
  inline bool parse(const char* hex, BlockEntry* out) {
    if (hex == nullptr || out == nullptr) return false;
    size_t n = strlen(hex);
    if (n < 2 * FILTER_BLOCK_MIN_LEN || n > 2 * FILTER_BLOCK_MAX_LEN || (n & 1)) return false;

    BlockEntry e;
    memset(&e, 0, sizeof(e));
    e.len = (uint8_t)(n / 2);
    for (uint8_t i = 0; i < e.len; i++) {
      int hi = hexVal(hex[2 * i]);
      int lo = hexVal(hex[2 * i + 1]);
      if (hi < 0 || lo < 0) return false;
      e.key[i] = (uint8_t)((hi << 4) | lo);
    }
    *out = e;
    return true;
  }

  // Upper-case hex of the first `max_bytes` bytes of the entry, NUL-terminated.
  // `cap` must hold 2 * max_bytes + 1.
  inline void format(char* out, size_t cap, const BlockEntry& e, uint8_t max_bytes = FILTER_BLOCK_MAX_LEN) {
    if (out == nullptr || cap == 0) return;
    out[0] = '\0';
    size_t len = 0;
    for (uint8_t i = 0; i < e.len && i < max_bytes; i++) {
      if (len + 3 > cap) break;
      snprintf(out + len, cap - len, "%02X", (unsigned)e.key[i]);
      len += 2;
    }
  }

  inline int count(const BlockEntry* list, int n) {
    int c = 0;
    for (int i = 0; i < n; i++) if (list[i].len != 0) c++;
    return c;
  }

  // Slot of the first entry that is a prefix of `key`, or -1. `key_len` is how many
  // bytes of the key are known (a packet carries all 32).
  inline int findMatch(const BlockEntry* list, int n, const uint8_t* key, size_t key_len) {
    if (list == nullptr || key == nullptr) return -1;
    for (int i = 0; i < n; i++) {
      const BlockEntry& e = list[i];
      if (e.len == 0 || e.len > key_len) continue;
      if (memcmp(e.key, key, e.len) == 0) return i;
    }
    return -1;
  }

  // Slot of the first entry that agrees with a PARTIAL key (`known_len` >= 4 bytes of it): the
  // known bytes equal the start of the entry. For a place that only has a short prefix of a key
  // (a discovery answer carries 8 bytes), so a long entry still hides the node it was made from.
  inline int findLoose(const BlockEntry* list, int n, const uint8_t* key, size_t known_len) {
    if (list == nullptr || key == nullptr || known_len < FILTER_BLOCK_MIN_LEN) return -1;
    for (int i = 0; i < n; i++) {
      const BlockEntry& e = list[i];
      if (e.len == 0) continue;
      size_t m = e.len < known_len ? e.len : known_len;
      if (memcmp(e.key, key, m) == 0) return i;
    }
    return -1;
  }

  // Raw add: false when the entry is malformed, already present (same bytes) or the list is full.
  // `slot` receives where it went, or the slot already holding it.
  inline bool addRaw(BlockEntry* list, int n, const uint8_t* key, size_t len, int* slot = nullptr) {
    if (key == nullptr || len < FILTER_BLOCK_MIN_LEN || len > FILTER_BLOCK_MAX_LEN) return false;
    int free_slot = -1;
    for (int i = 0; i < n; i++) {
      if (list[i].len == 0) {
        if (free_slot < 0) free_slot = i;
      } else if (list[i].len == len && memcmp(list[i].key, key, len) == 0) {
        if (slot) *slot = i;
        return false;
      }
    }
    if (free_slot < 0) return false;
    memset(&list[free_slot], 0, sizeof(BlockEntry));
    list[free_slot].len = (uint8_t)len;
    memcpy(list[free_slot].key, key, len);
    if (slot) *slot = free_slot;
    return true;
  }

  // Removes the entry with exactly these bytes. Returns its slot, or -1.
  inline int removeRaw(BlockEntry* list, int n, const uint8_t* key, size_t len) {
    if (key == nullptr) return -1;
    for (int i = 0; i < n; i++) {
      if (list[i].len == len && len != 0 && memcmp(list[i].key, key, len) == 0) {
        memset(&list[i], 0, sizeof(BlockEntry));
        return i;
      }
    }
    return -1;
  }

  // CLI add / remove. `formatted` (2 * FILTER_BLOCK_MAX_LEN + 1 bytes) receives the canonical
  // hex, or "" when `hex` did not parse, so the caller can tell a bad key from a duplicate or a
  // full list.
  inline bool add(BlockEntry* list, int n, const char* hex, char* formatted) {
    formatted[0] = '\0';
    BlockEntry e;
    if (!parse(hex, &e)) return false;
    format(formatted, 2 * FILTER_BLOCK_MAX_LEN + 1, e);
    return addRaw(list, n, e.key, e.len);
  }

  inline bool remove(BlockEntry* list, int n, const char* hex, char* formatted, int* slot = nullptr) {
    formatted[0] = '\0';
    BlockEntry e;
    if (!parse(hex, &e)) return false;
    format(formatted, 2 * FILTER_BLOCK_MAX_LEN + 1, e);
    int s = removeRaw(list, n, e.key, e.len);
    if (slot) *slot = s;
    return s >= 0;
  }

  // Forces every entry into a sane shape after a load.
  inline void sanitize(BlockEntry* list, int n) {
    for (int i = 0; i < n; i++) {
      if (list[i].len != 0 && (list[i].len < FILTER_BLOCK_MIN_LEN || list[i].len > FILTER_BLOCK_MAX_LEN)) {
        memset(&list[i], 0, sizeof(BlockEntry));
      }
    }
  }
}
