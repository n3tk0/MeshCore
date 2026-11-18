#include "Swd.h"

#define DP_DPIDR     0x0   // read
#define DP_ABORT     0x0   // write
#define DP_CTRLSTAT  0x4
#define DP_SELECT    0x8   // write
#define DP_RDBUFF    0xC   // read

#define AP_CSW       0x00
#define AP_TAR       0x04
#define AP_DRW       0x0C

// 32-bit, auto increment, debug master with privileged data access (as pyOCD and OpenOCD use)
#define CSW_WORD_INC 0x23000052u

#define WAIT_RETRIES 200

const char* swdError(int r) {
  switch (r) {
    case SWD_OK: return "ok";
    case SWD_WAIT: return "target busy (WAIT)";
    case SWD_FAULT: return "SWD fault";
    case SWD_NO_ACK: return "no answer on SWD (wiring, power?)";
    case SWD_PARITY: return "SWD parity error";
    case SWD_TIMEOUT: return "timeout";
    case SWD_VERIFY: return "verify failed";
    case SWD_FLASH: return "flash command failed";
    case SWD_LOCKED: return "debug access locked";
    default: return "error";
  }
}

static bool parity32(uint32_t v) {
  v ^= v >> 16; v ^= v >> 8; v ^= v >> 4; v ^= v >> 2; v ^= v >> 1;
  return v & 1;
}

int Swd::xfer(bool ap, bool read, uint8_t addr, uint32_t* data) {
  for (int attempt = 0; attempt < WAIT_RETRIES; attempt++) {
    bool a2 = addr & 4, a3 = addr & 8;
    bool par = ap ^ read ^ a2 ^ a3;
    _p.drive();
    _p.clockOut(1);      // start
    _p.clockOut(ap);
    _p.clockOut(read);
    _p.clockOut(a2);
    _p.clockOut(a3);
    _p.clockOut(par);
    _p.clockOut(0);      // stop
    _p.clockOut(1);      // park
    _p.release();
    _p.clockIn();        // turnaround
    uint8_t ack = 0;
    for (int i = 0; i < 3; i++) ack |= (uint8_t)_p.clockIn() << i;

    if (ack == 1) {      // OK
      if (read) {
        uint32_t v = 0;
        for (int i = 0; i < 32; i++) v |= (uint32_t)_p.clockIn() << i;
        bool p = _p.clockIn();
        _p.clockIn();    // turnaround back to the host
        _p.drive();
        for (int i = 0; i < 8; i++) _p.clockOut(0);
        if (p != parity32(v)) return SWD_PARITY;
        *data = v;
      } else {
        _p.clockIn();    // turnaround
        _p.drive();
        uint32_t v = *data;
        for (int i = 0; i < 32; i++) _p.clockOut((v >> i) & 1);
        _p.clockOut(parity32(v));
        for (int i = 0; i < 8; i++) _p.clockOut(0);
      }
      return SWD_OK;
    }
    _p.clockIn();        // turnaround (no data phase: overrun detection is off)
    _p.drive();
    for (int i = 0; i < 8; i++) _p.clockOut(0);
    if (ack == 2) {      // WAIT: try again
      waits++;
      continue;
    }
    if (ack == 4) return SWD_FAULT;
    return SWD_NO_ACK;
  }
  return SWD_WAIT;
}

