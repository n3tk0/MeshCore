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
  uint32_t _buf[64];   // word aligned bounce buffer: the DMA wants RAM, words and 4 byte addresses

  bool waitIdle(uint32_t timeout_ms) {
    unsigned long start = millis();
    while (nrfx_qspi_mem_busy_check() == NRFX_ERROR_BUSY) {
      if (millis() - start > timeout_ms) return false;
      yield();
    }
    return true;
  }

public:
  QspiStorage() : _on(false), _ok(false), _hold(false), _used(0) { }

  // true when a flash chip of at least 2 MB answers
  bool begin() {
    if (!on()) return false;
    uint8_t id[3] = { 0, 0, 0 };
    nrf_qspi_cinstr_conf_t c = NRFX_QSPI_DEFAULT_CINSTR(0x9F, NRF_QSPI_CINSTR_LEN_4B);   // JEDEC ID
    _ok = nrfx_qspi_cinstr_xfer(&c, NULL, id) == NRFX_SUCCESS && id[0] != 0 && id[0] != 0xFF && id[2] >= 0x15;
    off();
    return _ok;
  }
  bool ok() const { return _ok; }

  bool on() {
    _used = millis();
    if (_on) return true;
    nrfx_qspi_config_t cfg = NRFX_QSPI_DEFAULT_CONFIG(
        g_ADigitalPinMap[PIN_QSPI_SCK], g_ADigitalPinMap[PIN_QSPI_CS], g_ADigitalPinMap[PIN_QSPI_IO0],
        g_ADigitalPinMap[PIN_QSPI_IO1], g_ADigitalPinMap[PIN_QSPI_IO2], g_ADigitalPinMap[PIN_QSPI_IO3]);
    cfg.phy_if.sck_freq = NRF_QSPI_FREQ_32MDIV4;
    if (nrfx_qspi_init(&cfg, NULL, NULL) != NRFX_SUCCESS) return false;   // NULL handler: blocking calls
    nrfx_qspi_cinstr_quick_send(0xAB, NRF_QSPI_CINSTR_LEN_1B, NULL);       // out of deep power down
    delayMicroseconds(50);
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
