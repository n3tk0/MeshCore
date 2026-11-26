#pragma once

#include "PacketFilter.h"
#include "FilterFileIO.h"

// Firmware side of the repeater / room server filter: the files. Written for this fork.
// Not built on `native` (it needs a file system); PacketFilter.h has no such dependency.
//
// Flash wear: the settings file is written only when the settings really changed (a signature
// of what would be written is compared), the first-seen table (/filter_seen) at most once an
// hour and only when it changed. Both go through a temp file renamed over the old one, so a reset
// in the middle of a write keeps the old file.

namespace FilterStore {

  using FilterFileIO::Sink;
  using FilterFileIO::Src;
  using FilterFileIO::openRead;
  using FilterFileIO::openWrite;

  template <typename FS>
  bool replace(FS* fs, const char* tmp, const char* path) {
    fs->remove(path);
    return fs->rename(tmp, path);
  }

  template <typename FS>
  bool savePrefs(FS* fs, PacketFilter& f) {
    if (fs == nullptr) return false;
    File file = openWrite(fs, FILTER_PREFS_FILE ".tmp");
    if (!file) return false;
    Sink s(file);
    f.writePrefs(s);
    s.flush();
    file.close();
    if (!s.ok) {
      fs->remove(FILTER_PREFS_FILE ".tmp");
      return false;
    }
    if (!replace(fs, FILTER_PREFS_FILE ".tmp", FILTER_PREFS_FILE)) return false;
    f.markPrefsSaved();
    return true;
  }

  template <typename FS>
  bool saveSeen(FS* fs, PacketFilter& f, uint32_t now_ms) {
    if (fs == nullptr) return false;
    File file = openWrite(fs, FILTER_SEEN_FILE ".tmp");
    if (!file) return false;
    Sink s(file);
    uint8_t hdr[6];
    f.newkey.header(hdr);
    s.put(hdr, sizeof(hdr));
    for (int i = 0; i < f.newkey.count(); i++) {
      uint8_t row[8];
      f.newkey.entry(i, row);
      s.put(row, sizeof(row));
    }
    s.flush();
    file.close();
    if (!s.ok) {
      fs->remove(FILTER_SEEN_FILE ".tmp");
      return false;
    }
    if (!replace(fs, FILTER_SEEN_FILE ".tmp", FILTER_SEEN_FILE)) return false;
    f.newkey.markSaved(now_ms);
    return true;
  }

  // At boot: read both files (missing = defaults) and start the hourly write clock.
  template <typename FS>
  void begin(FS* fs, PacketFilter& f, uint32_t now_ms, uint32_t now_secs) {
    f.seed(now_secs ^ now_ms);
    f.newkey.setSaveClock(now_ms);
    if (fs == nullptr) return;
    if (fs->exists(FILTER_PREFS_FILE)) {
      File file = openRead(fs, FILTER_PREFS_FILE);
      if (file) {
        Src r(file);
        f.readPrefs(r);   // a damaged file leaves the defaults (off)
        file.close();
      }
    }
    f.markPrefsSaved();
    if (fs->exists(FILTER_SEEN_FILE)) {
      File file = openRead(fs, FILTER_SEEN_FILE);
      if (file) {
        Src r(file);
        uint8_t hdr[6];
        int n = r.get(hdr, sizeof(hdr)) ? f.newkey.loadBegin(hdr) : f.newkey.loadBegin((const uint8_t*)"\0\0\0\0\0\0");
        for (int i = 0; i < n; i++) {
          uint8_t row[8];
          if (!r.get(row, sizeof(row))) { f.newkey.loadBegin((const uint8_t*)"\0\0\0\0\0\0"); break; }   // damaged: start empty
          f.newkey.loadEntry(row);
        }
        file.close();
      }
    }
  }

  // After a `filter ...` command: write what the command says has to be written.
  template <typename FS>
  void afterCommand(FS* fs, PacketFilter& f, int saves, uint32_t now_ms) {
    if ((saves & PacketFilter::SAVE_PREFS) && f.prefsChanged()) savePrefs(fs, f);
    if (saves & PacketFilter::SAVE_SEEN) saveSeen(fs, f, now_ms);
  }

  // From the main loop: the hourly write of the first-seen table. `can_write` is false while the
  // battery is low (no flash writes then).
  template <typename FS>
  void loop(FS* fs, PacketFilter& f, uint32_t now_ms, bool can_write) {
    if (can_write && f.newkey.saveDue(now_ms)) {
      if (!saveSeen(fs, f, now_ms)) f.newkey.retryLater(now_ms);   // failed: try again in an hour, not every loop
    }
  }
}
