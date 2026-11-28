#pragma once

// What the bootloader's INFO_UF2.TXT says, read from the bootloader's own flash by the running app,
// so it can be seen without a double reset (CLI `bootloader`, the Faketec About screen).
//
// The Adafruit (and OTAFIX) bootloader keeps the text of INFO_UF2.TXT as initialised data in its image:
//   "UF2 Bootloader <version> lib/nrfx (...) ...\r\nModel: <name>\r\nBoard-ID: <id>\r\nDate: Mmm dd yyyy\r\n"
// (the "SoftDevice:" line is added at run time, so the SoftDevice is read from its own info struct).
// Ours (bootloader/ in this repo) also has the MOT1 word at OTA_STAGE_BL_INFO_ADDR.

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include "OtaStage.h"

enum BootloaderKind : uint8_t {
  BL_NONE,      // no bootloader start in UICR / MBR
  BL_OTHER,     // a bootloader without the UF2 info text (e.g. Nordic's)
  BL_ADAFRUIT,  // Adafruit UF2 bootloader
  BL_OTAFIX,    // oltaco's OTAFIX (LoRa OTA works, old RAM copy: no power-cut protection or rollback)
  BL_OURS,      // our OTAFIX with the staged install (MOT1)
};

struct BootloaderInfo {
  BootloaderKind kind;
  uint32_t start;        // bootloader start address
  uint32_t features;     // ours: bit 0 staged install, bit 1 QSPI source and rollback
  char version[40];      // "0.9.2-OTAFIX2.3-BP1.4-mcota1"
  char model[32];        // "Seeed XIAO nRF52840"
  char board[36];        // "nRF52840-SeeedXiaoSense-v1"
  char date[12];         // "2026-10-22"
  char softdevice[16];   // "S140/6.1.1", "" when none
};

static inline const char* bootloaderKindName(BootloaderKind k) {
  switch (k) {
    case BL_OURS: return "ours";
    case BL_OTAFIX: return "otafix";
    case BL_ADAFRUIT: return "adafruit";
    case BL_OTHER: return "other";
    default: return "none";
  }
}

// copies flash text up to a CR/LF/NUL (and, if stop_space, a space); always terminates
static inline void bl_copyField(char* out, size_t n, const char* p, const char* end, bool stop_space) {
  size_t i = 0;
  while (p < end && i + 1 < n && *p != '\r' && *p != '\n' && *p != 0 && !(stop_space && *p == ' ')) {
    char c = *p++;
    out[i++] = (c >= 0x20 && c < 0x7F) ? c : '?';
  }
  out[i] = 0;
}

// finds "<key>" after `from` (not past `end` or the info text's NUL) and returns what follows
static inline const char* bl_findLine(const char* from, const char* end, const char* key) {
  size_t kl = strlen(key);
  for (const char* p = from; p + kl <= end; p++) {
    if (*p == 0) return NULL;   // the info text ends at its NUL
    if (memcmp(p, key, kl) == 0) return p + kl;
  }
  return NULL;
}

// "Oct 22 2026" (or "Oct  2 2026") -> "2026-10-22"; else copied as it is
static inline void bl_isoDate(char* out, size_t n, const char* d) {
  static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
  int m = 0;
  if (strlen(d) >= 11) for (int i = 0; i < 12; i++) if (memcmp(d, &months[i * 3], 3) == 0) { m = i + 1; break; }
  int day = 0, year = 0;
  if (m && strlen(d) >= 11 && sscanf(d + 3, "%d %d", &day, &year) == 2 && day >= 1 && day <= 31 && year > 2000) {
    snprintf(out, n, "%04d-%02d-%02d", year, m, day);
  } else {
    snprintf(out, n, "%s", d);
  }
}

