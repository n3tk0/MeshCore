#pragma once

// The external QSPI flash of boards like the Xiao nRF52840 (2 MB): raw sectors for the repeater.
//   0x000000 - 0x0CFFFF  LoRa OTA: the new image (the internal flash then needs no room for it)
//   0x0D0000 - 0x12FFFF  mailbox: a ring of 16 KB slots, each save goes to the next (wear spread, and
//                        a power cut mid-save keeps the slot before)
//   0x130000 - 0x1FFFFF  LoRa OTA: the image that ran before (rollback by our bootloader, see OtaStage.h)
// Both images get 832 KB, more than the whole app area (792 KB), so rollback never has to be skipped.
// Before 2026-10-22: 1 MB / 512 KB / 512 KB. A record written then keeps its own backup address, and
// old mailbox slots that now fall in the ring still load (each has its magic and CRC).
// The peripheral is turned on for each use and off again after a short idle time: it draws current.

#include <Arduino.h>
#ifndef NRFX_QSPI_DEFAULT_CONFIG_IRQ_PRIORITY
  #define NRFX_QSPI_DEFAULT_CONFIG_IRQ_PRIORITY 6   // unused: no handler, the calls block
#endif
#include <nrfx_qspi.h>

#define QSPI_OTA_ADDR        0x000000
#define QSPI_OTA_SIZE        0x0D0000
#define QSPI_MAILBOX_ADDR    0x0D0000
#define QSPI_MAILBOX_SLOT    0x4000
#define QSPI_MAILBOX_SLOTS   24
#define QSPI_BACKUP_ADDR     0x130000
#define QSPI_BACKUP_SIZE     0x0D0000
#define QSPI_SECTOR          4096
#define QSPI_IDLE_OFF_MS     2000

class QspiStorage {
  bool _on, _ok, _hold;
  unsigned long _used;
  uint8_t _stage;      // where the last probe failed: 0 none, 1 init, 2 ID read, 3 bad ID
  int _err;            // its nrfx error, counted from NRFX_ERROR_BASE_NUM (5 = timeout)
  uint8_t _id[3];      // the JEDEC ID read by the last begin() (85 60 15 = P25Q16H)
  uint32_t _buf[64];   // word aligned bounce buffer: the DMA wants RAM, words and 4 byte addresses

  // off() leaves the flash in deep power down, where it ignores everything but 0xAB: also the status
  // reads the QSPI peripheral makes on ACTIVATE, so nrfx_qspi_init() would time out (after a restart
  // too: "qspi none"). 0xAB goes out by hand on the pins first (SPI mode 0, WP and HOLD high).
  static void wake() {
    uint32_t sck = g_ADigitalPinMap[PIN_QSPI_SCK], cs = g_ADigitalPinMap[PIN_QSPI_CS];
    uint32_t io0 = g_ADigitalPinMap[PIN_QSPI_IO0];
    nrf_gpio_pin_set(cs);
    nrf_gpio_cfg_output(cs);
    nrf_gpio_pin_clear(sck);
    nrf_gpio_cfg_output(sck);
    nrf_gpio_cfg_output(io0);
    nrf_gpio_pin_set(g_ADigitalPinMap[PIN_QSPI_IO2]);
    nrf_gpio_cfg_output(g_ADigitalPinMap[PIN_QSPI_IO2]);
    nrf_gpio_pin_set(g_ADigitalPinMap[PIN_QSPI_IO3]);
    nrf_gpio_cfg_output(g_ADigitalPinMap[PIN_QSPI_IO3]);
    nrf_gpio_pin_clear(cs);
    for (int b = 7; b >= 0; b--) {
      nrf_gpio_pin_write(io0, (0xAB >> b) & 1);
      nrf_gpio_pin_set(sck);
      nrf_gpio_pin_clear(sck);
    }
    nrf_gpio_pin_set(cs);
    delayMicroseconds(50);   // tRES1: well under 50 us
  }

