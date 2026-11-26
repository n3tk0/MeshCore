#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "BlockList.h"
#include "FilterStats.h"
#include "MessageAge.h"
#include "MessageContent.h"
#include "PathBlock.h"
#include "SenderRules.h"

// The part of the packet filter that the repeater / room server filter (PacketFilter.h) and the
// companion filter (CompanionFilter.h) share: the settings, the counters and the `filter ...` CLI
// for malformed, age, path, sender, text and block. Written for this fork around the ported
// helpers. Header-only and free of radio, file system and clock so it runs on the `native` env.

struct FilterCommon {
  bool malformed = false;                          // scan the text of Public group messages
  uint16_t age_mins = 0;                           // drop group text older than this, 0 = off
  PathPrefix path_block[FILTER_PATH_COUNT] = {};   // blocked repeater IDs in the path
  SenderRule sender_rules[FILTER_RULE_COUNT] = {};
  TextRule text_rules[FILTER_RULE_COUNT] = {};
  BlockEntry block[FILTER_BLOCK_COUNT] = {};       // blocked public keys (prefixes)

  Counters cnt;
  uint32_t sender_last[FILTER_RULE_COUNT] = {};    // throttle state per sender rule (millis, 0 = never)
  uint32_t text_last[FILTER_RULE_COUNT] = {};

  void resetSettings() {
    malformed = false;
    age_mins = 0;
    memset(path_block, 0, sizeof(path_block));
    memset(sender_rules, 0, sizeof(sender_rules));
    memset(text_rules, 0, sizeof(text_rules));
    memset(block, 0, sizeof(block));
    clearThrottles();
  }
  void clearThrottles() {
    memset(sender_last, 0, sizeof(sender_last));
    memset(text_last, 0, sizeof(text_last));
  }

  // After a load: clamp anything a damaged or hand-edited file got wrong.
  void sanitize() {
    if (age_mins > FILTER_AGE_MAX_MINS) age_mins = FILTER_AGE_MAX_MINS;
    for (int i = 0; i < FILTER_PATH_COUNT; i++) {
      if (path_block[i].len > FILTER_PATH_MAX_LEN) memset(&path_block[i], 0, sizeof(PathPrefix));
    }
    for (int i = 0; i < FILTER_RULE_COUNT; i++) {
      SenderRule& sr = sender_rules[i];
      sr.name[FILTER_SENDER_LEN - 1] = '\0';
      if (sr.name[0] == '\0') memset(&sr, 0, sizeof(sr));
      else if (sr.prob == 0 || sr.prob > 100) sr.prob = 100;
      TextRule& tr = text_rules[i];
      tr.text[FILTER_TEXT_LEN - 1] = '\0';
      if (tr.text[0] == '\0') memset(&tr, 0, sizeof(tr));
      else if (tr.prob == 0 || tr.prob > 100) tr.prob = 100;
    }
    FilterBlock::sanitize(block, FILTER_BLOCK_COUNT);
  }

  bool hasContentRules() const {
    return FilterRules::anyRule(sender_rules, FILTER_RULE_COUNT) || FilterRules::anyRule(text_rules, FILTER_RULE_COUNT);
  }
  bool hasBlock() const { return FilterBlock::count(block, FILTER_BLOCK_COUNT) > 0; }

  // Slot of the block entry that `pub_key` (32 bytes) starts with, or -1.
  int blockSlot(const uint8_t* pub_key) const {
    return FilterBlock::findMatch(block, FILTER_BLOCK_COUNT, pub_key, FILTER_BLOCK_MAX_LEN);
  }

  // Blocks a whole key (the UI's "Block"). False when it is already there or the list is full.
  bool addBlockKey(const uint8_t* pub_key) {
    return FilterBlock::addRaw(block, FILTER_BLOCK_COUNT, pub_key, FILTER_BLOCK_MAX_LEN);
  }
  // Removes the entry in `slot`.
  bool removeBlockSlot(int slot) {
    if (slot < 0 || slot >= FILTER_BLOCK_COUNT || block[slot].len == 0) return false;
    memset(&block[slot], 0, sizeof(BlockEntry));
    return true;
  }

