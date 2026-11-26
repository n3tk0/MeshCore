#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <Packet.h>
#include <Utils.h>

#include "AdvertLimiter.h"
#include "FilterCommon.h"
#include "IngressLimiter.h"
#include "NewKeyTracker.h"

// The forwarding filter of a repeater and a room server. Written for this fork around the
// ported Dutch-MeshCore helpers (see docs/packet_filter.md). It only decides whether to
// REPEAT a flood packet: the packet is still received and handled locally. Direct (routed)
// packets are never filtered. Everything is off until `filter on`.
//
// This class is pure logic: the clock, the radio (airtime estimate) and the file system are
// injected, so it runs on the `native` env. The firmware side (file I/O, hooks) is in
// FilterStore.h and the MyMesh classes.

#define FILTER_PREFS_FILE    "/filter_prefs"
#define FILTER_SEEN_FILE     "/filter_seen"
#define FILTER_PREFS_VER     1
#define FILTER_CHAN_NAME_LEN 24

// Room the filter may use in the `reply` buffer. Over the mesh the reply is temp[166] starting at
// offset 5, minus the 3-byte `xx|` tag the CLI reflects back: 166 - 5 - 3 = 158 (serial has 160).
#define FILTER_REPLY_SIZE    158

static const uint8_t FILTER_PUBLIC_SECRET[16] = { 0x8B, 0x33, 0x87, 0xE9, 0xC5, 0xCD, 0xEA, 0x6A,
                                                  0xC9, 0xE5, 0xED, 0xBA, 0xA1, 0x15, 0xCD, 0x72 };
static const uint8_t FILTER_PUBLIC_HASH = 0x11;

// A channel by name: `#name` (key = SHA-256 of the name) or Public.
struct FilterChannel {
  char name[FILTER_CHAN_NAME_LEN];   // '' = empty slot
  uint8_t hash;                      // 1-byte channel hash (derived from the name, not trusted from a file)
  uint8_t secret[16];                // derived too
};

namespace FilterChan {
  // Derives secret and hash from the name. False for an empty or over-long name.
  inline bool derive(const char* name, FilterChannel* ch) {
    if (name == nullptr || name[0] == '\0' || strlen(name) >= FILTER_CHAN_NAME_LEN) return false;
    char copy[FILTER_CHAN_NAME_LEN];   // `name` may be ch->name itself
    strncpy(copy, name, sizeof(copy) - 1);
    copy[sizeof(copy) - 1] = '\0';
    if (strcasecmp(copy, "Public") == 0) strcpy(copy, "Public");   // the one name that is not hashed
    memset(ch, 0, sizeof(*ch));
    memcpy(ch->name, copy, sizeof(copy));
    name = ch->name;
    if (strcmp(name, "Public") == 0) {
      memcpy(ch->secret, FILTER_PUBLIC_SECRET, sizeof(ch->secret));
    } else {
      mesh::Utils::sha256(ch->secret, sizeof(ch->secret), (const uint8_t*)name, (int)strlen(name));
    }
    mesh::Utils::sha256(&ch->hash, 1, ch->secret, sizeof(ch->secret));
    if (strcmp(name, "Public") == 0) ch->hash = FILTER_PUBLIC_HASH;
    return true;
  }
}

// What a decrypt needs: the channel secret zero-padded to the 32 bytes MACThenDecrypt takes.
struct FilterPrefs {
  bool enabled = false;
  bool dryrun = false;
  uint8_t min_hash = 1;                              // minimal path hash size, 1 = off
  uint16_t advert_hours = 0;                         // one flood advert per origin and window, 0 = off
  uint16_t ing_limit = 0, ing_secs = 60, ing_soft = 0;   // per ingress repeater, limit 0 = off
  uint16_t nk_hours = 0, nk_mins = 60;               // new key quota, hours 0 = off
  FilterChannel blocked_ch[FILTER_CHANNEL_COUNT] = {};
  FilterChannel watch_ch[FILTER_WATCH_COUNT] = {};
};

class PacketFilter {
public:
  typedef bool (*KnownFn)(void* ctx, const uint8_t* key4);      // does this node itself know the key?
  typedef uint32_t (*AirFn)(void* ctx, int raw_len);            // estimated TX airtime of a packet, ms
  typedef int (*DecryptFn)(const uint8_t* secret32, uint8_t* dest, const uint8_t* src, int src_len);   // MACThenDecrypt

  enum { SAVE_PREFS = 1, SAVE_SEEN = 2 };

  FilterPrefs p;
  FilterCommon c;
  AdvertLimiter advert;
  IngressLimiter ingress;
  NewKeyTracker newkey;

private:
  KnownFn _known = nullptr;
  void* _known_ctx = nullptr;
  AirFn _air = nullptr;
  void* _air_ctx = nullptr;
  DecryptFn _decrypt = nullptr;   // nullptr: Utils::MACThenDecrypt (tests inject a keyed fake: the native crypto is a mock)
  uint32_t _rng = 0xA5A5F00D;
  uint32_t _saved_sig = 0;   // signature of the prefs as last written