  // ---- by hand on the pins, without the QSPI peripheral (diagnostics, `qspi` command) ----
  static void bbByte(uint32_t sck, uint32_t io0, uint8_t v) {
    for (int b = 7; b >= 0; b--) {
      nrf_gpio_pin_write(io0, (v >> b) & 1);
      delayMicroseconds(1);
      nrf_gpio_pin_set(sck);
      delayMicroseconds(1);
      nrf_gpio_pin_clear(sck);
    }
  }
  static uint8_t bbRead(uint32_t sck, uint32_t io1) {
    uint8_t v = 0;
    for (int b = 7; b >= 0; b--) {
      nrf_gpio_pin_set(sck);
      delayMicroseconds(1);
      v = (v << 1) | (nrf_gpio_pin_read(io1) & 1);
      nrf_gpio_pin_clear(sck);
      delayMicroseconds(1);
    }
    return v;
  }

public:
  // Asks the chip by hand: IO1 with pull up / pull down (0 with pull up = something holds the line low),
  // then 0xFF x2 (ends a continuous read mode), reset 0x66 0x99, release 0xAB (answers the device ID),
  // JEDEC 0x9F and 0x90 (maker + device). Out: "pu=1 pd=0 ab=14 9f=856015 90=8514". The pins are left as
  // inputs; the QSPI peripheral takes them again on its next init. Not while the flash is in use.
  void bitbangProbe(char* out, size_t len) {
    if (_on) off();
    uint32_t sck = g_ADigitalPinMap[PIN_QSPI_SCK], cs = g_ADigitalPinMap[PIN_QSPI_CS];
    uint32_t io0 = g_ADigitalPinMap[PIN_QSPI_IO0], io1 = g_ADigitalPinMap[PIN_QSPI_IO1];
    uint32_t io2 = g_ADigitalPinMap[PIN_QSPI_IO2], io3 = g_ADigitalPinMap[PIN_QSPI_IO3];
    nrf_gpio_pin_set(cs);   nrf_gpio_cfg_output(cs);
    nrf_gpio_pin_clear(sck); nrf_gpio_cfg_output(sck);
    nrf_gpio_pin_set(io0);  nrf_gpio_cfg_output(io0);
    nrf_gpio_pin_set(io2);  nrf_gpio_cfg_output(io2);   // WP high
    nrf_gpio_pin_set(io3);  nrf_gpio_cfg_output(io3);   // HOLD high
    nrf_gpio_cfg_input(io1, NRF_GPIO_PIN_PULLUP);   delayMicroseconds(100);
    int pu = nrf_gpio_pin_read(io1);
    nrf_gpio_cfg_input(io1, NRF_GPIO_PIN_PULLDOWN); delayMicroseconds(100);
    int pd = nrf_gpio_pin_read(io1);
    auto cmd = [&](const uint8_t* c, int n) {
      nrf_gpio_pin_clear(cs); delayMicroseconds(1);
      for (int i = 0; i < n; i++) bbByte(sck, io0, c[i]);
    };
    auto end = [&]() { nrf_gpio_pin_set(io0); nrf_gpio_pin_set(cs); delayMicroseconds(5); };
    const uint8_t ff[2] = { 0xFF, 0xFF }; cmd(ff, 2); end();
    const uint8_t re[1] = { 0x66 };       cmd(re, 1); end();
    const uint8_t rs[1] = { 0x99 };       cmd(rs, 1); end();
    delayMicroseconds(100);                 // tRST
    const uint8_t ab[4] = { 0xAB, 0, 0, 0 }; cmd(ab, 4);
    uint8_t dev = bbRead(sck, io1); end();
    delayMicroseconds(50);
    const uint8_t j[1] = { 0x9F };        cmd(j, 1);
    uint8_t a = bbRead(sck, io1), b = bbRead(sck, io1), c = bbRead(sck, io1); end();
    const uint8_t r[4] = { 0x90, 0, 0, 0 }; cmd(r, 4);
    uint8_t m = bbRead(sck, io1), d = bbRead(sck, io1); end();
    nrf_gpio_cfg_default(sck); nrf_gpio_cfg_default(io0); nrf_gpio_cfg_default(io1);
    nrf_gpio_cfg_default(io2); nrf_gpio_cfg_default(io3);
    snprintf(out, len, "pu=%d pd=%d ab=%02X 9f=%02X%02X%02X 90=%02X%02X", pu, pd, dev, a, b, c, m, d);
  }

private:
  bool waitIdle(uint32_t timeout_ms) {
    unsigned long start = millis();
    while (nrfx_qspi_mem_busy_check() == NRFX_ERROR_BUSY) {
      if (millis() - start > timeout_ms) return false;
      yield();
    }
    return true;
  }

public:
  QspiStorage() : _on(false), _ok(false), _hold(false), _used(0), _stage(0), _err(0) { _id[0] = _id[1] = _id[2] = 0; }

  // true when a flash chip of at least 2 MB answers. Tried 3 times: a chip still starting after power up
  // (or after the bootloader) may miss the first try.
  bool begin() {
    for (int i = 0; i < 3; i++) {
      if (i) delay(20);
      if (probe()) return true;
    }
    return false;
  }

  bool probe() {
    _ok = false;
    _id[0] = _id[1] = _id[2] = 0;
    _err = 0;
    _stage = 1;
    if (!on()) return false;   // _err set by on()
    _stage = 2;
    nrf_qspi_cinstr_conf_t c = NRFX_QSPI_DEFAULT_CINSTR(0x9F, NRF_QSPI_CINSTR_LEN_4B);   // JEDEC ID
    nrfx_err_t e = nrfx_qspi_cinstr_xfer(&c, NULL, _id);
    if (e != NRFX_SUCCESS) _err = (int)e - (int)NRFX_ERROR_BASE_NUM;
    else _stage = 3;
    _ok = e == NRFX_SUCCESS && _id[0] != 0 && _id[0] != 0xFF && _id[2] >= 0x15;
    if (_ok) _stage = 0;
    off();
    return _ok;
  }
  bool ok() const { return _ok; }
  // no chip answers at all (the data line stays 0 or 1): a board without the flash fitted, e.g. some
  // SenseCAP Solar P1. Not a fault: the repeater keeps the mailbox and LoRa OTA in the internal flash.
  bool absent() const {
    return !_ok && _stage == 3 && ((_id[0] == 0 && _id[1] == 0 && _id[2] == 0) ||
                                   (_id[0] == 0xFF && _id[1] == 0xFF && _id[2] == 0xFF));
  }

