#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Packet.h>

#include "FilterCommon.h"

// Ported from Dutch-MeshCore (github.com/Dutch-MeshCore/MeshCore, dmc-dev), MIT, for the idea and the
// verdicts (examples/companion_radio/CompanionFilter.*); rewritten around this fork's shared helpers
// and extended with the block list. Copyright (c) 2025 Scott Powell / rippleradios.com, and the
// Dutch-MeshCore contributors. MIT licence: see license.txt in the repository root.
//
// Receive filter of the companion. A companion does not normally repeat, so this keeps junk and
// unwanted traffic away from the user. Besides PASS there are two verdicts:
//
//   DROP  junk (malformed text, too old, via a blocked repeater, an advert of a blocked key): not
//         shown, and not repeated either when the companion has repeat enabled.
//   HIDE  personal taste (sender / text rules, a hidden channel): not shown, but still repeated, so
//         one user's mute list never censors the mesh for others.
//
// Direct messages to this node are never judged here: the receiver handles those (a blocked
// contact's messages are dropped after decryption, and still acknowledged silently).
//
// Header-only and free of radio, file system and clock so it runs on the `native` env. The file is in
// examples/companion_radio/CompanionFilterHook.cpp.

#define CFILTER_PREFS_FILE   "/cfilter"
#define CFILTER_PREFS_VER    1

// The tightest reply buffer is remote CLI over the mesh: the reply sits at offset 5 of a 166-byte
// buffer (161 bytes), after an optional 3-byte "xx|" prefix echoed from the command. 158 fits all.
#define CFILTER_REPLY_SIZE   158

// Recently judged packets remembered, so the copies every nearby repeater relays are not judged and
// counted again.
#define CFILTER_JUDGED_COUNT 16

// Channels hidden on this device (by the first 4 bytes of the channel key).
#define CFILTER_HIDDEN_COUNT 4
#define CFILTER_HIDDEN_NAME  20

enum class CFilterVerdict : uint8_t { PASS, HIDE, DROP };

struct CHiddenChannel {
  uint32_t key;                      // first 4 bytes of the channel secret, 0 = empty slot
  char name[CFILTER_HIDDEN_NAME];    // to show in the list
};

class CompanionFilter {
public:
  bool enabled = false;
  bool dryrun = false;                 // count, but let everything through
  FilterCommon c;
  CHiddenChannel hidden[CFILTER_HIDDEN_COUNT] = {};

private:
  uint8_t _judged[CFILTER_JUDGED_COUNT][MAX_HASH_SIZE];
  uint8_t _judged_count = 0;
  uint8_t _judged_next = 0;
  uint32_t _saved_sig = 0;

  // dry-run: counted, never acted on
  CFilterVerdict verdict(CFilterVerdict v) const { return dryrun ? CFilterVerdict::PASS : v; }

public:
  CompanionFilter() { }

  bool isEnabled() const { return enabled; }

  void reset() {
    enabled = false;
    dryrun = false;
    c.resetSettings();
    memset(hidden, 0, sizeof(hidden));
  }

  // Whether checkGroupText() can say anything, i.e. whether decrypting a flood group text up front is
  // worth it.
  bool wantsGroupText() const {
    if (!enabled) return false;
    return c.malformed || c.age_mins > 0 || c.hasContentRules() || hiddenCount() > 0;
  }

  // ---- the block list ----------------------------------------------------------------------

  // True when this key (32 bytes) is blocked and the filter is on. In a dry run it only counts.
  bool isBlocked(const uint8_t* pub_key) {
    if (!enabled || pub_key == nullptr || !c.hasBlock()) return false;
    int slot = c.blockSlot(pub_key);
    if (slot < 0) return false;
    FilterStat::bump(c.cnt.block_local);
    return !dryrun;
  }

  // A key known only by its first `len` (>= 4) bytes (a discovery answer): blocked when those bytes
  // agree with a list entry. Not counted.
  bool isPrefixBlocked(const uint8_t* prefix, size_t len) const {
    if (!enabled || dryrun || !c.hasBlock()) return false;
    return FilterBlock::findLoose(c.block, FILTER_BLOCK_COUNT, prefix, len) >= 0;
  }

