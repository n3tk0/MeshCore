// Host tests: the SWD host and the MSPM0 flash writer against a bit-level simulated MSPM0
// (SW-DP + AHB-AP with 1 KB TAR wrap, Cortex-M0+ DHCSR, FLASHCTL with sector protection).
#include "../lib/mspm0swd/Swd.h"
#include "../lib/mspm0swd/Mspm0Flash.h"
#include "../lib/mspm0swd/Image.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <deque>

static int fails = 0, checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

static bool par32(uint32_t v) { int p = 0; while (v) { p ^= v & 1; v >>= 1; } return p; }

class SimMspm0 : public SwdPins {
public:
  // memory
  std::vector<uint8_t> flash = std::vector<uint8_t>(64 * 1024, 0xFF);
  uint32_t weprota = 0xFFFFFFFF, weprotb = 0xFFFFFFFF, cmdtype = 0, cmdaddr = 0, byten = 0, d0 = 0, d1 = 0;
  uint32_t statcmd = 0; int busy = 0;
  bool halted = false, debugen = false, asleep = false, running_after_reset = false;
  int nrst_pulses = 0, erases = 0, programs = 0, prot_fails = 0;
  bool in_reset = false;
  // DP / AP
  bool swd = false, powered = true;
  uint32_t ctrlstat = 0, select = 0, rdbuff = 0, csw = 0, tar = 0;
  bool sticky = false;
  int wait_every = 0, xfers = 0;
  // bit engine
  enum { IDLE, REQ, JUNK, WDATA } st = IDLE;
  int ones = 0, nreq = 0, nw = 0; uint8_t req = 0; uint64_t wbits = 0;
  bool after_reset = false, jtag_ok = false; int jbits = 0; uint16_t jval = 0;
  std::deque<bool> outq;
  bool driving_host = true;

  uint32_t mem_read(uint32_t a) {
    if (asleep) { sticky = true; return 0; }
    if (a < flash.size()) { uint32_t v; memcpy(&v, &flash[a], 4); return v; }
    switch (a) {
      case 0xE000EDF0: return (halted ? (1u << 17) : 0) | (debugen ? 1 : 0);
      case 0x41C40004: return 0x1BB8202F;   // made-up DEVICEID
      case 0x41C40008: return 0x12345678;
      case 0x41C40018: return (4u << 16) | 64;
      case 0x400CD000 + 0x13D0: if (busy) { busy--; return 0x4; } return statcmd;
      default: return 0;
    }
  }
  void mem_write(uint32_t a, uint32_t v) {
    if (asleep) { sticky = true; return; }
    switch (a) {
      case 0xE000EDF0:
        if ((v >> 16) == 0xA05F) { debugen = v & 1; halted = debugen && (v & 2); }
        return;
      case 0xE000EDFC: case 0xE000ED0C: return;
      case 0x400CD000 + 0x1104: cmdtype = v; return;
      case 0x400CD000 + 0x1120: cmdaddr = v; return;
      case 0x400CD000 + 0x1124: byten = v; return;
      case 0x400CD000 + 0x1130: d0 = v; return;
      case 0x400CD000 + 0x1134: d1 = v; return;
      case 0x400CD000 + 0x11D0: weprota = v; return;
      case 0x400CD000 + 0x11D4: weprotb = v; return;
      case 0x400CD000 + 0x1100: if (v & 1) exec(); return;
      default: if (a < flash.size()) sticky = true;   // flash is not writable from the bus
    }
  }
  bool prot(uint32_t addr) {
    uint32_t s = addr / 1024;
    return s < 32 ? (weprota >> s) & 1 : (weprotb >> ((s - 32) / 8)) & 1;
  }
  void exec() {
    if (!halted) { statcmd = 0x1; return; }   // only exercised with the core halted
    uint32_t c = cmdtype & 7;
    if (c == 5) { statcmd = 0; return; }
    if (c == 2 && (cmdtype & 0x70) == 0x40) {
      if (prot(cmdaddr)) { prot_fails++; statcmd = 0x1; }
      else { memset(&flash[cmdaddr & ~1023u], 0xFF, 1024); erases++; statcmd = 0x3; busy = 3; }
    } else if (c == 1) {
      if (prot(cmdaddr) || (cmdaddr & 7) || byten != 0xFF) { prot_fails++; statcmd = 0x1; }
      else {
        uint32_t w[2]; memcpy(w, &flash[cmdaddr], 8);
        w[0] &= d0; w[1] &= d1; memcpy(&flash[cmdaddr], w, 8);
        programs++; statcmd = 0x3; busy = 1;
      }
    } else statcmd = 0x1;
    weprota = weprotb = 0xFFFFFFFF;   // protection is back after every command
  }