  uint8_t nextRandom() {
    uint32_t x = _rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    _rng = x ? x : 0xA5A5F00D;
    return (uint8_t)(_rng >> 24);
  }

  // Every drop funnels through here, so dry-run and the airtime estimate apply to every reason.
  // Returns the forwarding verdict: true = still forward (dry-run).
  bool drop(const mesh::Packet* packet) {
    if (_air != nullptr) FilterStat::recordAir(c.cnt, _air(_air_ctx, packet->getRawLength()));
    return p.dryrun;
  }

  void applyConfig() {
    advert.setWindowHours(p.advert_hours);
    ingress.configure(p.ing_limit, p.ing_secs, p.ing_soft);
    newkey.configure(p.nk_hours, p.nk_mins);
  }

public:
  PacketFilter() { applyConfig(); }

  void setKnownFn(KnownFn fn, void* ctx) { _known = fn; _known_ctx = ctx; }
  void setAirFn(AirFn fn, void* ctx) { _air = fn; _air_ctx = ctx; }
  void setDecryptFn(DecryptFn fn) { _decrypt = fn; }
  void seed(uint32_t s) { _rng ^= (s << 1) | 1u; if (_rng == 0) _rng = 0xA5A5F00D; }

  bool enabled() const { return p.enabled; }

  void resetPrefs() {
    p = FilterPrefs();
    c.resetSettings();
    advert.clear();
    applyConfig();
  }

  // After a load: derive channel keys, clamp values.
  void sanitize() {
    c.sanitize();
    if (p.min_hash < 1 || p.min_hash > 3) p.min_hash = 1;
    if (p.advert_hours > ADVERT_MAX_HOURS) p.advert_hours = ADVERT_MAX_HOURS;
    if (p.ing_secs == 0 || p.ing_secs > 3600) p.ing_secs = 60;
    if (p.ing_soft >= p.ing_limit) p.ing_soft = 0;
    if (p.nk_hours > NEWKEY_MAX_HOURS) p.nk_hours = NEWKEY_MAX_HOURS;
    if (p.nk_mins == 0 || p.nk_mins > NEWKEY_MAX_MINS) p.nk_mins = 60;
    for (int i = 0; i < FILTER_CHANNEL_COUNT; i++) {
      p.blocked_ch[i].name[FILTER_CHAN_NAME_LEN - 1] = '\0';
      if (p.blocked_ch[i].name[0] && !FilterChan::derive(p.blocked_ch[i].name, &p.blocked_ch[i])) p.blocked_ch[i].name[0] = '\0';
    }
    for (int i = 0; i < FILTER_WATCH_COUNT; i++) {
      p.watch_ch[i].name[FILTER_CHAN_NAME_LEN - 1] = '\0';
      if (p.watch_ch[i].name[0] && !FilterChan::derive(p.watch_ch[i].name, &p.watch_ch[i])) p.watch_ch[i].name[0] = '\0';
    }
    applyConfig();
  }

  // ---- the decision -------------------------------------------------------------------------

  // The 1-byte identity hash of whoever originated the packet, where the payload carries one.
  static bool srcHash(const mesh::Packet* packet, uint8_t* out) {
    switch (packet->getPayloadType()) {
      case PAYLOAD_TYPE_ADVERT:
        if (packet->payload_len < 1) return false;
        *out = packet->payload[0];
        return true;
      case PAYLOAD_TYPE_REQ:
      case PAYLOAD_TYPE_RESPONSE:
      case PAYLOAD_TYPE_TXT_MSG:
      case PAYLOAD_TYPE_ANON_REQ:
      case PAYLOAD_TYPE_PATH:
        if (packet->payload_len < 2) return false;
        *out = packet->payload[1];
        return true;
      default:
        return false;
    }
  }

  // The 32-byte public key a packet carries for its sender: ADVERT (the key comes first) and
  // ANON_REQ (dest hash, then the sender's key). nullptr for every other packet.
  static const uint8_t* senderKey(const mesh::Packet* packet) {
    switch (packet->getPayloadType()) {
      case PAYLOAD_TYPE_ADVERT:
        return packet->payload_len >= PUB_KEY_SIZE ? packet->payload : nullptr;
      case PAYLOAD_TYPE_ANON_REQ:
        return packet->payload_len >= 1 + PUB_KEY_SIZE ? &packet->payload[1] : nullptr;
      default:
        return nullptr;
    }
  }

  // Advances the advert limiter's uptime clock. Called from the main loop so that it sees every
  // 49.7 day wrap of millis() even when no packet arrives for a long time.
  void tick(uint32_t now_ms) { advert.tick(now_ms); }