  // The UI's "Block": a whole key, and the filter is switched on so it takes effect.
  bool blockKey(const uint8_t* pub_key) {
    bool added = c.addBlockKey(pub_key);
    enabled = true;
    return added;
  }

  // ---- hidden channels -----------------------------------------------------------------------

  int hiddenCount() const {
    int n = 0;
    for (int i = 0; i < CFILTER_HIDDEN_COUNT; i++) if (hidden[i].key != 0) n++;
    return n;
  }
  bool isChannelHidden(uint32_t key) const {
    if (key == 0) return false;
    for (int i = 0; i < CFILTER_HIDDEN_COUNT; i++) if (hidden[i].key == key) return true;
    return false;
  }
  // False when the list is full (or the key is empty); true also when it was already hidden.
  bool hideChannel(uint32_t key, const char* name) {
    if (key == 0) return false;
    if (isChannelHidden(key)) { enabled = true; return true; }
    for (int i = 0; i < CFILTER_HIDDEN_COUNT; i++) {
      if (hidden[i].key != 0) continue;
      hidden[i].key = key;
      strncpy(hidden[i].name, name ? name : "", CFILTER_HIDDEN_NAME - 1);
      hidden[i].name[CFILTER_HIDDEN_NAME - 1] = '\0';
      enabled = true;
      return true;
    }
    return false;
  }
  bool unhideSlot(int slot) {
    if (slot < 0 || slot >= CFILTER_HIDDEN_COUNT || hidden[slot].key == 0) return false;
    memset(&hidden[slot], 0, sizeof(CHiddenChannel));
    return true;
  }

  // ---- verdicts ------------------------------------------------------------------------------

  // Flood ADVERT of a blocked key: DROP. Flood ADVERT / GRP_TXT / GRP_DATA whose path holds a blocked
  // repeater prefix: DROP. Traffic addressed to this node is never judged here.
  CFilterVerdict checkFlood(const mesh::Packet* packet) {
    if (!enabled || packet == nullptr || !packet->isRouteFlood()) return CFilterVerdict::PASS;

    uint8_t type = packet->getPayloadType();
    if (type == PAYLOAD_TYPE_ADVERT && packet->payload_len >= PUB_KEY_SIZE && c.hasBlock()) {
      int slot = c.blockSlot(packet->payload);
      if (slot >= 0) {
        FilterStat::recordBlock(c.cnt, slot);
        return verdict(CFilterVerdict::DROP);
      }
    }

    if (type != PAYLOAD_TYPE_ADVERT && type != PAYLOAD_TYPE_GRP_TXT && type != PAYLOAD_TYPE_GRP_DATA) {
      return CFilterVerdict::PASS;
    }
    int slot = FilterPath::findMatch(packet->path, packet->getPathHashSize(), packet->getPathHashCount(),
                                     c.path_block, FILTER_PATH_COUNT);
    if (slot < 0) return CFilterVerdict::PASS;
    FilterStat::recordPath(c.cnt, slot);
    return verdict(CFilterVerdict::DROP);
  }