  // SwdPins
  void drive() override { driving_host = true; }
  void release() override { driving_host = false; }
  void reset(bool low) override {
    if (low) { in_reset = true; return; }
    if (in_reset) { nrst_pulses++; asleep = false; halted = false; debugen = false; swd = false; running_after_reset = true; }
    in_reset = false;
  }
  bool clockIn() override {
    if (outq.empty()) return true;   // pull-up
    bool b = outq.front(); outq.pop_front(); return b;
  }
  void respond(int ack, bool read, uint32_t data) {
    outq.push_back(true);                                   // turnaround
    for (int i = 0; i < 3; i++) outq.push_back((ack >> i) & 1);
    if (ack == 1 && read) { for (int i = 0; i < 32; i++) outq.push_back((data >> i) & 1); outq.push_back(par32(data)); }
    outq.push_back(true);                                   // turnaround
  }
  void request() {
    bool ap = req & 2, rd = req & 4; uint8_t a = ((req >> 3) & 3) << 2;
    bool p = ((req >> 1) & 1) ^ ((req >> 2) & 1) ^ ((req >> 3) & 1) ^ ((req >> 4) & 1);
    if (!(req & 1) || p != ((req >> 5) & 1) || (req & 0x40) || !(req & 0x80) || !swd) { st = JUNK; return; }
    xfers++;
    if (wait_every && xfers % wait_every == 0) { respond(2, rd, 0); st = IDLE; return; }
    bool faulting = sticky && ap;
    if (faulting) { respond(4, rd, 0); st = IDLE; return; }
    if (rd) {
      uint32_t v = 0;
      if (!ap) {
        if (a == 0) v = 0x0BC11477;
        else if (a == 4) v = ctrlstat | ((ctrlstat & 0x50000000) << 1) | (sticky ? 0x20 : 0);
        else if (a == 0xC) { v = rdbuff; }
      } else {
        v = rdbuff;   // posted
        uint8_t reg = (select & 0xF0) | a;
        if ((select >> 24) == 0) {
          if (reg == 0x00) rdbuff = csw; else if (reg == 0x04) rdbuff = tar;
          else if (reg == 0x0C) { rdbuff = mem_read(tar); if (csw & 0x10) tar = (tar & ~1023u) | ((tar + 4) & 1023u); }
          else rdbuff = 0;
        }
      }
      respond(1, true, v);
      st = IDLE;
    } else {
      respond(1, false, 0);
      st = WDATA; nw = 0; wbits = 0;
      wap = ap; wa = a;
    }
  }
  bool wap = false; uint8_t wa = 0;
  void write_done(uint32_t v) {
    if (!wap) {
      if (wa == 0) { if (v & 0x1E) sticky = false; }
      else if (wa == 4) ctrlstat = v;
      else if (wa == 8) select = v;
      return;
    }
    uint8_t reg = (select & 0xF0) | wa;
    if ((select >> 24) != 0) return;
    if (reg == 0x00) csw = v; else if (reg == 0x04) tar = v;
    else if (reg == 0x0C) { mem_write(tar, v); if (csw & 0x10) tar = (tar & ~1023u) | ((tar + 4) & 1023u); }
  }
  void clockOut(bool b) override {
    ones = b ? ones + 1 : 0;
    if (ones >= 50) { after_reset = true; st = IDLE; jbits = 0; jval = 0; outq.clear(); return; }
    if (after_reset && !b) {   // first 0 after a line reset
      after_reset = false;
      if (jtag_ok) { swd = true; jtag_ok = false; } else { jbits = 1; jval = 0; }
      return;
    }
    if (jbits) {
      jval |= (uint16_t)b << jbits; jbits++;
      if (jbits == 16) { jbits = 0; jtag_ok = (jval == 0xE79E); }
      return;
    }
    switch (st) {
      case IDLE: if (b) { st = REQ; req = 1; nreq = 1; } return;
      case REQ: req |= (uint8_t)b << nreq; if (++nreq == 8) request(); return;
      case JUNK: return;
      case WDATA:
        wbits |= (uint64_t)b << nw;
        if (++nw == 33) {
          uint32_t v = (uint32_t)wbits;
          if (((wbits >> 32) & 1) == par32(v)) write_done(v);
          st = IDLE;
        }
        return;
    }
  }
  void delayMs(uint32_t) override { }
};

