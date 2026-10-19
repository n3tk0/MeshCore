#pragma once

#include <stdint.h>
#include <string.h>

// Store & forward for direct messages to a few "owner" nodes (a repeater keeps them while the
// owner is away and hands them over when it hears the owner again with no hop between).
// The repeater cannot read the messages: they stay encrypted to the owner. A TXT_MSG payload starts
// with dest_hash(1) + src_hash(1), so a message is kept by the owner's first public key byte.
// Pure logic, no radio or file system: MyMesh feeds it packets and saves toBytes()/fromBytes().

#ifndef MAILBOX_MAX
  #define MAILBOX_MAX            64     // stored messages, all owners together
#endif
#define MAILBOX_MAX_OWNERS        4
#define MAILBOX_KEY_SIZE         32     // PUB_KEY_SIZE
#define MAILBOX_HASH_SIZE         8     // MAX_HASH_SIZE
#define MAILBOX_PAYLOAD_MAX     184     // MAX_PACKET_PAYLOAD
#define MAILBOX_RETRY_SECS       30     // same sender and length this soon after = a retry, keep the newest
#define MAILBOX_DELIVERED_SECS  120     // owner's PATH reply to the sender this soon after = it got the message
#define MAILBOX_FILE_VER          1

struct MailboxEntry {
  uint32_t stored_at;                  // RTC seconds
  uint8_t  owner;                      // index into owners
  uint8_t  len;                        // payload length
  uint8_t  hash[MAILBOX_HASH_SIZE];    // packet hash: the same packet heard twice is kept once
  uint8_t  payload[MAILBOX_PAYLOAD_MAX];
};

struct MailboxConfig {
  uint8_t  enabled;
  uint8_t  num_owners;
  uint16_t ttl_hours;                  // drop messages older than this
  uint16_t away_mins;                  // owner heard with no hop this recently = present, nothing is kept
  uint8_t  owners[MAILBOX_MAX_OWNERS][MAILBOX_KEY_SIZE];
};

class MailboxStore {
  MailboxConfig _cfg;
  MailboxEntry  _items[MAILBOX_MAX];   // oldest first
  int           _count;
  uint32_t      _heard[MAILBOX_MAX_OWNERS];   // RTC seconds the owner was last heard with no hop, 0 = not since boot

  void removeAt(int i) {
    for (int k = i; k < _count - 1; k++) _items[k] = _items[k + 1];
    _count--;
  }
  void removeOwnerItems(int owner) {
    for (int i = _count - 1; i >= 0; i--) if (_items[i].owner == owner) removeAt(i);
  }
  static bool isMessage(uint8_t payload_type, int len) {
    return payload_type == 0x02 && len > 2 + 2 && len <= MAILBOX_PAYLOAD_MAX;   // TXT_MSG: hashes + MAC + data
  }

public:
  MailboxStore() { reset(); }

  void reset() {
    memset(&_cfg, 0, sizeof(_cfg));
    _cfg.ttl_hours = 72;
    _cfg.away_mins = 10;
    _count = 0;
    memset(_heard, 0, sizeof(_heard));
  }

  MailboxConfig& config() { return _cfg; }
  bool isEnabled() const { return _cfg.enabled && _cfg.num_owners > 0; }
  int  count() const { return _count; }
  int  countFor(int owner) const {
    int n = 0;
    for (int i = 0; i < _count; i++) if (_items[i].owner == owner) n++;
    return n;
  }
  const MailboxEntry& at(int i) const { return _items[i]; }

  int findOwner(const uint8_t* pub_key, int key_len) const {
    for (int i = 0; i < _cfg.num_owners; i++) {
      if (memcmp(_cfg.owners[i], pub_key, key_len) == 0) return i;
    }
    return -1;
  }
  bool addOwner(const uint8_t* pub_key) {
    if (findOwner(pub_key, MAILBOX_KEY_SIZE) >= 0) return true;
    if (_cfg.num_owners >= MAILBOX_MAX_OWNERS) return false;
    memcpy(_cfg.owners[_cfg.num_owners], pub_key, MAILBOX_KEY_SIZE);
    _heard[_cfg.num_owners] = 0;
    _cfg.num_owners++;
    return true;
  }
  bool removeOwner(int owner) {   // its stored messages go too
    if (owner < 0 || owner >= _cfg.num_owners) return false;
    removeOwnerItems(owner);
    for (int i = 0; i < _count; i++) if (_items[i].owner > owner) _items[i].owner--;
    for (int k = owner; k < _cfg.num_owners - 1; k++) {
      memcpy(_cfg.owners[k], _cfg.owners[k + 1], MAILBOX_KEY_SIZE);
      _heard[k] = _heard[k + 1];
    }
    _cfg.num_owners--;
    return true;
  }

  // the owner was heard with no hop between (its advert, or a flood packet it just sent)
  void markHeard(int owner, uint32_t now) { if (owner >= 0 && owner < MAILBOX_MAX_OWNERS) _heard[owner] = now ? now : 1; }
  bool isPresent(int owner, uint32_t now) const {
    uint32_t h = _heard[owner];
    return h != 0 && now >= h && now - h < (uint32_t)_cfg.away_mins * 60;
  }