  // what the last probe saw, for `selftest` and `qspi`: "ok:856015" (JEDEC ID; P25Q16H), "init:5" (nrfx error
  // of the init, 5 = timeout: the chip did not answer the status read), "xfer:N" (the ID read failed),
  // "id:FFFFFF" / "id:000000" (no chip answers, or a chip under 2 MB)
  void describe(char* out, size_t len) const {
    if (_stage == 1) snprintf(out, len, "init:%d", _err);
    else if (_stage == 2) snprintf(out, len, "xfer:%d", _err);
    else snprintf(out, len, "%s:%02X%02X%02X", _ok ? "ok" : "id", _id[0], _id[1], _id[2]);
  }

  bool on() {
    _used = millis();
    if (_on) return true;
    nrfx_qspi_config_t cfg = NRFX_QSPI_DEFAULT_CONFIG(
        g_ADigitalPinMap[PIN_QSPI_SCK], g_ADigitalPinMap[PIN_QSPI_CS], g_ADigitalPinMap[PIN_QSPI_IO0],
        g_ADigitalPinMap[PIN_QSPI_IO1], g_ADigitalPinMap[PIN_QSPI_IO2], g_ADigitalPinMap[PIN_QSPI_IO3]);
    cfg.phy_if.sck_freq = NRF_QSPI_FREQ_32MDIV4;
    wake();
    nrfx_err_t e = nrfx_qspi_init(&cfg, NULL, NULL);   // NULL handler: blocking calls
    if (e != NRFX_SUCCESS) {
      _err = (int)e - (int)NRFX_ERROR_BASE_NUM;
      if (e == NRFX_ERROR_TIMEOUT) nrfx_qspi_uninit();   // it stays "initialized": the next try would be refused
      return false;
    }
    _on = true;
    return true;
  }

  void off() {
    if (!_on || _hold) return;
    waitIdle(1000);
    nrfx_qspi_cinstr_quick_send(0xB9, NRF_QSPI_CINSTR_LEN_1B, NULL);   // deep power down: under 1 uA instead of ~15 uA standby
    nrfx_qspi_uninit();
    _on = false;
  }

  void hold(bool h) { _hold = h; if (h) on(); }   // kept on (e.g. right before the OTA copy)

  void loop() {
    if (_on && !_hold && millis() - _used > QSPI_IDLE_OFF_MS) off();
  }

  bool erase(uint32_t addr) {   // one 4 KB sector
    if (!on() || !waitIdle(500)) return false;
    bool r = nrfx_qspi_erase(NRF_QSPI_ERASE_LEN_4KB, addr) == NRFX_SUCCESS && waitIdle(500);
    _used = millis();
    return r;
  }

  bool write(uint32_t addr, const uint8_t* src, size_t len) {
    if (!on()) return false;
    while (len > 0) {
      // within one 256 byte page, in whole words (the tail padded with 0xFF: unprogrammed)
      uint32_t lead = addr & 3;
      uint32_t base = addr - lead;
      size_t room = 256 - (base & 255);
      size_t n = len < room - lead ? len : room - lead;
      size_t words = (lead + n + 3) / 4;
      memset(_buf, 0xFF, words * 4);
      memcpy((uint8_t*)_buf + lead, src, n);
      if (!waitIdle(100) || nrfx_qspi_write(_buf, words * 4, base) != NRFX_SUCCESS) return false;
      addr += n; src += n; len -= n;
    }
    _used = millis();
    return waitIdle(100);
  }

  bool read(uint32_t addr, uint8_t* dest, size_t len) {
    if (!on() || !waitIdle(500)) return false;
    while (len > 0) {
      uint32_t lead = addr & 3;
      uint32_t base = addr - lead;
      size_t n = len < sizeof(_buf) - lead ? len : sizeof(_buf) - lead;
      size_t words = (lead + n + 3) / 4;
      if (nrfx_qspi_read(_buf, words * 4, base) != NRFX_SUCCESS) return false;
      memcpy(dest, (uint8_t*)_buf + lead, n);
      addr += n; dest += n; len -= n;
    }
    _used = millis();
    return true;
  }
};

static_assert(QSPI_OTA_ADDR + QSPI_OTA_SIZE <= QSPI_MAILBOX_ADDR, "QSPI layout");
static_assert(QSPI_MAILBOX_ADDR + QSPI_MAILBOX_SLOTS * QSPI_MAILBOX_SLOT <= QSPI_BACKUP_ADDR, "QSPI layout");
static_assert(QSPI_BACKUP_ADDR + QSPI_BACKUP_SIZE <= 0x200000, "QSPI layout: 2 MB flash");