// Looks for the INFO_UF2.TXT text in [begin, end): on a hit fills version, model, board, date and sets
// kind to BL_ADAFRUIT or BL_OTAFIX; returns false (bi unchanged) when it is not there.
static inline bool bl_parseInfoText(const char* begin, const char* end, BootloaderInfo& bi) {
  static const char MARK[] = "UF2 Bootloader ";
  const size_t ml = sizeof(MARK) - 1;
  const char* p = begin;
  for (; p + ml <= end; p++) {
    if (*p == 'U' && memcmp(p, MARK, ml) == 0) break;
  }
  if (p + ml > end) return false;
  bi.kind = BL_ADAFRUIT;
  const char* v = p + ml;
  bl_copyField(bi.version, sizeof(bi.version), v, end, true);
  const char* q = bl_findLine(v, end, "Model: ");
  if (q) bl_copyField(bi.model, sizeof(bi.model), q, end, false);
  q = bl_findLine(v, end, "Board-ID: ");
  if (q) bl_copyField(bi.board, sizeof(bi.board), q, end, false);
  q = bl_findLine(v, end, "Date: ");
  if (q) {
    char d[16] = {};
    bl_copyField(d, sizeof(d), q, end, false);
    bl_isoDate(bi.date, sizeof(bi.date), d);
  }
  if (strstr(bi.version, "OTAFIX") != NULL) bi.kind = BL_OTAFIX;
  return true;
}

static inline void readBootloaderInfo(BootloaderInfo& bi) {
  memset(&bi, 0, sizeof(bi));
  // UICR.NRFFW[0] (0x10001014) is where the MBR starts the bootloader; else the MBR's own setting (0xFF8)
  uintptr_t start = *(const volatile uint32_t*)0x10001014u;
  if (start == 0xFFFFFFFFu || start == 0) start = *(const volatile uint32_t*)0x00000FF8u;
  if (start == 0xFFFFFFFFu || start == 0 || start < 0x26000u || start >= 0xFE000u) {
    bi.kind = BL_NONE;
  } else {
    bi.start = (uint32_t)start;
    bi.kind = BL_OTHER;
    // the bootloader ends at the MBR parameter page (0xFE000); the settings come after it
    bl_parseInfoText((const char*)start, (const char*)(uintptr_t)0xFE000u, bi);
    const volatile uint32_t* mot = (const volatile uint32_t*)OTA_STAGE_BL_INFO_ADDR;
    if (mot[0] == OTA_STAGE_BL_INFO_MAGIC) {
      bi.kind = BL_OURS;
      bi.features = mot[1];
    }
  }
  // SoftDevice info struct: MBR (0x1000) + 0x2000; size byte, magic 0x51B1E5DB, id at +0x10, version at +0x14
  const volatile uint32_t* sd = (const volatile uint32_t*)0x3000u;
  if (sd[1] == 0x51B1E5DBu && (sd[0] & 0xFF) > 0x14) {
    uint32_t id = sd[4], ver = sd[5];
    snprintf(bi.softdevice, sizeof(bi.softdevice), "S%lu/%lu.%lu.%lu", (unsigned long)id,
             (unsigned long)(ver / 1000000), (unsigned long)(ver / 1000 % 1000), (unsigned long)(ver % 1000));
  }
}

// One line for the CLI (fits 155 chars): kind first, then key=value words without spaces, e.g.
// "ours ver=0.9.2-OTAFIX2.3-BP1.4-mcota1 board=nRF52840-SeeedXiaoSense-v1 date=2026-10-22 sd=S140/6.1.1 feat=staged,rollback"
static inline void formatBootloaderInfo(const BootloaderInfo& bi, char* out, size_t n) {
  int k = snprintf(out, n, "%s", bootloaderKindName(bi.kind));
#define BL_ADD(...) do { if (k >= 0 && (size_t)k < n) k += snprintf(out + k, n - k, __VA_ARGS__); } while (0)
  if (bi.version[0]) BL_ADD(" ver=%s", bi.version);
  if (bi.board[0]) {
    char b[sizeof(bi.board)];
    strcpy(b, bi.board);
    for (char* c = b; *c; c++) if (*c == ' ') *c = '_';   // e.g. "TRACKER L1"
    BL_ADD(" board=%s", b);
  }
  if (bi.date[0]) BL_ADD(" date=%s", bi.date);
  if (bi.kind == BL_OTHER) BL_ADD(" at=%05lX", (unsigned long)bi.start);
  if (bi.softdevice[0]) BL_ADD(" sd=%s", bi.softdevice);
  if (bi.kind == BL_OURS) {
    BL_ADD(" feat=%s%s", (bi.features & 1) ? "staged" : "-", (bi.features & 2) ? ",rollback" : "");
  }
#undef BL_ADD
}

static inline void formatBootloaderInfo(char* out, size_t n) {
  BootloaderInfo bi;
  readBootloaderInfo(bi);
  formatBootloaderInfo(bi, out, n);
}