int Swd::connect() {
  forget();
  _p.drive();
  for (int i = 0; i < 56; i++) _p.clockOut(1);                       // line reset
  const uint16_t jtag_to_swd = 0xE79E;
  for (int i = 0; i < 16; i++) _p.clockOut((jtag_to_swd >> i) & 1);
  for (int i = 0; i < 56; i++) _p.clockOut(1);                       // line reset again
  for (int i = 0; i < 4; i++) _p.clockOut(0);                        // idle
  int r = dpRead(DP_DPIDR, &idcode);
  if (r) return r;
  if (idcode == 0 || idcode == 0xFFFFFFFF) return SWD_NO_ACK;
  if ((r = clearErrors())) return r;
  if ((r = dpWrite(DP_SELECT, 0))) return r;
  _select = 0;
  if ((r = dpWrite(DP_CTRLSTAT, 0x50000000))) return r;              // CSYSPWRUPREQ | CDBGPWRUPREQ
  for (int i = 0; i < 100; i++) {
    uint32_t cs;
    if ((r = dpRead(DP_CTRLSTAT, &cs))) return r;
    if ((cs & 0xA0000000) == 0xA0000000) return SWD_OK;               // both ACKs
    _p.delayMs(1);
  }
  return SWD_TIMEOUT;
}

int Swd::dpRead(uint8_t addr, uint32_t* v) { return xfer(false, true, addr, v); }
int Swd::dpWrite(uint8_t addr, uint32_t v) { return xfer(false, false, addr, &v); }

int Swd::clearErrors() {
  forget();
  return dpWrite(DP_ABORT, 0x1E);   // STKCMPCLR | STKERRCLR | WDERRCLR | ORUNERRCLR
}

int Swd::selectAp(uint8_t ap, uint8_t reg) {
  uint32_t sel = ((uint32_t)ap << 24) | (reg & 0xF0);
  if (sel == _select) return SWD_OK;
  int r = dpWrite(DP_SELECT, sel);
  if (r == SWD_OK) _select = sel;
  return r;
}

int Swd::apRead(uint8_t ap, uint8_t reg, uint32_t* v) {
  int r = selectAp(ap, reg);
  if (r) return r;
  uint32_t dummy;
  if ((r = xfer(true, true, reg & 0x0C, &dummy))) return r;   // posted: the value comes with RDBUFF
  return dpRead(DP_RDBUFF, v);
}

int Swd::apWrite(uint8_t ap, uint8_t reg, uint32_t v) {
  int r = selectAp(ap, reg);
  if (r) return r;
  return xfer(true, false, reg & 0x0C, &v);
}

int Swd::read32(uint32_t addr, uint32_t* v) { return readBlock(addr, v, 1); }

int Swd::write32(uint32_t addr, uint32_t v) {
  int r;
  if (_csw != CSW_WORD_INC) {
    if ((r = apWrite(0, AP_CSW, CSW_WORD_INC))) return r;
    _csw = CSW_WORD_INC;
  }
  if (_tar != addr) {
    _tar = 0xFFFFFFFF;
    if ((r = apWrite(0, AP_TAR, addr))) return r;
  }
  if ((r = apWrite(0, AP_DRW, v))) { _tar = 0xFFFFFFFF; return r; }
  _tar = addr + 4;
  if ((_tar & 0x3FF) == 0) _tar = 0xFFFFFFFF;   // auto increment is only sure inside 1 KB
  return SWD_OK;
}

int Swd::readBlock(uint32_t addr, uint32_t* v, size_t words) {
  int r;
  if (_csw != CSW_WORD_INC) {
    if ((r = apWrite(0, AP_CSW, CSW_WORD_INC))) return r;
    _csw = CSW_WORD_INC;
  }
  while (words) {
    // one run inside a 1 KB block: TAR, then pipelined DRW reads (each returns the one before)
    size_t run = (0x400 - (addr & 0x3FF)) / 4;
    if (run > words) run = words;
    _tar = 0xFFFFFFFF;
    if ((r = apWrite(0, AP_TAR, addr))) return r;
    if ((r = selectAp(0, AP_DRW))) return r;
    uint32_t prev;
    if ((r = xfer(true, true, AP_DRW, &prev))) return r;
    for (size_t i = 1; i < run; i++) {
      if ((r = xfer(true, true, AP_DRW, &v[i - 1]))) return r;
    }
    if ((r = dpRead(DP_RDBUFF, &v[run - 1]))) return r;
    addr += run * 4;
    v += run;
    words -= run;
  }
  return SWD_OK;
}