  // True when this flood packet may be repeated. Direct packets and a filter that is off always pass.
  bool allowForward(const mesh::Packet* packet, uint32_t now_ms, uint32_t now_secs) {
    if (!p.enabled) return true;
    if (packet->isRouteDirect()) return true;

    advert.tick(now_ms);   // its clock must not miss the millis wrap
    uint8_t type = packet->getPayloadType();
    uint8_t src = 0;
    bool have_src = srcHash(packet, &src);

    // blocked key: its adverts and anonymous requests (logins) are not repeated
    const uint8_t* key = senderKey(packet);
    if (key != nullptr && c.hasBlock()) {
      int slot = c.blockSlot(key);
      if (slot >= 0) {
        FilterStat::recordBlock(c.cnt, slot);
        if (have_src) FilterStat::recordSrc(c.cnt, src);
        return drop(packet);
      }
    }

    // minimal path hash size
    if (packet->getPathHashSize() < p.min_hash) {
      FilterStat::recordHash(c.cnt, packet->getPathHashSize(), type);
      if (have_src) FilterStat::recordSrc(c.cnt, src);
      return drop(packet);
    }

    // blocked path prefixes (a rogue upstream repeater)
    int path_slot = FilterPath::findMatch(packet->path, packet->getPathHashSize(), packet->getPathHashCount(),
                                          c.path_block, FILTER_PATH_COUNT);
    if (path_slot >= 0) {
      FilterStat::recordPath(c.cnt, path_slot);
      if (have_src) FilterStat::recordSrc(c.cnt, src);
      return drop(packet);
    }

    // The limiters that stamp state (a new key's quota, an origin's advert window) only look here;
    // they are committed below once the packet is accepted, so a packet dropped by a later check
    // does not use up the budget of a packet that would have been repeated.
    bool commit_newkey = false, commit_advert = false;
    const uint8_t* k4 = nullptr;

    // new keys get a small quota: their flood adverts and anonymous requests
    if (key != nullptr && newkey.active()) {
      k4 = type == PAYLOAD_TYPE_ADVERT ? packet->payload : &packet->payload[1];
      bool known = _known != nullptr && _known(_known_ctx, k4);
      bool is_advert = type == PAYLOAD_TYPE_ADVERT;
      if (!newkey.allow(k4, is_advert ? NewKeyTracker::ADVERT : NewKeyTracker::ANON, known, now_secs, false)) {
        if (is_advert) FilterStat::bump(c.cnt.newkey_advert); else FilterStat::bump(c.cnt.newkey_anon);
        if (have_src) FilterStat::recordSrc(c.cnt, src);
        return drop(packet);
      }
      commit_newkey = !known;
    }

    // one advert per origin and window
    if (type == PAYLOAD_TYPE_ADVERT && packet->payload_len >= ADVERT_KEY_LEN) {
      if (!advert.allow(packet->payload, now_ms, false)) {
        FilterStat::recordAdvert(c.cnt);
        if (have_src) FilterStat::recordSrc(c.cnt, src);
        return drop(packet);
      }
      commit_advert = true;
    }

    // group text: blocked channels, then the content checks
    if (type == PAYLOAD_TYPE_GRP_TXT) {
      // too short to hold a channel hash plus a MAC: cannot be a group text
      if (packet->payload_len <= PATH_HASH_SIZE + CIPHER_MAC_SIZE) {
        FilterStat::recordMalformed(c.cnt, MALFORMED_SHORT);
        return drop(packet);
      }
      // A blocked channel is recognised by its key, not by the 1-byte hash alone: another channel
      // with the same hash is not dropped. Only a packet that decrypts (MAC verified) with the blocked
      // channel's own secret is.
      uint8_t channel_hash = packet->payload[0];
      for (int i = 0; i < FILTER_CHANNEL_COUNT; i++) {
        const FilterChannel& ch = p.blocked_ch[i];
        if (ch.name[0] == '\0' || ch.hash != channel_hash) continue;
        uint8_t data[MAX_PACKET_PAYLOAD + 1];
        if (decrypt(packet, ch.secret, data) > 0) {
          FilterStat::recordChannel(c.cnt, i);
          return drop(packet);
        }
      }
    }

    // the ingress limit counts what would be repeated otherwise
    if (p.ing_limit != 0) {
      uint8_t ikey[INGRESS_KEY_MAX], ilen;
      IngressLimiter::keyOf(packet->path, packet->getPathHashSize(), packet->getPathHashCount(), ikey, &ilen);
      if (!ingress.allow(ikey, ilen, now_ms, nextRandom(), false)) {
        FilterStat::bump(c.cnt.ingress);
        if (have_src) FilterStat::recordSrc(c.cnt, src);
        return drop(packet);
      }
    }

    if (type == PAYLOAD_TYPE_GRP_TXT && !contentAllows(packet, now_ms, now_secs)) return drop(packet);

    // accepted: now the limiters stamp
    if (p.ing_limit != 0) {
      uint8_t ikey[INGRESS_KEY_MAX], ilen;
      IngressLimiter::keyOf(packet->path, packet->getPathHashSize(), packet->getPathHashCount(), ikey, &ilen);
      ingress.allow(ikey, ilen, now_ms, 0, true);
    }
    if (commit_newkey) newkey.allow(k4, type == PAYLOAD_TYPE_ADVERT ? NewKeyTracker::ADVERT : NewKeyTracker::ANON, false, now_secs, true);
    if (commit_advert) advert.allow(packet->payload, now_ms, true);
    return true;
  }

private:
  // Decrypts the group text of `packet` with a 16-byte channel secret. <= 0: the MAC did not verify
  // (another channel with the same 1-byte hash).
  int decrypt(const mesh::Packet* packet, const uint8_t* secret16, uint8_t* data) const {
    uint8_t secret[PUB_KEY_SIZE];
    memset(secret, 0, sizeof(secret));
    memcpy(secret, secret16, 16);
    int len = (_decrypt != nullptr ? _decrypt : mesh::Utils::MACThenDecrypt)(secret, data, &packet->payload[PATH_HASH_SIZE],
                                                                           packet->payload_len - PATH_HASH_SIZE);
    return len > 255 ? 255 : len;
  }

