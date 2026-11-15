#include "Mspm0Flash.h"
#include <string.h>

// Cortex-M0+ debug
#define DHCSR          0xE000EDF0u
#define DEMCR          0xE000EDFCu
#define AIRCR          0xE000ED0Cu
#define DBGKEY         0xA05F0000u
#define C_DEBUGEN      0x1u
#define C_HALT         0x2u
#define S_HALT         (1u << 17)
#define VC_CORERESET   0x1u

// MSPM0 factory region
#define FR_DEVICEID    0x41C40004u
#define FR_USERID      0x41C40008u
#define FR_SRAMFLASH   0x41C40018u

// FLASHCTL (GEN registers, base 0x400CD000)
#define FC             0x400CD000u
#define FC_CMDEXEC     (FC + 0x1100)
#define FC_CMDTYPE     (FC + 0x1104)
#define FC_CMDADDR     (FC + 0x1120)
#define FC_CMDBYTEN    (FC + 0x1124)
#define FC_CMDDATA0    (FC + 0x1130)
#define FC_CMDDATA1    (FC + 0x1134)
#define FC_CMDWEPROTA  (FC + 0x11D0)
#define FC_CMDWEPROTB  (FC + 0x11D4)
#define FC_STATCMD     (FC + 0x13D0)

#define CMD_PROGRAM    0x01u
#define CMD_ERASE      0x02u
#define CMD_CLEARSTAT  0x05u
#define SIZE_ONEWORD   0x00u
#define SIZE_SECTOR    0x40u
#define STAT_DONE      0x1u
#define STAT_PASS      0x2u
#define BYTEN_64       0xFFu   // 64 data bits, no ECC (the L130x has none)

int Mspm0Flash::halt() {
  int r;
  if ((r = _swd.write32(DHCSR, DBGKEY | C_DEBUGEN | C_HALT))) return r;
  for (int i = 0; i < 50; i++) {
    uint32_t v;
    if ((r = _swd.read32(DHCSR, &v))) return r;
    if (v & S_HALT) return SWD_OK;
    _pins.delayMs(1);
  }
  return SWD_TIMEOUT;
}

int Mspm0Flash::attach(Mspm0Info* info) {
  int r = _swd.connect();
  if (r == SWD_OK) r = halt();
  if (r != SWD_OK) {
    // a firmware asleep in a deep low-power mode or one that turned SWD pins into GPIO:
    // reset and catch the core before the firmware gets that far
    _pins.reset(true);
    _pins.delayMs(5);
    _pins.reset(false);
    for (int i = 0; i < 50 && r != SWD_OK; i++) {   // the boot code needs a few ms after NRST
      if (i) _pins.delayMs(1);
      r = _swd.connect();
      if (r == SWD_OK) r = halt();
    }
    if (r != SWD_OK) return r;
  }
  if ((r = _swd.write32(DEMCR, 0))) return r;
  memset(info, 0, sizeof(*info));
  info->dpidr = _swd.idcode;
  uint32_t sf;
  if ((r = _swd.read32(FR_DEVICEID, &info->deviceid))) return r;
  if ((r = _swd.read32(FR_USERID, &info->userid))) return r;
  if ((r = _swd.read32(FR_SRAMFLASH, &sf))) return r;
  info->flash_kb = sf & 0xFFF;
  info->sram_kb = (sf >> 16) & 0x3FF;
  return SWD_OK;
}

int Mspm0Flash::unprotect(uint32_t addr) {
  // the protection comes back after every command: open just this sector each time
  uint32_t s = addr / MSPM0_SECTOR;
  int r;
  if (s < 32) {
    if ((r = _swd.write32(FC_CMDWEPROTA, ~(1u << s)))) return r;
    return _swd.write32(FC_CMDWEPROTB, 0xFFFFFFFFu);
  }
  if ((r = _swd.write32(FC_CMDWEPROTA, 0xFFFFFFFFu))) return r;
  return _swd.write32(FC_CMDWEPROTB, ~(1u << ((s - 32) / 8)));   // B: 8 sectors a bit
}