  // Muted sender (UI): a block rule on the EXACT name (a trailing '*' is escaped, not a prefix rule).
  // False when it does not fit or the list is full; true also when the rule is already there.
  bool addSenderName(const char* name) {
    char pat[FILTER_SENDER_LEN];
    if (!FilterRules::exactNamePattern(name, pat, sizeof(pat))) return false;
    for (int i = 0; i < FILTER_RULE_COUNT; i++) if (strcmp(sender_rules[i].name, pat) == 0) return true;
    return FilterRules::addRule(sender_rules, FILTER_RULE_COUNT, pat, 0, 100);
  }

  // ---- the stream format of the lists (shared by both filters' files) ----
  //   8 x path (u8 len + 4) ; 8 x sender (name 16, u16 secs, u8 prob) ; 8 x text (24, u16, u8) ;
  //   u8 n, n x (u8 len, key[len]) block

  template <typename Sink>
  void writeLists(Sink& w) const {
    for (int i = 0; i < FILTER_PATH_COUNT; i++) {
      uint8_t row[1 + FILTER_PATH_MAX_LEN];
      row[0] = path_block[i].len;
      memcpy(&row[1], path_block[i].bytes, FILTER_PATH_MAX_LEN);
      w.put(row, sizeof(row));
    }
    for (int i = 0; i < FILTER_RULE_COUNT; i++) {
      w.put(sender_rules[i].name, FILTER_SENDER_LEN);
      put16(w, sender_rules[i].secs);
      w.put(&sender_rules[i].prob, 1);
    }
    for (int i = 0; i < FILTER_RULE_COUNT; i++) {
      w.put(text_rules[i].text, FILTER_TEXT_LEN);
      put16(w, text_rules[i].secs);
      w.put(&text_rules[i].prob, 1);
    }
    uint8_t nb = (uint8_t)FilterBlock::count(block, FILTER_BLOCK_COUNT);
    w.put(&nb, 1);
    for (int i = 0; i < FILTER_BLOCK_COUNT; i++) {
      if (block[i].len == 0) continue;
      w.put(&block[i].len, 1);
      w.put(block[i].key, block[i].len);
    }
  }

  template <typename Src>
  bool readLists(Src& r) {
    bool ok = true;
    for (int i = 0; i < FILTER_PATH_COUNT && ok; i++) {
      uint8_t row[1 + FILTER_PATH_MAX_LEN];
      ok = r.get(row, sizeof(row));
      path_block[i].len = row[0];
      memcpy(path_block[i].bytes, &row[1], FILTER_PATH_MAX_LEN);
    }
    for (int i = 0; i < FILTER_RULE_COUNT && ok; i++) {
      ok = r.get(sender_rules[i].name, FILTER_SENDER_LEN) && get16(r, &sender_rules[i].secs) && r.get(&sender_rules[i].prob, 1);
    }
    for (int i = 0; i < FILTER_RULE_COUNT && ok; i++) {
      ok = r.get(text_rules[i].text, FILTER_TEXT_LEN) && get16(r, &text_rules[i].secs) && r.get(&text_rules[i].prob, 1);
    }
    uint8_t nb = 0;
    ok = ok && r.get(&nb, 1) && nb <= FILTER_BLOCK_COUNT;
    for (int i = 0; i < nb && ok; i++) {
      uint8_t len;
      uint8_t key[FILTER_BLOCK_MAX_LEN];
      ok = r.get(&len, 1) && len >= FILTER_BLOCK_MIN_LEN && len <= FILTER_BLOCK_MAX_LEN && r.get(key, len);
      if (ok) FilterBlock::addRaw(block, FILTER_BLOCK_COUNT, key, len);
    }
    return ok;
  }

  template <typename Sink> static void put16(Sink& w, uint16_t v) {
    uint8_t b[2] = { (uint8_t)v, (uint8_t)(v >> 8) };
    w.put(b, 2);
  }
  template <typename Src> static bool get16(Src& r, uint16_t* v) {
    uint8_t b[2];
    if (!r.get(b, 2)) return false;
    *v = (uint16_t)(b[0] | (b[1] << 8));
    return true;
  }

  // Splits `s` in place on single spaces (runs of spaces collapse). Returns the number of parts.
  static int split(char* s, const char** parts, int max) {
    int n = 0;
    while (*s && n < max) {
      while (*s == ' ') s++;
      if (!*s) break;
      parts[n++] = s;
      while (*s && *s != ' ') s++;
      if (*s) *s++ = '\0';
    }
    return n;
  }

