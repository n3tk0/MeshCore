#pragma once
// SWD host over bit-banged pins (ARM ADIv5 SW-DP), and a 32-bit MEM-AP on top of it.
// Pure logic over SwdPins, so the host tests drive it against a simulated target.
#include <stdint.h>
#include <stddef.h>

class SwdPins {
public:
  // one clock cycle: SWCLK low, (host drives SWDIO = bit), SWCLK high; the target samples on the rise
  virtual void clockOut(bool bit) = 0;
  // one clock cycle with SWDIO released: the target drives it, read before the rising edge
  virtual bool clockIn() = 0;
  virtual void release() = 0;   // SWDIO to input (turnaround to the target)
  virtual void drive() = 0;     // SWDIO to output (turnaround to the host)
  virtual void reset(bool low) { (void)low; }   // NRST, if wired
  virtual void delayMs(uint32_t ms) { (void)ms; }
  virtual ~SwdPins() { }
};

enum SwdResult : int8_t {
  SWD_OK = 0,
  SWD_WAIT = -1,       // still WAIT after the retries
  SWD_FAULT = -2,      // sticky error in CTRL/STAT (cleared with ABORT)
  SWD_NO_ACK = -3,     // nothing answered (not wired, no power, or not SWD)
  SWD_PARITY = -4,     // read data parity wrong
  SWD_TIMEOUT = -5,    // a poll (power-up, halt, flash command) did not finish
  SWD_VERIFY = -6,     // read back differs
  SWD_FLASH = -7,      // FLASHCTL reported a failed command
  SWD_LOCKED = -8,     // AHB-AP not usable (debug disabled in NONMAIN)
};

const char* swdError(int r);

class Swd {
  SwdPins& _p;
  uint32_t _select;    // DP SELECT as last written (0xFFFFFFFF = unknown)
  uint32_t _csw;       // AP CSW as last written
  uint32_t _tar;       // AP TAR as the target has it (auto increment followed), 0xFFFFFFFF = unknown

  int xfer(bool ap, bool read, uint8_t addr, uint32_t* data);
  int selectAp(uint8_t ap, uint8_t reg);

public:
  uint32_t idcode = 0;
  uint32_t waits = 0, retries = 0;   // counters for the status line

  explicit Swd(SwdPins& p) : _p(p), _select(0xFFFFFFFF), _csw(0), _tar(0xFFFFFFFF) { }

  // line reset + JTAG-to-SWD + DPIDR read + debug power-up
  int connect();
  int dpRead(uint8_t addr, uint32_t* v);
  int dpWrite(uint8_t addr, uint32_t v);
  int apRead(uint8_t ap, uint8_t reg, uint32_t* v);
  int apWrite(uint8_t ap, uint8_t reg, uint32_t v);
  int clearErrors();

  // AHB-AP (AP 0) 32-bit memory access
  int read32(uint32_t addr, uint32_t* v);
  int write32(uint32_t addr, uint32_t v);
  int readBlock(uint32_t addr, uint32_t* v, size_t words);
  void forget() { _select = 0xFFFFFFFF; _csw = 0; _tar = 0xFFFFFFFF; }
};
