#pragma once
// Writes an image into the MAIN flash of an MSPM0 (L130x/L110x: one bank, 1 KB sectors, 64-bit words)
// over SWD: halt the core, erase the sectors the image covers, program word by word through FLASHCTL,
// read back, then reset the chip so it runs the new firmware.
#include "Swd.h"

#define MSPM0_SECTOR        1024u

struct Mspm0Info {
  uint32_t dpidr;
  uint32_t deviceid;     // FACTORYREGION DEVICEID (JTAG ID code of the part)
  uint32_t userid;
  uint16_t flash_kb;     // MAIN flash size
  uint16_t sram_kb;
};

typedef void (*Mspm0Progress)(const char* step, uint32_t done, uint32_t total);

class Mspm0Flash {
  Swd& _swd;
  SwdPins& _pins;
  int flashCmd(uint32_t cmdtype, uint32_t addr, const uint32_t* data);
  int unprotect(uint32_t addr);

public:
  uint32_t verify_errors = 0;
  uint32_t fail_addr = 0;

  Mspm0Flash(Swd& swd, SwdPins& pins) : _swd(swd), _pins(pins) { }

  // connect and halt; on failure pulses NRST (when wired) and catches the core right after reset
  int attach(Mspm0Info* info);
  int halt();
  int eraseSector(uint32_t addr);
  int programWord(uint32_t addr, uint32_t lo, uint32_t hi);
  // erase + program + verify; len is rounded up to 8 bytes with 0xFF. Sectors past the image are kept.
  int write(uint32_t addr, const uint8_t* data, uint32_t len, Mspm0Progress progress = nullptr);
  int verify(uint32_t addr, const uint8_t* data, uint32_t len);
  // let the core run the new firmware: debug off, then NRST pulse (or SYSRESETREQ without NRST)
  int run(bool have_nrst);
};