  // `data` is a decrypted GRP_TXT plaintext (timestamp, flags, "Sender: text"). `roll` is a random 0..99
  // for the rules' probability. `channel_key` is the first 4 bytes of the channel's secret.
  CFilterVerdict checkGroupText(const uint8_t* data, int len, uint32_t now_secs, uint32_t now_ms, uint8_t roll,
                                uint32_t channel_key = 0) {
    if (!enabled || data == nullptr || len <= 0) return CFilterVerdict::PASS;
    if (len > 255) len = 255;   // a LoRa payload never is; keeps the uint8_t length below honest

    // junk first: it is dropped outright, not merely hidden
    if (c.malformed) {
      uint8_t reason;
      if (!MessageContent::validate(data, (uint8_t)len, now_secs, &reason)) {
        FilterStat::recordMalformed(c.cnt, reason);
        return verdict(CFilterVerdict::DROP);
      }
    }

    if (c.age_mins > 0 && len >= 4) {
      uint32_t ts;
      memcpy(&ts, data, 4);
      if (MessageAge::outsideLimit(ts, now_secs, c.age_mins)) {
        FilterStat::recordAge(c.cnt);
        return verdict(CFilterVerdict::DROP);
      }
    }

    // then the user's own choices
    if (isChannelHidden(channel_key)) {
      FilterStat::bump(c.cnt.channel);
      return verdict(CFilterVerdict::HIDE);
    }

    FilterRules::GroupText gt;
    if (!FilterRules::parseGroupText(data, len, &gt)) return CFilterVerdict::PASS;

    int slot = FilterRules::evaluate(c.sender_rules, FILTER_RULE_COUNT, now_ms, roll, c.sender_last, c.cnt.sender_pass,
                                     [&](const SenderRule& r) { return FilterRules::matchName(r.name, gt.sender, gt.sender_len); });
    if (slot >= 0) {
      FilterStat::recordSender(c.cnt, slot);
      return verdict(CFilterVerdict::HIDE);
    }
    slot = FilterRules::evaluate(c.text_rules, FILTER_RULE_COUNT, now_ms, roll, c.text_last, c.cnt.text_pass,
                                 [&](const TextRule& r) { return FilterRules::matchText(r.text, gt.text, gt.text_len); });
    if (slot >= 0) {
      FilterStat::recordText(c.cnt, slot);
      return verdict(CFilterVerdict::HIDE);
    }
    return CFilterVerdict::PASS;
  }

  // True when this packet hash was judged recently; otherwise records it. The caller lets such a copy
  // through to the mesh's own duplicate check.
  bool alreadyJudged(const uint8_t hash[MAX_HASH_SIZE]) {
    for (int i = 0; i < _judged_count; i++) {
      if (memcmp(_judged[i], hash, MAX_HASH_SIZE) == 0) return true;
    }
    memcpy(_judged[_judged_next], hash, MAX_HASH_SIZE);
    _judged_next = (uint8_t)((_judged_next + 1) % CFILTER_JUDGED_COUNT);
    if (_judged_count < CFILTER_JUDGED_COUNT) _judged_count++;
    return false;
  }

  // ---- CLI -----------------------------------------------------------------------------------

  static bool isCommand(const char* command) {
    return command != nullptr && strncmp(command, "filter", 6) == 0 && (command[6] == '\0' || command[6] == ' ');
  }