  // The subcommands both filters have. `parts[0]` is "filter", `parts[1]` the subcommand.
  // Returns 1 when the command was handled and the settings changed, 0 when it was handled and
  // nothing changed, -1 when it is not one of these (the caller handles it).
  int runCmd(const char* const* parts, int n, char* reply, size_t cap, uint32_t now_secs) {
    if (n < 2) return -1;
    const char* sub = parts[1];

    if (strcmp(sub, "stats") == 0) {
      if (n < 3) return -1;
      const char* t = parts[2];
      if (strcmp(t, "malformed") == 0) FilterStat::formatStatsMalformed(reply, cap, cnt);
      else if (strcmp(t, "age") == 0) FilterStat::formatStatsAge(reply, cap, cnt, age_mins, MessageAge::clockSet(now_secs));
      else if (strcmp(t, "path") == 0) FilterStat::formatPathList(reply, cap, path_block, &cnt);
      else if (strcmp(t, "sender") == 0) FilterStat::formatRuleList(reply, cap, sender_rules, FILTER_RULE_COUNT, cnt.sender_slot, cnt.sender_pass);
      else if (strcmp(t, "text") == 0) FilterStat::formatRuleList(reply, cap, text_rules, FILTER_RULE_COUNT, cnt.text_slot, cnt.text_pass);
      else if (strcmp(t, "block") == 0) {
        int page = n >= 4 ? atoi(parts[3]) : 0;
        if (n >= 4 && page >= 0) FilterStat::formatBlockList(reply, cap, block, &cnt, page);
        else FilterStat::formatStatsBlock(reply, cap, cnt, FilterBlock::count(block, FILTER_BLOCK_COUNT));
      } else return -1;
      return 0;
    }

    if (strcmp(sub, "malformed") == 0) {
      if (n == 2) {
        snprintf(reply, cap, "> Filter: malformed text scan %s", malformed ? "on" : "off");
        return 0;
      }
      if (strcmp(parts[2], "on") == 0 || strcmp(parts[2], "off") == 0) {
        malformed = parts[2][1] == 'n';
        snprintf(reply, cap, "> Filter: malformed scan %s", malformed ? "on" : "off");
        return 1;
      }
      snprintf(reply, cap, "> Filter: syntax error 'filter malformed <on | off>'");
      return 0;
    }

    if (strcmp(sub, "age") == 0) {
      if (n == 2) {
        FilterStat::formatStatsAge(reply, cap, cnt, age_mins, MessageAge::clockSet(now_secs));
        return 0;
      }
      uint16_t mins;
      if (!MessageAge::parseMins(parts[2], &mins)) {
        snprintf(reply, cap, "> Filter: error <minutes> range is 1-10080, or off");
        return 0;
      }
      age_mins = mins;
      snprintf(reply, cap, "> Filter: OK");
      return 1;
    }

    if (strcmp(sub, "path") == 0) {
      char hex[2 * FILTER_PATH_MAX_LEN + 1];
      if (n >= 3 && strcmp(parts[2], "list") == 0) {
        FilterStat::formatPathList(reply, cap, path_block, nullptr);
        return 0;
      }
      bool is_add = n >= 4 && strcmp(parts[2], "add") == 0;
      if (is_add || (n >= 4 && strcmp(parts[2], "remove") == 0)) {
        bool ok = is_add ? FilterPath::add(path_block, FILTER_PATH_COUNT, parts[3], hex)
                         : FilterPath::remove(path_block, FILTER_PATH_COUNT, parts[3], hex);
        if (ok) {
          snprintf(reply, cap, "> Filter: path %s %s", hex, is_add ? "added" : "removed");
          return 1;
        }
        snprintf(reply, cap, hex[0] == '\0' ? "> Filter: error path prefix is 2-8 hex digits" : "Failed");
        return 0;
      }
      snprintf(reply, cap, "> filter path [ list | add | remove ] <hex prefix, 2-8 digits>");
      return 0;
    }

    if (strcmp(sub, "sender") == 0 || strcmp(sub, "text") == 0) {
      bool is_sender = strcmp(sub, "sender") == 0;
      if (n >= 3 && strcmp(parts[2], "list") == 0) {
        if (is_sender) FilterStat::formatRuleList(reply, cap, sender_rules, FILTER_RULE_COUNT, nullptr, nullptr);
        else FilterStat::formatRuleList(reply, cap, text_rules, FILTER_RULE_COUNT, nullptr, nullptr);
        return 0;
      }
      if (n >= 4 && strcmp(parts[2], "add") == 0) {
        uint16_t secs;
        uint8_t prob;
        size_t max_len = is_sender ? FILTER_SENDER_LEN - 1 : FILTER_TEXT_LEN - 1;
        if (strlen(parts[3]) > max_len) {
          snprintf(reply, cap, "> Filter: error <pattern> max %u chars", (unsigned)max_len);
        } else if (!FilterRules::parseArgs(n >= 5 ? parts[4] : nullptr, n >= 6 ? parts[5] : nullptr, &secs, &prob)) {
          snprintf(reply, cap, "> Filter: error <secs> range is 0-65535, <prob> range is 1-100");
        } else if (is_sender ? FilterRules::addRule(sender_rules, FILTER_RULE_COUNT, parts[3], secs, prob)
                             : FilterRules::addRule(text_rules, FILTER_RULE_COUNT, parts[3], secs, prob)) {
          snprintf(reply, cap, "> Filter: %s %s added", sub, parts[3]);
          return 1;
        } else {
          snprintf(reply, cap, "Failed");
        }
        return 0;
      }
      if (n >= 4 && strcmp(parts[2], "remove") == 0) {
        int slot = is_sender ? FilterRules::removeRule(sender_rules, FILTER_RULE_COUNT, parts[3])
                             : FilterRules::removeRule(text_rules, FILTER_RULE_COUNT, parts[3]);
        if (slot >= 0) {
          (is_sender ? sender_last : text_last)[slot] = 0;
          snprintf(reply, cap, "> Filter: %s %s removed", sub, parts[3]);
          return 1;
        }
        snprintf(reply, cap, "Failed");
        return 0;
      }
      snprintf(reply, cap, "> filter sender|text [ list | add <pattern> [secs] [prob] | remove <pattern> ]");
      return 0;
    }

    if (strcmp(sub, "block") == 0) {
      char hex[2 * FILTER_BLOCK_MAX_LEN + 1];
      if (n >= 3 && strcmp(parts[2], "list") == 0) {
        // `list [page] [full]`: `full` prints whole keys, two a page, for a backup
        bool full = n >= 5 && strcmp(parts[4], "full") == 0;
        FilterStat::formatBlockList(reply, cap, block, nullptr, n >= 4 ? atoi(parts[3]) : 0, full);
        return 0;
      }
      if (n >= 3 && strcmp(parts[2], "clear") == 0) {
        bool any = hasBlock();
        memset(block, 0, sizeof(block));
        snprintf(reply, cap, "> Filter: block list cleared");
        return any ? 1 : 0;
      }
      bool is_add = n >= 4 && strcmp(parts[2], "add") == 0;
      if (is_add || (n >= 4 && strcmp(parts[2], "remove") == 0)) {
        bool ambiguous = false;
        bool ok = is_add ? FilterBlock::add(block, FILTER_BLOCK_COUNT, parts[3], hex)
                         : FilterBlock::remove(block, FILTER_BLOCK_COUNT, parts[3], hex, nullptr, &ambiguous);
        if (ok) {
          snprintf(reply, cap, "> Filter: block %.12s%s %s", hex, strlen(hex) > 12 ? ".." : "", is_add ? "added" : "removed");
          return 1;
        }
        if (hex[0] == '\0') snprintf(reply, cap, "> Filter: error key is 8-64 hex digits");
        else if (ambiguous) snprintf(reply, cap, "> Filter: error more than one key starts with that, add digits");
        else if (is_add && FilterBlock::count(block, FILTER_BLOCK_COUNT) >= FILTER_BLOCK_COUNT) snprintf(reply, cap, "> Filter: error block list is full (%d)", FILTER_BLOCK_COUNT);
        else snprintf(reply, cap, "Failed");
        return 0;
      }
      snprintf(reply, cap, "> filter block [ list [page] [full] | add | remove | clear ] <hex key, 8-64 digits>");
      return 0;
    }

    return -1;
  }
};
