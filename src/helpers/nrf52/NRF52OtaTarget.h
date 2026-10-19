#pragma once

// LoRa OTA on nRF52: the new image is written to the free flash after the running one, then a routine
// in RAM copies it over the running image and resets. If power is lost during that copy the
// application is gone but the bootloader stays, so the node can be flashed again over USB or BLE.

#include <Arduino.h>
#include <helpers/LoraOta.h>
#include <flash/flash_nrf5x.h>
#include <nrf_sdm.h>

extern "C" {
  extern uint32_t __flash_arduino_start[];
  extern uint32_t __flash_arduino_end[];
  extern uint32_t __etext[];
  extern uint32_t __data_start__[];
  extern uint32_t __data_end__[];
}

#define NRF52_OTA_PAGE        4096
#define NRF52_BL_SETTINGS     0xFF000   // Adafruit bootloader settings: u16 bank_0, u16 bank_0_crc, ...

// Runs from RAM with interrupts off: flash below it is being rewritten, so it calls nothing.
__attribute__((section(".data"), noinline, long_call))
static void nrf52OtaCopyAndReset(uint32_t dst, uint32_t src, uint32_t pages) {
  __disable_irq();
  for (uint32_t p = 0; p < pages; p++) {
    uint32_t d = dst + p * NRF52_OTA_PAGE;
    volatile uint32_t* to = (volatile uint32_t*) d;
    volatile uint32_t* from = (volatile uint32_t*) (src + p * NRF52_OTA_PAGE);
    if (NRF_WDT->RUNSTATUS) {   // a watchdog started by the board must not reset us half way
      for (uint32_t r = 0; r < 8; r++) {
        if (NRF_WDT->RREN & (1UL << r)) NRF_WDT->RR[r] = WDT_RR_RR_Reload;
      }
    }

    NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Een;
    while (NRF_NVMC->READY == 0) { }
    NRF_NVMC->ERASEPAGE = d;
    while (NRF_NVMC->READY == 0) { }

    NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Wen;
    while (NRF_NVMC->READY == 0) { }
    for (uint32_t w = 0; w < NRF52_OTA_PAGE / 4; w++) {
      to[w] = from[w];
      while (NRF_NVMC->READY == 0) { }
    }
  }
  // a CRC stored by an earlier DFU would no longer match: 0 tells the bootloader not to check it
  volatile uint32_t* settings = (volatile uint32_t*) NRF52_BL_SETTINGS;
  uint32_t word = settings[0];
  if ((word & 0xFFFF0000) != 0 && (word & 0xFFFF) == 0x0001) {   // bank_0 == BANK_VALID_APP, crc set
    NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Wen;
    while (NRF_NVMC->READY == 0) { }
    settings[0] = word & 0x0000FFFF;
    while (NRF_NVMC->READY == 0) { }
  }
  NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Ren;
  while (NRF_NVMC->READY == 0) { }

  __DSB();
  SCB->AIRCR = (0x5FAUL << SCB_AIRCR_VECTKEY_Pos) | SCB_AIRCR_SYSRESETREQ_Msk;
  __DSB();
  while (1) { }
}

class NRF52OtaTarget : public OtaTarget {
  uint32_t _new_size, _pos;

  static uint32_t appStart() { return (uint32_t) __flash_arduino_start; }
  static uint32_t flashEnd() { return (uint32_t) __flash_arduino_end; }   // the file system starts here
  uint32_t staging() {
    uint32_t end = appStart() + imageSize();
    return (end + NRF52_OTA_PAGE - 1) & ~(uint32_t)(NRF52_OTA_PAGE - 1);
  }

public:
  NRF52OtaTarget() : _new_size(0), _pos(0) { }

  uint8_t platform() const override { return OTA_PLATFORM_NRF52; }

  uint32_t imageSize() override {
    // the .bin is code + read-only data up to __etext, then the initial values of .data
    return ((uint32_t) __etext - appStart()) + ((uint32_t) __data_end__ - (uint32_t) __data_start__);
  }

  bool readImage(uint32_t offset, uint8_t* dest, size_t len) override {
    memcpy(dest, (const void*) (appStart() + offset), len);
    return true;
  }

  uint32_t maxNewSize() override {
    return flashEnd() > staging() ? flashEnd() - staging() : 0;
  }

  bool beginNew(uint32_t size) override {
    if (size > maxNewSize()) return false;
    _new_size = size;
    _pos = 0;
    return true;
  }

  bool writeNew(const uint8_t* src, size_t len) override {
    if (_pos + len > _new_size) return false;
    if (flash_nrf5x_write(staging() + _pos, src, len) < 0) return false;
    _pos += len;
    return true;
  }

  bool finishNew() override {
    flash_nrf5x_flush();
    return _pos == _new_size;
  }

  bool readNew(uint32_t offset, uint8_t* dest, size_t len) override {
    memcpy(dest, (const void*) (staging() + offset), len);
    return true;
  }

  void abortNew() override {
    flash_nrf5x_flush();
    _pos = 0;
  }

  void commit() override {
    uint32_t src = staging();
    uint32_t pages = (_new_size + NRF52_OTA_PAGE - 1) / NRF52_OTA_PAGE;
    uint8_t sd_en = 0;
    sd_softdevice_is_enabled(&sd_en);
    if (sd_en) sd_softdevice_disable();
    nrf52OtaCopyAndReset(appStart(), src, pages);
  }
};