  // The checks that need the plaintext: malformed scan (Public only), age and the sender / text
  // rules (Public plus the watch list). Every channel whose 1-byte hash matches is a candidate and
  // is tried until one decrypts; the plaintext is then reused for all the checks.
  bool contentAllows(const mesh::Packet* packet, uint32_t now_ms, uint32_t now_secs) {
    uint8_t channel_hash = packet->payload[0];
    bool is_public_hash = channel_hash == FILTER_PUBLIC_HASH;
    bool want_malformed = c.malformed && is_public_hash;
    bool want_rules = c.hasContentRules();
    bool want_plain = want_rules || c.age_mins > 0;

    const uint8_t* cand[1 + FILTER_WATCH_COUNT];
    int nc = 0;
    if (is_public_hash) cand[nc++] = FILTER_PUBLIC_SECRET;
    if (want_plain) {
      for (int i = 0; i < FILTER_WATCH_COUNT; i++) {
        const FilterChannel& ch = p.watch_ch[i];
        if (ch.name[0] != '\0' && ch.hash == channel_hash && memcmp(ch.secret, FILTER_PUBLIC_SECRET, 16) != 0) cand[nc++] = ch.secret;
      }
    }
    if (nc == 0 || (!want_malformed && !want_plain)) return true;

    uint8_t data[MAX_PACKET_PAYLOAD + 1];
    int len = 0;
    const uint8_t* used = nullptr;
    for (int i = 0; i < nc && len <= 0; i++) {
      len = decrypt(packet, cand[i], data);
      if (len > 0) used = cand[i];
    }
    if (len <= 0) return true;   // no channel we know: nothing to judge

    const bool is_public = used == FILTER_PUBLIC_SECRET;

    if (want_malformed && is_public) {
      uint8_t reason;
      if (!MessageContent::validate(data, (uint8_t)len, now_secs, &reason)) {
        FilterStat::recordMalformed(c.cnt, reason);
        return false;
      }
    }

    if (c.age_mins > 0 && len >= 4) {
      uint32_t ts;
      memcpy(&ts, &data[0], 4);
      if (MessageAge::outsideLimit(ts, now_secs, c.age_mins)) {
        FilterStat::recordAge(c.cnt);
        return false;
      }
    }

    if (want_rules) {
      FilterRules::GroupText gt;
      if (FilterRules::parseGroupText(data, len, &gt)) {
        uint8_t roll = nextRandom() % 100;
        int slot = FilterRules::evaluate(c.sender_rules, FILTER_RULE_COUNT, now_ms, roll, c.sender_last, c.cnt.sender_pass,
                                         [&](const SenderRule& r) { return FilterRules::matchName(r.name, gt.sender, gt.sender_len); });
        if (slot >= 0) {
          FilterStat::recordSender(c.cnt, slot);
          return false;
        }
        slot = FilterRules::evaluate(c.text_rules, FILTER_RULE_COUNT, now_ms, roll, c.text_last, c.cnt.text_pass,
                                     [&](const TextRule& r) { return FilterRules::matchText(r.text, gt.text, gt.text_len); });
        if (slot >= 0) {
          FilterStat::recordText(c.cnt, slot);
          return false;
        }
      }
    }
    return true;
  }

public:
  // ---- local use of the block list (login, neighbours, mailbox) --------------------------------

  // True when `pub_key` (32 bytes) is blocked and the filter is on and not a dry run. In a dry run
  // it only counts what it would have refused. `count` = false for a pure query.
  bool isBlocked(const uint8_t* pub_key, bool count = true) {
    if (!p.enabled || pub_key == nullptr || !c.hasBlock()) return false;
    int slot = c.blockSlot(pub_key);
    if (slot < 0) return false;
    if (count) FilterStat::bump(c.cnt.block_local);
    return !p.dryrun;
  }