static std::vector<uint8_t> image(size_t n, unsigned seed) {
  std::vector<uint8_t> v(n);
  srand(seed);
  for (auto& b : v) b = (uint8_t)rand();
  for (size_t i = 0; i + 8 <= n && i < 64; i += 8) memset(&v[i + 16 * 8], 0xFF, 8);   // some blank words
  return v;
}

static void testConnect() {
  SimMspm0 t; Swd swd(t);
  CHECK(swd.connect() == SWD_OK);
  CHECK(swd.idcode == 0x0BC11477);
  uint32_t v = 0;
  CHECK(swd.read32(0x41C40004, &v) == SWD_OK && v == 0x1BB8202F);
}

static void testNoTarget() {
  struct Open : SwdPins {
    void clockOut(bool) override { } bool clockIn() override { return true; }
    void release() override { } void drive() override { }
  } open;
  Swd swd(open);
  CHECK(swd.connect() == SWD_NO_ACK);
}

static void testFlash(int wait_every, size_t len) {
  SimMspm0 t; t.wait_every = wait_every;
  // old firmware and settings in flash
  for (size_t i = 0; i < t.flash.size(); i++) t.flash[i] = (uint8_t)(i * 7);
  std::vector<uint8_t> old = t.flash;
  Swd swd(t); Mspm0Flash fl(swd, t);
  Mspm0Info info;
  CHECK(fl.attach(&info) == SWD_OK);
  CHECK(t.halted);
  CHECK(info.flash_kb == 64);
  std::vector<uint8_t> img = image(len, (unsigned)len);
  int r = fl.write(0, img.data(), (uint32_t)img.size());
  CHECK(r == SWD_OK);
  if (r) printf("  write: %s at 0x%x\n", swdError(r), fl.fail_addr);
  CHECK(memcmp(t.flash.data(), img.data(), img.size()) == 0);
  size_t end = (img.size() + 7) & ~7u;
  for (size_t i = img.size(); i < end; i++) CHECK(t.flash[i] == 0xFF);
  size_t sec_end = (img.size() + 1023) & ~1023u;
  for (size_t i = end; i < sec_end; i++) if (t.flash[i] != 0xFF) { CHECK(false); break; }
  CHECK(memcmp(&t.flash[sec_end], &old[sec_end], t.flash.size() - sec_end) == 0);   // settings kept
  CHECK(t.prot_fails == 0);
  CHECK(t.erases == (int)(sec_end / 1024));
  CHECK(fl.run(true) == SWD_OK);
  CHECK(!t.halted && !t.debugen && t.nrst_pulses == 1);
}

static void testVerifyCatchesBadFlash() {
  SimMspm0 t; Swd swd(t); Mspm0Flash fl(swd, t);
  Mspm0Info info;
  CHECK(fl.attach(&info) == SWD_OK);
  std::vector<uint8_t> img = image(4096, 3);
  CHECK(fl.write(0, img.data(), 4096) == SWD_OK);
  t.flash[2052] ^= 0x10;
  CHECK(fl.verify(0, img.data(), 4096) == SWD_VERIFY);
  CHECK(fl.fail_addr == 2052);
}

static void testAsleepNeedsReset() {
  SimMspm0 t; t.asleep = true;
  Swd swd(t); Mspm0Flash fl(swd, t);
  Mspm0Info info;
  CHECK(fl.attach(&info) == SWD_OK);   // halt fails asleep, NRST pulse, then caught
  CHECK(t.nrst_pulses == 1 && t.halted);
}

static void testUpperSectorsUseWeprotB() {
  SimMspm0 t; Swd swd(t); Mspm0Flash fl(swd, t);
  Mspm0Info info;
  CHECK(fl.attach(&info) == SWD_OK);
  std::vector<uint8_t> img = image(1024, 9);
  CHECK(fl.write(40 * 1024, img.data(), 1024) == SWD_OK);
  CHECK(memcmp(&t.flash[40 * 1024], img.data(), 1024) == 0);
  CHECK(t.prot_fails == 0);
}

