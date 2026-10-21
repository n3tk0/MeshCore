#pragma once

// "Staged" LoRa OTA install, done by our OTAFIX bootloader (bootloader/ in this repo).
// The same file is in the bootloader as src/ota_stage.h: keep both the same.
//
// The app puts the whole new image somewhere the copy does not touch (the QSPI flash, or the internal
// flash after the running image), writes a record and resets. The bootloader checks the image and
// copies it over the app. The source stays as it is, so a power cut only makes the copy start again.
//
// Rollback (QSPI only): the app also keeps the running image in the QSPI flash. The new image then
// runs "on trial": if the app does not confirm it within OTA_STAGE_TRIAL_BOOTS starts, or asks for it
// (`ota rollback`), the bootloader copies the old image back.
//
// Records: slots of 128 bytes in the bootloader settings page, after bootloader_settings_t. Each is
// written once; its state words only go from 0xFFFFFFFF to 0 (no erase). The last valid slot counts.
// A normal DFU (UF2, BLE) erases the page and with it every record.

#include <stdint.h>

#define OTA_STAGE_PAGE          0xFF000u   // bootloader settings page
#define OTA_STAGE_FIRST         0x400u     // offset of the first slot (the settings use 28 bytes)
#define OTA_STAGE_SLOT          128u
#define OTA_STAGE_SLOTS         ((0x1000u - OTA_STAGE_FIRST) / OTA_STAGE_SLOT)   // 24
#define OTA_STAGE_MAGIC         0x5453434Du   // "MCST"
#define OTA_STAGE_VERSION       1u
#define OTA_STAGE_TRIAL_BOOTS   3u

// flags
#define OTA_STAGE_SRC_QSPI      0x01u   // src (and backup) are addresses in the QSPI flash
#define OTA_STAGE_BACKUP        0x02u   // the old image is at backup: run the new one on trial

#define OTA_STAGE_SET           0u            // a state word that happened
#define OTA_STAGE_UNSET         0xFFFFFFFFu   // ... and one that did not

typedef struct {
  uint32_t magic;
  uint32_t version;       // OTA_STAGE_VERSION
  uint32_t flags;
  uint32_t dst;           // where the app starts (must be the bootloader's app start)
  uint32_t src;           // the new image
  uint32_t size;          // bytes
  uint32_t crc;           // CRC-32 (IEEE, as zlib) of the new image
  uint32_t backup;        // the old image (OTA_STAGE_BACKUP)
  uint32_t backup_size;
  uint32_t backup_crc;
  uint32_t head_crc;      // CRC-32 of the words above
  // state, written by:
  uint32_t started;       // bootloader: copying the new image
  uint32_t installed;     // bootloader: the new image is in place and checked
  uint32_t boots;         // bootloader: one bit cleared per start on trial (lowest first)
  uint32_t confirmed;     // app: the new image works
  uint32_t rb_request;    // app: go back to the old image
  uint32_t rb_started;    // bootloader: copying the old image back
  uint32_t rb_done;       // bootloader: the old image is back in place
  uint32_t failed;        // bootloader: gave up (bad source), the app was not touched
  uint32_t reserved[13];
} ota_stage_t;

// The bootloader sets this in its own image so the app knows it can do a staged install: a word at
// OTA_STAGE_BL_INFO_ADDR, inside the bootloader's flash and read only. Old bootloaders never have it there.
#define OTA_STAGE_BL_INFO_MAGIC 0x31544F4Du   // "MOT1"
#define OTA_STAGE_BL_INFO_ADDR  0xFD7F8u      // last 8 bytes before the bootloader config (0xFD800)
// word at +4: features, bit 0 = staged install, bit 1 = QSPI source and rollback

static inline uint32_t ota_stage_crc32(uint32_t crc, const uint8_t* p, uint32_t len) {
  crc = ~crc;
  while (len--) {
    crc ^= *p++;
    for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}

static inline uint32_t ota_stage_head_crc(const ota_stage_t* r) {
  return ota_stage_crc32(0, (const uint8_t*) r, (uint32_t)((const uint8_t*) &r->head_crc - (const uint8_t*) r));
}

static inline int ota_stage_valid(const ota_stage_t* r) {
  return r->magic == OTA_STAGE_MAGIC && r->version == OTA_STAGE_VERSION && r->head_crc == ota_stage_head_crc(r);
}

// the slot in use (last valid one), or 0
static inline const ota_stage_t* ota_stage_find(void) {
  const ota_stage_t* found = 0;
  for (uint32_t i = 0; i < OTA_STAGE_SLOTS; i++) {
    const ota_stage_t* r = (const ota_stage_t*)(uintptr_t)(OTA_STAGE_PAGE + OTA_STAGE_FIRST + i * OTA_STAGE_SLOT);
    if (r->magic == 0xFFFFFFFFu) break;   // slots are used in order
    if (ota_stage_valid(r)) found = r;
  }
  return found;
}

// the first slot never written, or 0 when the page is full
static inline ota_stage_t* ota_stage_free_slot(void) {
  for (uint32_t i = 0; i < OTA_STAGE_SLOTS; i++) {
    ota_stage_t* r = (ota_stage_t*)(uintptr_t)(OTA_STAGE_PAGE + OTA_STAGE_FIRST + i * OTA_STAGE_SLOT);
    const uint32_t* w = (const uint32_t*) r;
    int empty = 1;
    for (uint32_t k = 0; k < OTA_STAGE_SLOT / 4; k++) if (w[k] != 0xFFFFFFFFu) { empty = 0; break; }
    if (empty) return r;
  }
  return 0;
}

static inline uint32_t ota_stage_boots_used(const ota_stage_t* r) {
  uint32_t n = 0;
  for (uint32_t b = r->boots; (b & 1u) == 0 && n < 32; b >>= 1) n++;
  return n;
}

// the new image runs on trial: not confirmed yet, the bootloader may still go back
static inline int ota_stage_on_trial(const ota_stage_t* r) {
  return (r->flags & OTA_STAGE_BACKUP) && r->installed == OTA_STAGE_SET && r->confirmed == OTA_STAGE_UNSET
         && r->rb_started == OTA_STAGE_UNSET && r->failed == OTA_STAGE_UNSET;
}