  // A packet heard on air. Returns true when it was stored (the caller then saves).
  bool onPacket(uint8_t payload_type, const uint8_t* payload, int len, const uint8_t* pkt_hash, uint32_t now) {
    if (!isEnabled() || !isMessage(payload_type, len)) return false;
    int owner = -1;
    for (int i = 0; i < _cfg.num_owners; i++) {
      if (_cfg.owners[i][0] == payload[0]) { owner = i; break; }
    }
    if (owner < 0 || isPresent(owner, now)) return false;

    for (int i = 0; i < _count; i++) {
      if (memcmp(_items[i].hash, pkt_hash, MAILBOX_HASH_SIZE) == 0) return false;   // already kept
    }
    // a sender retries the same message (other packet hash, same length): keep only the newest
    for (int i = _count - 1; i >= 0; i--) {
      const MailboxEntry& e = _items[i];
      if (e.owner == owner && e.payload[1] == payload[1] && e.len == len
          && now >= e.stored_at && now - e.stored_at < MAILBOX_RETRY_SECS) {
        removeAt(i);
        break;
      }
    }
    if (_count >= MAILBOX_MAX) removeAt(0);   // full: the oldest goes

    MailboxEntry& e = _items[_count++];
    e.stored_at = now;
    e.owner = owner;
    e.len = len;
    memcpy(e.hash, pkt_hash, MAILBOX_HASH_SIZE);
    memcpy(e.payload, payload, len);
    return true;
  }

  // The owner answered a flood message with a PATH packet: it got it, nothing to keep.
  // payload: dest_hash (the sender) + src_hash (the owner). Returns true when something was removed.
  bool onPathReply(const uint8_t* payload, int len, uint32_t now) {
    if (!isEnabled() || len < 2) return false;
    bool removed = false;
    for (int i = _count - 1; i >= 0; i--) {
      const MailboxEntry& e = _items[i];
      if (_cfg.owners[e.owner][0] == payload[1] && e.payload[1] == payload[0]
          && now >= e.stored_at && now - e.stored_at < MAILBOX_DELIVERED_SECS) {
        removeAt(i);
        removed = true;
      }
    }
    return removed;
  }

  // drop messages older than ttl_hours; returns true when something was removed
  bool expire(uint32_t now) {
    bool removed = false;
    uint32_t ttl = (uint32_t)_cfg.ttl_hours * 3600;
    for (int i = _count - 1; i >= 0; i--) {
      if (now > _items[i].stored_at && now - _items[i].stored_at > ttl) { removeAt(i); removed = true; }
    }
    return removed;
  }

  // oldest message for this owner, -1 when none
  int nextFor(int owner) const {
    for (int i = 0; i < _count; i++) if (_items[i].owner == owner) return i;
    return -1;
  }
  void remove(int i) { if (i >= 0 && i < _count) removeAt(i); }
  void clear() { _count = 0; }

  // file image: ver(1) + config + count(2) + entries (only 'len' bytes of each payload)
  static constexpr int maxBytes() { return 1 + sizeof(MailboxConfig) + 2 + MAILBOX_MAX * (4 + 1 + 1 + MAILBOX_HASH_SIZE + MAILBOX_PAYLOAD_MAX); }

  int toBytes(uint8_t* out) const {
    int n = 0;
    out[n++] = MAILBOX_FILE_VER;
    memcpy(&out[n], &_cfg, sizeof(_cfg)); n += sizeof(_cfg);
    out[n++] = _count & 0xFF;
    out[n++] = _count >> 8;
    for (int i = 0; i < _count; i++) {
      const MailboxEntry& e = _items[i];
      memcpy(&out[n], &e.stored_at, 4); n += 4;
      out[n++] = e.owner;
      out[n++] = e.len;
      memcpy(&out[n], e.hash, MAILBOX_HASH_SIZE); n += MAILBOX_HASH_SIZE;
      memcpy(&out[n], e.payload, e.len); n += e.len;
    }
    return n;
  }

  // a damaged or foreign file leaves the mailbox empty and off
  bool fromBytes(const uint8_t* in, int len) {
    reset();
    int n = 0;
    if (len < 1 + (int)sizeof(MailboxConfig) + 2 || in[n++] != MAILBOX_FILE_VER) return false;
    MailboxConfig c;
    memcpy(&c, &in[n], sizeof(c)); n += sizeof(c);
    if (c.num_owners > MAILBOX_MAX_OWNERS) return false;
    int cnt = in[n] | (in[n + 1] << 8); n += 2;
    if (cnt > MAILBOX_MAX) return false;
    for (int i = 0; i < cnt; i++) {
      if (n + 6 + MAILBOX_HASH_SIZE > len) { _count = 0; return false; }
      MailboxEntry& e = _items[i];
      memcpy(&e.stored_at, &in[n], 4); n += 4;
      e.owner = in[n++];
      e.len = in[n++];
      memcpy(e.hash, &in[n], MAILBOX_HASH_SIZE); n += MAILBOX_HASH_SIZE;
      if (e.owner >= c.num_owners || !isMessage(0x02, e.len) || n + e.len > len) { _count = 0; return false; }
      memcpy(e.payload, &in[n], e.len); n += e.len;
      _count = i + 1;
    }
    _cfg = c;
    return true;
  }
};