static void testRealImage(const char* path) {
  FILE* f = fopen(path, "rb");
  if (!f) { printf("skip real image (%s)\n", path); return; }
  std::vector<uint8_t> img(70000);
  size_t n = fread(img.data(), 1, img.size(), f); fclose(f);
  img.resize(n);
  SimMspm0 t; t.wait_every = 37; Swd swd(t); Mspm0Flash fl(swd, t);
  Mspm0Info info;
  CHECK(fl.attach(&info) == SWD_OK);
  CHECK(fl.write(0, img.data(), (uint32_t)n) == SWD_OK);
  CHECK(memcmp(t.flash.data(), img.data(), n) == 0);
  printf("real image %zu bytes: %d SWD transfers, %u waits\n", n, t.xfers, swd.waits);
}

static void testHex(const char* hexpath, const char* binpath) {
  FILE* f = fopen(hexpath, "rb"); FILE* g = fopen(binpath, "rb");
  if (!f || !g) { printf("skip hex (%s)\n", hexpath); if (f) fclose(f); if (g) fclose(g); return; }
  std::vector<char> txt(200000); size_t tn = fread(txt.data(), 1, txt.size(), f); fclose(f);
  std::vector<uint8_t> bin(70000); size_t bn = fread(bin.data(), 1, bin.size(), g); fclose(g);
  std::vector<uint8_t> out(IMAGE_MAX);
  int n = hexToBin(txt.data(), tn, out.data(), IMAGE_MAX);
  CHECK(n == (int)bn);
  CHECK(n > 0 && memcmp(out.data(), bin.data(), bn) == 0);
  CHECK(imageLooksValid(out.data(), n));
  txt[20] = txt[20] == '0' ? '1' : '0';                 // one digit changed: checksum
  CHECK(hexToBin(txt.data(), tn, out.data(), IMAGE_MAX) == -2);
  const char* far = ":020000040001F9\n:0400000001020304F2\n:00000001FF\n";   // 0x10000: outside
  CHECK(hexToBin(far, strlen(far), out.data(), IMAGE_MAX) == -3);
  uint8_t junk[16]; memset(junk, 0x11, 16);
  CHECK(!imageLooksValid(junk, 16));
}

static void testHexEdgeCases() {
  std::vector<uint8_t> out(IMAGE_MAX);
  // base = 0xFFFF0000, off = 0xFFFF, n = 16 -> a = 0xFFFFFFFF, a + n would wrap 32-bit uint
  // :02000004FFFFFC  (02+00+00+04+FF+FF = 0x204 -> two's comp = 0xFC)
  // :10FFFF00 00..00 -> 10+FF+FF+00 = 0x20E -> two's comp = 0xF2
  const char* wrap = ":02000004FFFFFC\n:10FFFF0000000000000000000000000000000000F2\n:00000001FF\n";
  CHECK(hexToBin(wrap, strlen(wrap), out.data(), IMAGE_MAX) == -3);
  // type 0x04 with n != 2 (n = 1: :0100000400FB -> 01+00+00+04+00=05 -> FB)
  const char* bad_ela = ":0100000400FB\n:00000001FF\n";
  CHECK(hexToBin(bad_ela, strlen(bad_ela), out.data(), IMAGE_MAX) == -1);
  // type 0x02 with n != 2 (n = 1: :0100000200FD -> 01+00+00+02+00=03 -> FD)
  const char* bad_esa = ":0100000200FD\n:00000001FF\n";
  CHECK(hexToBin(bad_esa, strlen(bad_esa), out.data(), IMAGE_MAX) == -1);
}

int main(int argc, char** argv) {
  testConnect();
  testNoTarget();
  testFlash(0, 13800);
  testFlash(5, 3001);     // WAIT on every 5th transfer, odd length
  testFlash(0, 61440);    // the whole application area (0x0000-0xEFFF)
  testVerifyCatchesBadFlash();
  testAsleepNeedsReset();
  testUpperSectorsUseWeprotB();
  testHexEdgeCases();
  if (argc > 1) testRealImage(argv[1]);
  if (argc > 2) testHex(argv[2], argv[1]);
  printf("%d checks, %d failed\n", checks, fails);
  return fails ? 1 : 0;
}