  // ---- the CLI ---------------------------------------------------------------------------------

  static const char* helpText() {
    static_assert(sizeof("> filter [ help | on | off | reset | dryrun | hash | path | advert | channel | watch | malformed | age | sender | text | block | ingress | newkey | stats ]") <= FILTER_REPLY_SIZE, "filter help no longer fits the reply buffer");
    return "> filter [ help | on | off | reset | dryrun | hash | path | advert | channel | watch | malformed | age | sender | text | block | ingress | newkey | stats ]";
  }

  // `command` starts with "filter" and is edited in place. `reply` holds FILTER_REPLY_SIZE bytes.
  // Returns a bitmask of SAVE_PREFS / SAVE_SEEN for what the caller has to write.
  int handleCommand(char* command, char* reply, uint32_t now_ms, uint32_t now_secs) {
    (void)now_ms;
    const char* parts[6];
    int n = FilterCommon::split(command, parts, 6);
    reply[0] = '\0';
    const size_t cap = FILTER_REPLY_SIZE;
    int saves = 0;

    if (n <= 1) {
      FilterStat::formatSummary(reply, cap, c.cnt, p.enabled, p.dryrun);
      return 0;
    }
    const char* sub = parts[1];

    int r = c.runCmd(parts, n, reply, cap, now_secs);
    if (r >= 0) return r ? SAVE_PREFS : 0;

    if (strcmp(sub, "help") == 0) {
      snprintf(reply, cap, "%s", helpText());
    } else if (n == 2 && (strcmp(sub, "on") == 0 || strcmp(sub, "off") == 0)) {
      p.enabled = sub[1] == 'n';
      snprintf(reply, cap, "> Filter: %s", p.enabled ? "on" : "off");
      saves = SAVE_PREFS;
    } else if (n == 2 && strcmp(sub, "reset") == 0) {
      resetPrefs();
      snprintf(reply, cap, "> Filter: preferences reset");
      saves = SAVE_PREFS;
    } else if (strcmp(sub, "dryrun") == 0) {
      if (n == 2) {
        snprintf(reply, cap, "> Filter: dry-run %s", p.dryrun ? "on" : "off");
      } else if (strcmp(parts[2], "on") == 0 || strcmp(parts[2], "off") == 0) {
        p.dryrun = parts[2][1] == 'n';
        snprintf(reply, cap, "> Filter: dry-run %s", p.dryrun ? "on" : "off");
        saves = SAVE_PREFS;
      } else {
        snprintf(reply, cap, "> Filter: syntax error 'filter dryrun <on | off>'");
      }
    } else if (strcmp(sub, "hash") == 0) {
      if (n == 2) {
        snprintf(reply, cap, "> Filter: minimal %d bytes path hash size", p.min_hash);
      } else {
        int v = atoi(parts[2]);
        if (v < 1 || v > 3) {
          snprintf(reply, cap, "> Filter: error hash bytes range is 1-3");
        } else {
          p.min_hash = (uint8_t)v;
          snprintf(reply, cap, "> Filter: OK");
          saves = SAVE_PREFS;
        }
      }
    } else if (strcmp(sub, "advert") == 0) {
      if (n == 2) {
        snprintf(reply, cap, "> Filter: advert origin window %uh (cache %d/%d)", (unsigned)p.advert_hours,
                 advert.getCount(), advert.getCapacity());
      } else if (strcmp(parts[2], "clear") == 0) {
        advert.clear();
        snprintf(reply, cap, "> Filter: advert cache cleared");
      } else {
        long v;
        if (!FilterRules::parseUint(parts[2], ADVERT_MAX_HOURS, &v)) {
          snprintf(reply, cap, "> Filter: error <hours> range is 0-720");
        } else {
          p.advert_hours = (uint16_t)v;
          advert.setWindowHours(p.advert_hours);
          snprintf(reply, cap, "> Filter: OK");
          saves = SAVE_PREFS;
        }
      }
    } else if (strcmp(sub, "channel") == 0) {
      saves = channelCmd(p.blocked_ch, FILTER_CHANNEL_COUNT, "channel", parts, n, reply, cap, true);
    } else if (strcmp(sub, "watch") == 0) {
      saves = channelCmd(p.watch_ch, FILTER_WATCH_COUNT, "watch", parts, n, reply, cap, false);
    } else if (strcmp(sub, "ingress") == 0) {
      saves = ingressCmd(parts, n, reply, cap);
    } else if (strcmp(sub, "newkey") == 0) {
      saves = newkeyCmd(parts, n, reply, cap);
    } else if (strcmp(sub, "stats") == 0) {
      statsCmd(parts, n, reply, cap, now_secs);
    } else {
      snprintf(reply, cap, "> Filter: command error");
    }
    return saves;
  }

private:
  // `filter channel|watch list [page]` (cnt == nullptr, four a page: a 23 character name with its hash
  // is 28 bytes) and `filter stats channel` (with drops, in one piece).
  static void channelList(char* out, size_t cap, const FilterChannel* list, int count, const Counters* cnt, bool with_public, int page = 0) {
    if (cnt == nullptr) {
      // entry 0 is "Public" when listed, the others are list[i - 1]
      const int off = with_public ? 1 : 0;
      FilterStat::formatPaged(out, cap, count + off, 4, page,
                              [&](int i) { return i < off || list[i - off].name[0] != '\0'; },
                              [&](FilterStat::Buf& b, int i, bool first) {
                                if (i < off) b.add(first ? "Public (11)" : ",Public (11)");
                                else b.add(first ? "%s (%02x)" : ",%s (%02x)", list[i - off].name, (unsigned)list[i - off].hash);
                              });
      return;
    }
    FilterStat::Buf b(out, cap);
    int listed = 0;
    for (int i = 0; i < count; i++) {
      if (list[i].name[0] == '\0') continue;
      b.add(listed > 0 ? ",%s (%02x): %lu" : "%s (%02x): %lu", list[i].name, (unsigned)list[i].hash, (unsigned long)cnt->channel_slot[i]);
      listed++;
    }
    if (listed == 0) {
      b.add("None");
      return;
    }
    b.markTruncated("..");
  }