int Mspm0Flash::flashCmd(uint32_t cmdtype, uint32_t addr, const uint32_t* data) {
  int r;
  if ((r = _swd.write32(FC_CMDTYPE, CMD_CLEARSTAT))) return r;
  if ((r = _swd.write32(FC_CMDEXEC, 1))) return r;
  if ((r = unprotect(addr))) return r;
  if ((r = _swd.write32(FC_CMDTYPE, cmdtype))) return r;
  if ((r = _swd.write32(FC_CMDADDR, addr))) return r;
  if (data) {
    if ((r = _swd.write32(FC_CMDBYTEN, BYTEN_64))) return r;
    if ((r = _swd.write32(FC_CMDDATA0, data[0]))) return r;
    if ((r = _swd.write32(FC_CMDDATA1, data[1]))) return r;
  }
  if ((r = _swd.write32(FC_CMDEXEC, 1))) return r;
  for (int i = 0; i < 200; i++) {   // a sector erase takes a few ms, a word ~50 us
    uint32_t st;
    if ((r = _swd.read32(FC_STATCMD, &st))) return r;
    if (st & STAT_DONE) {
      if (st & STAT_PASS) return SWD_OK;
      fail_addr = addr;
      return SWD_FLASH;
    }
    if (i > 2) _pins.delayMs(1);
  }
  fail_addr = addr;
  return SWD_TIMEOUT;
}

int Mspm0Flash::eraseSector(uint32_t addr) {
  return flashCmd(CMD_ERASE | SIZE_SECTOR, addr & ~(MSPM0_SECTOR - 1), nullptr);
}

int Mspm0Flash::programWord(uint32_t addr, uint32_t lo, uint32_t hi) {
  uint32_t d[2] = { lo, hi };
  return flashCmd(CMD_PROGRAM | SIZE_ONEWORD, addr, d);
}

static uint32_t le32(const uint8_t* p, uint32_t off, uint32_t len) {
  uint32_t v = 0;
  for (int i = 3; i >= 0; i--) v = (v << 8) | (off + i < len ? p[off + i] : 0xFF);
  return v;
}

int Mspm0Flash::write(uint32_t addr, const uint8_t* data, uint32_t len, Mspm0Progress progress) {
  if ((addr & 7) || len == 0) return SWD_FLASH;
  int r;
  uint32_t first = addr & ~(MSPM0_SECTOR - 1);
  uint32_t end = addr + len;
  uint32_t sectors = (end - first + MSPM0_SECTOR - 1) / MSPM0_SECTOR;
  for (uint32_t i = 0; i < sectors; i++) {
    if (progress) progress("erase", i, sectors);
    if ((r = eraseSector(first + i * MSPM0_SECTOR))) return r;
  }
  uint32_t words = (len + 7) / 8;
  for (uint32_t w = 0; w < words; w++) {
    uint32_t off = w * 8;
    uint32_t lo = le32(data, off, len), hi = le32(data, off + 4, len);
    if (progress && (w % 64) == 0) progress("write", w, words);
    if (lo == 0xFFFFFFFFu && hi == 0xFFFFFFFFu) continue;   // erased already
    if ((r = programWord(addr + off, lo, hi))) return r;
  }
  if (progress) progress("verify", 0, words);
  return verify(addr, data, len);
}

int Mspm0Flash::verify(uint32_t addr, const uint8_t* data, uint32_t len) {
  uint32_t buf[64];
  verify_errors = 0;
  for (uint32_t off = 0; off < len; off += sizeof(buf)) {
    uint32_t n = len - off < sizeof(buf) ? (len - off + 3) / 4 : 64;
    int r = _swd.readBlock(addr + off, buf, n);
    if (r) return r;
    for (uint32_t i = 0; i < n; i++) {
      if (buf[i] != le32(data, off + i * 4, len)) {
        if (!verify_errors) fail_addr = addr + off + i * 4;
        verify_errors++;
      }
    }
  }
  return verify_errors ? SWD_VERIFY : SWD_OK;
}

int Mspm0Flash::run(bool have_nrst) {
  int r;
  if ((r = _swd.write32(DEMCR, 0))) return r;
  if ((r = _swd.write32(DHCSR, DBGKEY))) return r;   // C_DEBUGEN off: the core runs
  if (have_nrst) {
    _pins.reset(true);
    _pins.delayMs(5);
    _pins.reset(false);
    return SWD_OK;
  }
  _swd.write32(AIRCR, 0x05FA0004u);   // SYSRESETREQ: the reset may cut the answer, so it is not checked
  return SWD_OK;
}
