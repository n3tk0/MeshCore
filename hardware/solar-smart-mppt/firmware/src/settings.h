// Settings: defaults per chemistry, limits, and the flash store (two 1 KB sectors, A/B)
#pragma once
#include "smppt.h"

void settings_defaults(settings_t *s, uint8_t chem);

// Checks one field change before it is taken. Returns 0 or ERR_*.
// The limits come from the chemistry and the board (3 A fuse, 3.2 A shunt range, 5.5 V parts on SYS).
int settings_check(const settings_t *s, uint8_t active_chem);

// Chemistry to use: the setting, or JP4. A jumper that differs from the one saved with the record
// wins and resets the voltages to its defaults (protects a LiFePO4 from 4.2 V).
// Returns true when the settings were reset because of the jumper.
bool settings_resolve_chem(settings_t *s, uint8_t jumper, uint8_t *active);

// ---- flash store ----
// Records are appended to the active sector; when it is full the other sector is erased and the
// newest record of each type is copied there first, so a power cut never leaves no valid copy.
#define STORE_SECTOR_SIZE 1024u
#define REC_SETTINGS 1
#define REC_GAUGE    2

typedef struct {
  // erase one sector (addr = sector start); program len bytes (multiple of 8) at addr (8-aligned)
  bool (*erase)(uint32_t addr);
  bool (*program)(uint32_t addr, const void *data, uint32_t len);
  const uint8_t *(*ptr)(uint32_t addr);
  uint32_t base;            // two sectors from here
} flash_ops_t;

void store_init(const flash_ops_t *ops);
// copies the newest valid record of this type, returns false if none
bool store_load(uint8_t type, void *data, uint16_t len);
bool store_save(uint8_t type, const void *data, uint16_t len);
uint16_t crc16_ccitt(const void *data, uint32_t len);

// command errors (register 0x62 on 0x42)
enum {
  ERR_OK = 0, ERR_LOCKED = 1, ERR_RANGE = 2, ERR_FLASH = 3, ERR_BAD_REG = 4, ERR_BUSY = 5,
  ERR_UNKNOWN_CMD = 6, ERR_NOT_NOW = 7,
};