  int channelCmd(FilterChannel* list, int count, const char* what, const char* const* parts, int n, char* reply,
                 size_t cap, bool blocked) {
    if (n >= 3 && strcmp(parts[2], "list") == 0) {
      channelList(reply, cap, list, count, nullptr, !blocked, n >= 4 ? atoi(parts[3]) : 0);
      return 0;
    }
    if (n >= 4 && strcmp(parts[2], "add") == 0) {
      FilterChannel ch;
      bool valid = FilterChan::derive(parts[3], &ch);   // "public" in any case is "Public"
      int free_slot = -1;
      bool dup = false;
      for (int i = 0; i < count && valid; i++) {
        if (list[i].name[0] == '\0') { if (free_slot < 0) free_slot = i; }
        else if (strcmp(list[i].name, ch.name) == 0) dup = true;
      }
      if (!blocked && valid && strcmp(ch.name, "Public") == 0) dup = true;   // always watched
      if (!valid || dup || free_slot < 0) {
        snprintf(reply, cap, "Failed");
        return 0;
      }
      list[free_slot] = ch;
      snprintf(reply, cap, "> Filter: %s %s added", what, parts[3]);
      return SAVE_PREFS;
    }
    if (n >= 4 && strcmp(parts[2], "remove") == 0) {
      FilterChannel want;
      if (!FilterChan::derive(parts[3], &want)) want.name[0] = '\0';
      for (int i = 0; i < count && want.name[0] != '\0'; i++) {
        if (list[i].name[0] != '\0' && strcmp(list[i].name, want.name) == 0) {
          list[i].name[0] = '\0';
          snprintf(reply, cap, "> Filter: %s %s removed", what, parts[3]);
          return SAVE_PREFS;
        }
      }
      snprintf(reply, cap, "Failed");
      return 0;
    }
    snprintf(reply, cap, "> filter %s [ list [page] | add | remove ] <#name%s>", what, blocked ? " | Public" : "");
    return 0;
  }

  int ingressCmd(const char* const* parts, int n, char* reply, size_t cap) {
    if (n == 2) {
      if (p.ing_limit == 0) snprintf(reply, cap, "> Ingress limit off");
      else snprintf(reply, cap, "> Ingress limit %u per %us, soft %u (%d keys)", (unsigned)p.ing_limit,
                    (unsigned)p.ing_secs, (unsigned)p.ing_soft, ingress.used());
      return 0;
    }
    if (strcmp(parts[2], "off") == 0 || strcmp(parts[2], "0") == 0) {
      p.ing_limit = 0;
      p.ing_soft = 0;
      ingress.configure(0, p.ing_secs, 0);
      snprintf(reply, cap, "> Filter: ingress limit off");
      return SAVE_PREFS;
    }
    long lim, secs = 60, soft = 0;
    if (!FilterRules::parseUint(parts[2], 10000, &lim) || lim < 1) {
      snprintf(reply, cap, "> Filter: error <n> range is 1-10000, or off");
      return 0;
    }
    if (n >= 4 && (!FilterRules::parseUint(parts[3], 3600, &secs) || secs < 1)) {
      snprintf(reply, cap, "> Filter: error <secs> range is 1-3600");
      return 0;
    }
    if (n >= 5) {
      bool soft_word = strcmp(parts[4], "soft") == 0;
      if (soft_word) soft = lim * 2 / 3;   // "soft" alone: the ramp starts at two thirds
      else if (!FilterRules::parseUint(parts[4], 10000, &soft) || soft >= lim) {
        snprintf(reply, cap, "> Filter: error <soft> must be less than <n>");
        return 0;
      }
    }
    p.ing_limit = (uint16_t)lim;
    p.ing_secs = (uint16_t)secs;
    p.ing_soft = (uint16_t)soft;
    ingress.configure(p.ing_limit, p.ing_secs, p.ing_soft);
    snprintf(reply, cap, "> Filter: OK");
    return SAVE_PREFS;
  }