  // `filter ...`. `reply` must hold CFILTER_REPLY_SIZE bytes. Returns true when the settings changed and
  // should be saved.
  bool handleCommand(const char* command, char* reply, uint32_t now_secs) {
    static const char HELP[] = "> filter [ help | on | off | reset | dryrun | stats | malformed | age | path | sender | text | block ]";
    static const char STATS_HELP[] = "> filter stats [ malformed | age | path | sender | text | block ]";
    static_assert(sizeof(HELP) <= CFILTER_REPLY_SIZE, "filter help no longer fits the reply buffer");

    char buf[CFILTER_REPLY_SIZE];
    strncpy(buf, command, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    const char* parts[6];
    int n = FilterCommon::split(buf, parts, 6);
    const size_t cap = CFILTER_REPLY_SIZE;
    reply[0] = '\0';

    if (n <= 1) {
      FilterStat::Buf b(reply, cap);
      b.add("> Filter %s: Hidden [", enabled ? "on" : "off");
      b.add(" Malformed:%lu", (unsigned long)c.cnt.malformed);
      b.add(" Age:%lu", (unsigned long)c.cnt.age);
      b.add(" Path:%lu", (unsigned long)c.cnt.path);
      b.add(" Sender:%lu", (unsigned long)c.cnt.sender);
      b.add(" Text:%lu", (unsigned long)c.cnt.text);
      b.add(" Key:%lu", (unsigned long)c.cnt.block);
      b.add(" ]");
      if (dryrun) b.add(" (dry-run)");
      b.markTruncated("..");
      return false;
    }
    const char* sub = parts[1];

    if (strcmp(sub, "stats") == 0 && n < 3) {
      snprintf(reply, cap, "%s", STATS_HELP);
      return false;
    }
    int r = c.runCmd(parts, n, reply, cap, now_secs);
    if (r >= 0) return r != 0;

    if (strcmp(sub, "help") == 0) {
      snprintf(reply, cap, "%s", HELP);
    } else if (n == 2 && (strcmp(sub, "on") == 0 || strcmp(sub, "off") == 0)) {
      enabled = sub[1] == 'n';
      snprintf(reply, cap, "> Filter: %s", enabled ? "on" : "off");
      return true;
    } else if (n == 2 && strcmp(sub, "reset") == 0) {
      reset();
      snprintf(reply, cap, "> Filter: preferences reset");
      return true;
    } else if (strcmp(sub, "dryrun") == 0) {
      if (n == 2) {
        snprintf(reply, cap, "> Filter: dry-run %s", dryrun ? "on" : "off");
      } else if (strcmp(parts[2], "on") == 0 || strcmp(parts[2], "off") == 0) {
        dryrun = parts[2][1] == 'n';
        snprintf(reply, cap, "> Filter: dry-run %s", dryrun ? "on" : "off");
        return true;
      } else {
        snprintf(reply, cap, "> Filter: syntax error 'filter dryrun <on | off>'");
      }
    } else if (strcmp(sub, "stats") == 0) {
      snprintf(reply, cap, "%s", STATS_HELP);
    } else {
      snprintf(reply, cap, "> Filter: command error");
    }
    return false;
  }

  // ---- the saved file: [ 'C' 'F' 'L' ver ] [u8 enabled][u8 dryrun][lists][u8 n, n x (u32 key, name[20])] ----

  template <typename Sink>
  void write(Sink& w) const {
    uint8_t hdr[4] = { 'C', 'F', 'L', CFILTER_PREFS_VER };
    w.put(hdr, 4);
    uint8_t b[4] = { enabled, dryrun, c.malformed, 0 };
    w.put(b, 4);
    FilterCommon::put16(w, c.age_mins);
    c.writeLists(w);
    uint8_t n = (uint8_t)hiddenCount();
    w.put(&n, 1);
    for (int i = 0; i < CFILTER_HIDDEN_COUNT; i++) {
      if (hidden[i].key == 0) continue;
      uint8_t k[4] = { (uint8_t)hidden[i].key, (uint8_t)(hidden[i].key >> 8), (uint8_t)(hidden[i].key >> 16), (uint8_t)(hidden[i].key >> 24) };
      w.put(k, 4);
      w.put(hidden[i].name, CFILTER_HIDDEN_NAME);
    }
  }

  // False (and everything at the defaults, off) when the file does not parse.
  template <typename Src>
  bool read(Src& r) {
    reset();
    uint8_t hdr[4];
    if (!r.get(hdr, 4) || hdr[0] != 'C' || hdr[1] != 'F' || hdr[2] != 'L' || hdr[3] == 0 || hdr[3] > CFILTER_PREFS_VER) return false;
    uint8_t b[4];
    uint16_t age = 0;
    bool ok = r.get(b, 4) && FilterCommon::get16(r, &age) && c.readLists(r);
    uint8_t n = 0;
    ok = ok && r.get(&n, 1) && n <= CFILTER_HIDDEN_COUNT;
    for (int i = 0; i < n && ok; i++) {
      uint8_t k[4];
      ok = r.get(k, 4) && r.get(hidden[i].name, CFILTER_HIDDEN_NAME);
      hidden[i].key = (uint32_t)k[0] | ((uint32_t)k[1] << 8) | ((uint32_t)k[2] << 16) | ((uint32_t)k[3] << 24);
      hidden[i].name[CFILTER_HIDDEN_NAME - 1] = '\0';
    }
    if (!ok) {
      reset();
      return false;
    }
    enabled = b[0] != 0;
    dryrun = b[1] != 0;
    c.malformed = b[2] != 0;
    c.age_mins = age;
    c.sanitize();
    return true;
  }

  struct SigSink {
    uint32_t h = 2166136261u;
    void put(const void* d, size_t n) {
      const uint8_t* q = (const uint8_t*)d;
      for (size_t i = 0; i < n; i++) { h ^= q[i]; h *= 16777619u; }
    }
  };
  uint32_t signature() const {
    SigSink s;
    write(s);
    return s.h;
  }
  bool changed() const { return signature() != _saved_sig; }
  void markSaved() { _saved_sig = signature(); }
};