  int newkeyCmd(const char* const* parts, int n, char* reply, size_t cap) {
    if (n == 2) {
      if (p.nk_hours == 0) snprintf(reply, cap, "> Newkey quota off (%d keys known)", newkey.count());
      else snprintf(reply, cap, "> Newkey: keys under %uh get 1 advert per %um (%d keys known)", (unsigned)p.nk_hours,
                    (unsigned)p.nk_mins, newkey.count());
      return 0;
    }
    if (strcmp(parts[2], "clear") == 0) {
      newkey.clear();
      snprintf(reply, cap, "> Filter: known keys cleared");
      return SAVE_SEEN;
    }
    if (strcmp(parts[2], "off") == 0 || strcmp(parts[2], "0") == 0) {
      p.nk_hours = 0;
      newkey.configure(0, p.nk_mins);
      snprintf(reply, cap, "> Filter: newkey quota off");
      return SAVE_PREFS;
    }
    long h, m = 60;
    if (!FilterRules::parseUint(parts[2], NEWKEY_MAX_HOURS, &h) || h < 1) {
      snprintf(reply, cap, "> Filter: error <hours> range is 1-%d, or off", NEWKEY_MAX_HOURS);
      return 0;
    }
    if (n >= 4 && (!FilterRules::parseUint(parts[3], NEWKEY_MAX_MINS, &m) || m < 1)) {
      snprintf(reply, cap, "> Filter: error <mins> range is 1-%d", NEWKEY_MAX_MINS);
      return 0;
    }
    p.nk_hours = (uint16_t)h;
    p.nk_mins = (uint16_t)m;
    newkey.configure(p.nk_hours, p.nk_mins);
    snprintf(reply, cap, "> Filter: OK");
    return SAVE_PREFS;
  }

  static void hexKey(char* out, const uint8_t* key, uint8_t len) {
    if (len == IngressLimiter::KEY_DIRECT) { strcpy(out, "direct"); return; }
    for (uint8_t i = 0; i < len && i < INGRESS_KEY_MAX; i++) snprintf(out + 2 * i, 3, "%02x", (unsigned)key[i]);
    if (len == 0) out[0] = '\0';
  }

  void statsCmd(const char* const* parts, int n, char* reply, size_t cap, uint32_t now_secs) {
    static const char HELP[] = "> filter stats [ hash | path | advert | channel | malformed | age | sender | text | block | ingress | newkey | air | top | reset ]";
    static_assert(sizeof(HELP) <= FILTER_REPLY_SIZE, "filter stats help no longer fits the reply buffer");
    if (n < 3) { snprintf(reply, cap, "%s", HELP); return; }
    const char* t = parts[2];
    if (strcmp(t, "hash") == 0) FilterStat::formatStatsHash(reply, cap, c.cnt);
    else if (strcmp(t, "advert") == 0) FilterStat::formatStatsAdvert(reply, cap, c.cnt, p.advert_hours, advert.getCount(), advert.getCapacity());
    else if (strcmp(t, "channel") == 0) channelList(reply, cap, p.blocked_ch, FILTER_CHANNEL_COUNT, &c.cnt, false);
    else if (strcmp(t, "top") == 0) FilterStat::formatStatsTop(reply, cap, c.cnt);
    else if (strcmp(t, "air") == 0) FilterStat::formatStatsAir(reply, cap, c.cnt, p.dryrun);
    else if (strcmp(t, "reset") == 0) { c.cnt.reset(); snprintf(reply, cap, "> Filter: counters reset"); }
    else if (strcmp(t, "ingress") == 0) {
      FilterStat::Buf b(reply, cap);
      if (p.ing_limit == 0) { b.add("> Filter: ingress limit off"); return; }
      b.add("> Ingress %u/%us: dropped %lu, %d keys", (unsigned)p.ing_limit, (unsigned)p.ing_secs,
            (unsigned long)c.cnt.ingress, ingress.used());
      IngressLimiter::Top top[INGRESS_TOP_COUNT];
      int k = ingress.top(top, INGRESS_TOP_COUNT);
      for (int i = 0; i < k; i++) {
        char hex[9];
        hexKey(hex, top[i].key, top[i].key_len);
        b.add(i == 0 ? "\n%s:%u" : " %s:%u", hex, (unsigned)top[i].drops);
      }
      b.markTruncated("..");
    } else if (strcmp(t, "newkey") == 0) {
      FilterStat::Buf b(reply, cap);
      if (p.nk_hours == 0) { b.add("> Filter: newkey quota off"); return; }
      b.add("> Newkey %uh/%um: dropped adverts %lu, requests %lu, %d keys", (unsigned)p.nk_hours, (unsigned)p.nk_mins,
            (unsigned long)c.cnt.newkey_advert, (unsigned long)c.cnt.newkey_anon, newkey.count());
      if (!MessageAge::clockSet(now_secs)) b.add(" (clock not set, inactive)");
      b.markTruncated("..");
    } else {
      snprintf(reply, cap, "%s", HELP);
    }
  }

public:
  // ---- persistence (the stream format; the file I/O is in FilterStore.h) ----------------------
  //
  // [ 'F' 'P' 'F' ver ] then little endian fields in this order. Later versions only append
  // sections: a reader keeps the defaults of the sections an older file does not have.
  //   u8 enabled, dryrun, min_hash, malformed ; u16 advert_hours, age_mins, ing_limit, ing_secs,
  //   ing_soft, nk_hours, nk_mins ; 8 x path (u8 len + 4) ; 8 x sender (name 16, u16 secs, u8 prob) ;
  //   8 x text (24, u16, u8) ; u8 n, n x (u8 len, key[len]) block ; u8 n, n x name[24] channels ;
  //   u8 n, n x name[24] watch

  template <typename Sink>
  void writePrefs(Sink& w) const {
    uint8_t hdr[4] = { 'F', 'P', 'F', FILTER_PREFS_VER };
    w.put(hdr, 4);
    uint8_t b[4] = { p.enabled, p.dryrun, p.min_hash, c.malformed };
    w.put(b, 4);
    FilterCommon::put16(w, p.advert_hours); FilterCommon::put16(w, c.age_mins); FilterCommon::put16(w, p.ing_limit); FilterCommon::put16(w, p.ing_secs);
    FilterCommon::put16(w, p.ing_soft); FilterCommon::put16(w, p.nk_hours); FilterCommon::put16(w, p.nk_mins);
    c.writeLists(w);
    putChannels(w, p.blocked_ch, FILTER_CHANNEL_COUNT);
    putChannels(w, p.watch_ch, FILTER_WATCH_COUNT);
  }

  // Reads what writePrefs() wrote. `r.get(buf, n)` returns true when it delivered n bytes.
  // On any failure the prefs are left at their defaults and false is returned.
  template <typename Src>
  bool readPrefs(Src& r) {
    resetPrefs();
    uint8_t hdr[4];
    if (!r.get(hdr, 4) || hdr[0] != 'F' || hdr[1] != 'P' || hdr[2] != 'F' || hdr[3] == 0 || hdr[3] > FILTER_PREFS_VER) return false;
    bool ok = true;
    uint8_t b[4];
    ok = ok && r.get(b, 4);
    uint16_t v[7] = {};
    for (int i = 0; i < 7 && ok; i++) ok = FilterCommon::get16(r, &v[i]);
    ok = ok && c.readLists(r);
    ok = ok && getChannels(r, p.blocked_ch, FILTER_CHANNEL_COUNT) && getChannels(r, p.watch_ch, FILTER_WATCH_COUNT);
    if (!ok) {
      resetPrefs();
      return false;
    }
    p.enabled = b[0] != 0;
    p.dryrun = b[1] != 0;
    p.min_hash = b[2];
    c.malformed = b[3] != 0;
    p.advert_hours = v[0]; c.age_mins = v[1]; p.ing_limit = v[2]; p.ing_secs = v[3];
    p.ing_soft = v[4]; p.nk_hours = v[5]; p.nk_mins = v[6];
    sanitize();
    return true;
  }

  // A signature of the settings as they would be written, to write the file only on change.
  struct SigSink {
    uint32_t h = 2166136261u;
    void put(const void* d, size_t n) {
      const uint8_t* q = (const uint8_t*)d;
      for (size_t i = 0; i < n; i++) { h ^= q[i]; h *= 16777619u; }
    }
  };
  uint32_t prefsSignature() const {
    SigSink s;
    writePrefs(s);
    return s.h;
  }
  bool prefsChanged() const { return prefsSignature() != _saved_sig; }
  void markPrefsSaved() { _saved_sig = prefsSignature(); }

private:
  template <typename Sink> static void putChannels(Sink& w, const FilterChannel* list, int count) {
    uint8_t n = 0;
    for (int i = 0; i < count; i++) if (list[i].name[0]) n++;
    w.put(&n, 1);
    for (int i = 0; i < count; i++) {
      if (list[i].name[0] == '\0') continue;
      w.put(list[i].name, FILTER_CHAN_NAME_LEN);
    }
  }
  template <typename Src> static bool getChannels(Src& r, FilterChannel* list, int count) {
    uint8_t n;
    if (!r.get(&n, 1) || n > count) return false;
    for (int i = 0; i < n; i++) {
      memset(&list[i], 0, sizeof(FilterChannel));
      if (!r.get(list[i].name, FILTER_CHAN_NAME_LEN)) return false;
      list[i].name[FILTER_CHAN_NAME_LEN - 1] = '\0';   // derive() is done by sanitize()
    }
    return true;
  }
};
