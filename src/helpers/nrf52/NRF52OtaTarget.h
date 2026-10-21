#pragma once

// LoRa OTA on nRF52: the new image is written to the free flash after the running one (or to the QSPI
// flash), then copied over the running image.
//
// With our bootloader (bootloader/ in this repo) the bootloader does the copy: the app leaves a record
// (OtaStage.h) and resets. The source is not touched, so a power cut only makes the copy start again.
// From the QSPI flash the running image is also kept there, and the new one runs on trial until
// confirm(): if it never gets that far, the bootloader puts the old one back.
//
// With any other bootloader a routine in RAM copies it and resets. While it copies, the bootloader
// settings say "no valid app", so a power cut leaves the node in the bootloader (USB / BLE DFU), not in
// half an image. That needs the settings page to hold only the settings (as the Adafruit bootloader
// writes it); otherwise the page is left as it is and a power cut during the copy can leave a broken
// app. At the end the settings get the image's CRC16, which the bootloader then checks at every start.

#include <Arduino.h>
#include <helpers/LoraOta.h>
#include <flash/flash_nrf5x.h>
#include <nrf_sdm.h>
#include <nrf_soc.h>
#include <helpers/nrf52/OtaStage.h>
#ifdef QSPI_STORAGE
  #include <helpers/nrf52/QspiStorage.h>
#endif

extern "C" {
  extern uint32_t __flash_arduino_start[];
  extern uint32_t __flash_arduino_end[];
  extern uint32_t __etext[];
  extern uint32_t __data_start__[];
  extern uint32_t __data_end__[];
}

#define NRF52_OTA_PAGE        4096
#define NRF52_BL_SETTINGS     0xFF000   // Adafruit bootloader settings: u16 bank_0, u16 bank_0_crc, ...
#define NRF52_BL_SETTINGS_WORDS 8       // sizeof(bootloader_settings_t) = 28 bytes, rounded up

// One piece of the copy: pages from RAM, internal flash, or the QSPI flash (read into RAM page by page)
struct Nrf52OtaPiece {
  uint32_t src;       // address (RAM or internal flash), or the QSPI flash address
  uint32_t pages;
  uint32_t qspi;      // 1: src is in the QSPI flash
};

// Runs from RAM with interrupts off: flash below it is being rewritten, so it calls nothing.
// Writes the pieces one after the other from dst on. buf: a 4 KB RAM page for the QSPI reads.
// crc, size: the new image's CRC16 (as the bootloader computes it) and size, for the settings.
__attribute__((section(".data"), noinline, long_call))
static void nrf52OtaCopyAndReset(uint32_t dst, const Nrf52OtaPiece* pieces, uint32_t num, uint32_t* buf,
                                 uint16_t crc, uint32_t size) {
  __disable_irq();
  // Bootloader settings: u16 bank_0 (1 = valid app), u16 bank_0_crc, then sizes. While the copy runs
  // bank_0 is 0, so a power cut leaves the node in the bootloader (USB / BLE DFU), not in half an image.
  volatile uint32_t* settings = (volatile uint32_t*) NRF52_BL_SETTINGS;
  uint32_t saved[NRF52_BL_SETTINGS_WORDS];
  for (uint32_t w = 0; w < NRF52_BL_SETTINGS_WORDS; w++) saved[w] = settings[w];
  // only when the rest of the page is empty: it is erased and rewritten at the end
  bool marked = (saved[0] & 0xFFFF) == 0x0001;
  for (uint32_t w = NRF52_BL_SETTINGS_WORDS; marked && w < NRF52_OTA_PAGE / 4; w++) {
    if (settings[w] != 0xFFFFFFFF) marked = false;
  }
  if (marked) {
    NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Wen;
    while (NRF_NVMC->READY == 0) { }
    settings[0] = saved[0] & 0xFFFF0000;   // bank_0 = 0: no valid app
    while (NRF_NVMC->READY == 0) { }
  }

  uint32_t d = dst;
  for (uint32_t k = 0; k < num; k++) {
    for (uint32_t p = 0; p < pieces[k].pages; p++, d += NRF52_OTA_PAGE) {
      uint32_t s = pieces[k].src + p * NRF52_OTA_PAGE;
      if (NRF_WDT->RUNSTATUS) {   // a watchdog started by the board must not reset us half way
        for (uint32_t r = 0; r < 8; r++) {
          if (NRF_WDT->RREN & (1UL << r)) NRF_WDT->RR[r] = WDT_RR_RR_Reload;
        }
      }
#if defined(NRF_QSPI)
      if (pieces[k].qspi) {
        NRF_QSPI->READ.SRC = s;
        NRF_QSPI->READ.DST = (uint32_t) buf;
        NRF_QSPI->READ.CNT = NRF52_OTA_PAGE;
        NRF_QSPI->EVENTS_READY = 0;
        NRF_QSPI->TASKS_READSTART = 1;
        while (NRF_QSPI->EVENTS_READY == 0) { }
        s = (uint32_t) buf;
      }
#endif
      volatile uint32_t* to = (volatile uint32_t*) d;
      volatile uint32_t* from = (volatile uint32_t*) s;

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
  }
  if (marked) {
    // valid again, with the new image's CRC: the bootloader checks it at every start
    NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Een;
    while (NRF_NVMC->READY == 0) { }
    NRF_NVMC->ERASEPAGE = NRF52_BL_SETTINGS;
    while (NRF_NVMC->READY == 0) { }
    NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Wen;
    while (NRF_NVMC->READY == 0) { }
    saved[0] = 0x00000001 | ((uint32_t) crc << 16);
    saved[2] = size;   // bank_0_size: what the CRC covers
    for (uint32_t w = 0; w < NRF52_BL_SETTINGS_WORDS; w++) {
      settings[w] = saved[w];
      while (NRF_NVMC->READY == 0) { }
    }
  } else if ((saved[0] & 0xFFFF) == 0x0001 && (saved[0] & 0xFFFF0000) != 0) {
    NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Wen;   // at least turn off the CRC check
    while (NRF_NVMC->READY == 0) { }
    settings[0] = saved[0] & 0x0000FFFF;
    while (NRF_NVMC->READY == 0) { }
  }
  NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Ren;
  while (NRF_NVMC->READY == 0) { }

  __DSB();
  SCB->AIRCR = (0x5FAUL << SCB_AIRCR_VECTKEY_Pos) | SCB_AIRCR_SYSRESETREQ_Msk;
  __DSB();
  while (1) { }
}

// CRC16 as the Adafruit bootloader checks the app (crc16_compute of the nRF5 SDK); start with 0xFFFF
static uint16_t nrf52Crc16(uint16_t crc, const uint8_t* p, uint32_t len) {
  while (len--) {
    crc = (uint8_t)(crc >> 8) | (crc << 8);
    crc ^= *p++;
    crc ^= (uint8_t)(crc & 0xFF) >> 4;
    crc ^= (crc << 8) << 4;
    crc ^= ((crc & 0xFF) << 4) << 1;
  }
  return crc;
}

static void nrf52FeedWatchdog() {
  if (NRF_WDT->RUNSTATUS) {
    for (uint32_t r = 0; r < 8; r++) {
      if (NRF_WDT->RREN & (1UL << r)) NRF_WDT->RR[r] = WDT_RR_RR_Reload;
    }
  }
}

#ifndef NRF52_OTA_MAX_HEAD
  #define NRF52_OTA_MAX_HEAD  (192 * 1024)   // in place: the start of the new image, made again in RAM (if malloc gives it)
#endif

class NRF52OtaTarget : public OtaTarget {
  uint32_t _new_size, _pos;
  uint32_t _stage;        // the new image in the internal flash (not in place)
  // in place: [staging] the delta, then [_tail] the new image from _head on
  uint32_t _head, _tail, _patch_len;
  uint8_t* _patch_page;   // the delta's last page, until full (two pages written in turns would wear)
  uint32_t _vec[2];       // in place: initial SP and reset handler of the new image (not kept in flash)
#ifdef QSPI_STORAGE
  QspiStorage* _qspi;     // set: the new image goes to the external flash
  uint32_t _qbuf_len;
  uint8_t _qbuf[256];
#endif

  static uint32_t appStart() { return (uint32_t) __flash_arduino_start; }
  static uint32_t flashEnd() { return (uint32_t) __flash_arduino_end; }   // the file system starts here
  static uint32_t pageUp(uint32_t v) { return (v + NRF52_OTA_PAGE - 1) & ~(uint32_t)(NRF52_OTA_PAGE - 1); }
  uint32_t staging() { return pageUp(appStart() + imageSize()); }
  // Where a new image of this size goes. The copy in RAM goes forward, so it may run over the start of
  // the source it already read. The bootloader's copy must not: after a power cut it reads it all again.
  uint32_t stagingFor(uint32_t size) {
    if (!stagedBootloader(1) || size <= imageSize()) return staging();
    return pageUp(appStart() + size);
  }
  bool onQspi() const {
#ifdef QSPI_STORAGE
    return _qspi != NULL && _qspi->ok();
#else
    return false;
#endif
  }

  bool flushPatchPage() {
    uint32_t used = _patch_len % NRF52_OTA_PAGE;
    if (_patch_page == NULL || used == 0) return true;
    return flash_nrf5x_write(staging() + _patch_len - used, _patch_page, used) >= 0;
  }

  void stopInPlace() {
    free(_patch_page);
    _patch_page = NULL;
    _head = 0;
  }

#ifdef QSPI_STORAGE
  bool qspiFlush() {
    if (_qbuf_len == 0) return true;
    uint32_t at = QSPI_OTA_ADDR + _pos - _qbuf_len;
    bool ok = _qspi->write(at, _qbuf, _qbuf_len);
    _qbuf_len = 0;
    return ok;
  }
#endif

  // our bootloader, with these OTA_STAGE features (1: staged install, 2: also from QSPI with rollback)
  static bool stagedBootloader(uint32_t features) {
    const volatile uint32_t* info = (const volatile uint32_t*) OTA_STAGE_BL_INFO_ADDR;
    return info[0] == OTA_STAGE_BL_INFO_MAGIC && (info[1] & features) == features;
  }

  // one state word of a record, 0xFFFFFFFF -> 0 (no erase)
  static bool markWord(const uint32_t* word) {
    uint32_t zero = 0;
    uint8_t sd_en = 0;
    sd_softdevice_is_enabled(&sd_en);
    if (sd_en) {   // the SoftDevice owns the flash: ask it, and wait until the word reads 0
      if (sd_flash_write((uint32_t*) word, &zero, 1) != NRF_SUCCESS) return false;
      unsigned long start = millis();
      while (*(volatile const uint32_t*) word != 0 && millis() - start < 200) delay(1);
    } else {
      NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Wen;
      while (NRF_NVMC->READY == 0) { }
      *(volatile uint32_t*) word = zero;
      while (NRF_NVMC->READY == 0) { }
      NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Ren;
      while (NRF_NVMC->READY == 0) { }
    }
    return *(volatile const uint32_t*) word == 0;
  }

  // a new record for the bootloader; the SoftDevice must be off (see run()). Words in order, magic first:
  // a slot a power cut left half written is then not valid, and not free either.
  static bool writeRecord(const ota_stage_t& t) {
    ota_stage_t* slot = ota_stage_free_slot();
    volatile uint32_t* page = (volatile uint32_t*) OTA_STAGE_PAGE;
    if (slot == NULL) {   // every 24 updates: erase the page, keep the settings
      uint32_t saved[NRF52_BL_SETTINGS_WORDS];
      for (uint32_t w = 0; w < NRF52_BL_SETTINGS_WORDS; w++) saved[w] = page[w];
      NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Een;
      while (NRF_NVMC->READY == 0) { }
      NRF_NVMC->ERASEPAGE = OTA_STAGE_PAGE;
      while (NRF_NVMC->READY == 0) { }
      NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Wen;
      while (NRF_NVMC->READY == 0) { }
      for (uint32_t w = 0; w < NRF52_BL_SETTINGS_WORDS; w++) {
        page[w] = saved[w];
        while (NRF_NVMC->READY == 0) { }
      }
      slot = ota_stage_free_slot();
      if (slot == NULL) return false;
    }
    NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Wen;
    while (NRF_NVMC->READY == 0) { }
    const uint32_t* src = (const uint32_t*) &t;
    volatile uint32_t* to = (volatile uint32_t*) slot;
    uint32_t n = (uint32_t)((const uint8_t*) &t.started - (const uint8_t*) &t) / 4;
    for (uint32_t w = 0; w < n; w++) {
      to[w] = src[w];
      while (NRF_NVMC->READY == 0) { }
    }
    NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Ren;
    while (NRF_NVMC->READY == 0) { }
    return ota_stage_find() == slot;
  }

  static void resetNow() {
    __DSB();
    NVIC_SystemReset();
  }

  // the record for the image at src (size _new_size, CRC-32 crc)
  void fillRecord(ota_stage_t& t, uint32_t flags, uint32_t src, uint32_t crc) {
    memset(&t, 0xFF, sizeof(t));
    t.magic = OTA_STAGE_MAGIC;
    t.version = OTA_STAGE_VERSION;
    t.flags = flags;
    t.dst = appStart();
    t.src = src;
    t.size = _new_size;
    t.crc = crc;
  }

  uint32_t internalCrc32(uint32_t addr, uint32_t size) {
    uint32_t crc = 0;
    for (uint32_t off = 0; off < size; off += NRF52_OTA_PAGE) {
      uint32_t n = size - off < NRF52_OTA_PAGE ? size - off : NRF52_OTA_PAGE;
      crc = ota_stage_crc32(crc, (const uint8_t*)(addr + off), n);
      nrf52FeedWatchdog();
    }
    return crc;
  }

  bool bootable(uint32_t sp, uint32_t reset) {
    return sp > 0x20000000 && sp <= 0x20040000 && (sp & 3) == 0
           && (reset & 1) && reset > appStart() && reset < appStart() + _new_size;
  }

  static void stopSoftDevice() {
    uint8_t sd_en = 0;
    sd_softdevice_is_enabled(&sd_en);
    if (sd_en) sd_softdevice_disable();
  }

  void run(const Nrf52OtaPiece* pieces, uint32_t num, uint32_t* buf, uint16_t crc) {
    stopSoftDevice();
    nrf52OtaCopyAndReset(appStart(), pieces, num, buf, crc, _new_size);
  }

#ifdef QSPI_STORAGE
  // the new image is at QSPI_OTA_ADDR: keep the running one at QSPI_BACKUP_ADDR, then let the bootloader
  // copy (the new one runs on trial). Returns only when that failed.
  void commitStagedQspi() {
    uint8_t* buf = (uint8_t*) malloc(NRF52_OTA_PAGE);
    if (buf == NULL) return;
    uint32_t crc = 0;
    bool ok = true;
    for (uint32_t off = 0; ok && off < _new_size; off += NRF52_OTA_PAGE) {
      uint32_t n = _new_size - off < NRF52_OTA_PAGE ? _new_size - off : NRF52_OTA_PAGE;
      ok = _qspi->read(QSPI_OTA_ADDR + off, buf, n);
      crc = ota_stage_crc32(crc, buf, n);
      nrf52FeedWatchdog();
    }
    ota_stage_t t;
    fillRecord(t, OTA_STAGE_SRC_QSPI, QSPI_OTA_ADDR, crc);
    // the running image, for a way back
    uint32_t old_size = imageSize();
    if (ok && old_size <= QSPI_BACKUP_SIZE) {
      bool saved = true;
      uint32_t old_crc = internalCrc32(appStart(), old_size);
      for (uint32_t off = 0; saved && off < old_size; off += QSPI_SECTOR) {
        uint32_t n = old_size - off < QSPI_SECTOR ? old_size - off : QSPI_SECTOR;
        saved = _qspi->erase(QSPI_BACKUP_ADDR + off) && _qspi->write(QSPI_BACKUP_ADDR + off, (const uint8_t*)(appStart() + off), n);
        nrf52FeedWatchdog();
      }
      uint32_t back = 0;
      for (uint32_t off = 0; saved && off < old_size; off += NRF52_OTA_PAGE) {
        uint32_t n = old_size - off < NRF52_OTA_PAGE ? old_size - off : NRF52_OTA_PAGE;
        saved = _qspi->read(QSPI_BACKUP_ADDR + off, buf, n);
        back = ota_stage_crc32(back, buf, n);
        nrf52FeedWatchdog();
      }
      if (saved && back == old_crc) {
        t.flags |= OTA_STAGE_BACKUP;
        t.backup = QSPI_BACKUP_ADDR;
        t.backup_size = old_size;
        t.backup_crc = old_crc;
      }
    }
    free(buf);
    if (!ok) return;
    t.head_crc = ota_stage_head_crc(&t);
    _qspi->hold(false);
    _qspi->off();   // the bootloader takes it over
    stopSoftDevice();
    if (writeRecord(t)) resetNow();
  }
#endif

public:
  NRF52OtaTarget() : _new_size(0), _pos(0), _stage(0), _head(0), _tail(0), _patch_len(0), _patch_page(NULL) {
    _vec[0] = _vec[1] = 0;
#ifdef QSPI_STORAGE
    _qspi = NULL;
    _qbuf_len = 0;
#endif
  }

#ifdef QSPI_STORAGE
  void setQspi(QspiStorage* q) { _qspi = q; }
#endif

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
#ifdef QSPI_STORAGE
    if (onQspi()) return flashEnd() - appStart() < QSPI_OTA_SIZE ? flashEnd() - appStart() : QSPI_OTA_SIZE;
#endif
    uint32_t room = flashEnd() > staging() ? flashEnd() - staging() : 0;
    if (!stagedBootloader(1)) return room;
    // our bootloader: no bigger than the running image there, or a new one up to half the room
    uint32_t old_size = imageSize();
    uint32_t same = room < old_size ? room : old_size;
    uint32_t half = ((flashEnd() - appStart()) / 2) & ~(uint32_t)(NRF52_OTA_PAGE - 1);
    return half > old_size && half > same ? half : same;
  }

  // the delta (one page at least) and the end of the image in the free flash, the start in RAM; the copy
  // of the end goes down, so it must stay a page below where it is read from
  uint32_t maxInPlaceSize() override {
    if (onQspi() || flashEnd() <= staging() + NRF52_OTA_PAGE) return 0;
    uint32_t by_copy = flashEnd() - appStart() - 2 * NRF52_OTA_PAGE;
    uint32_t by_room = flashEnd() - staging() - NRF52_OTA_PAGE + NRF52_OTA_MAX_HEAD;
    return by_copy < by_room ? by_copy : by_room;
  }

  bool beginNew(uint32_t size) override {
    confirm();   // the next image takes the place of the way back
    stopInPlace();
    if (size > maxNewSize()) return false;
    _new_size = size;
    _stage = stagingFor(size);
    _pos = 0;
#ifdef QSPI_STORAGE
    _qbuf_len = 0;
#endif
    return true;
  }

  bool beginInPlace(uint32_t size, uint32_t patch_size) override {
    confirm();
    stopInPlace();
    if (onQspi() || size > maxInPlaceSize()) return false;
    uint32_t tail = staging() + pageUp(patch_size);
    if (tail >= flashEnd()) return false;
    uint32_t head = size > flashEnd() - tail ? pageUp(size - (flashEnd() - tail)) : 0;
    if (head == 0 || head > NRF52_OTA_MAX_HEAD || head >= size) return false;
    if (tail < appStart() + head + NRF52_OTA_PAGE) return false;   // the copy down would erase what it reads
    void* trial = malloc(head + NRF52_OTA_PAGE);   // is there RAM for it now (checked again at commit)
    if (trial == NULL) return false;
    free(trial);
    _patch_page = (uint8_t*) malloc(NRF52_OTA_PAGE);
    if (_patch_page == NULL) return false;
    _new_size = size;
    _pos = 0;
    _head = head;
    _tail = tail;
    _patch_len = 0;
    _vec[0] = _vec[1] = 0;
    return true;
  }

  uint32_t headSize() override { return _head; }

  bool writePatch(const uint8_t* src, size_t len) override {
    while (len > 0) {
      uint32_t used = _patch_len % NRF52_OTA_PAGE;
      size_t n = NRF52_OTA_PAGE - used < len ? NRF52_OTA_PAGE - used : len;
      if (_patch_page == NULL || staging() + _patch_len + n > _tail) return false;
      memcpy(&_patch_page[used], src, n);
      _patch_len += n;
      src += n; len -= n;
      if (_patch_len % NRF52_OTA_PAGE == 0
          && flash_nrf5x_write(staging() + _patch_len - NRF52_OTA_PAGE, _patch_page, NRF52_OTA_PAGE) < 0) return false;
    }
    return true;
  }

  bool readPatch(uint32_t offset, uint8_t* dest, size_t len) override {
    if (offset + len > _patch_len) return false;
    memcpy(dest, (const void*) (staging() + offset), len);
    return true;
  }

  bool writeNew(const uint8_t* src, size_t len) override {
    if (_pos + len > _new_size) return false;
    if (_head > 0) {
      while (len > 0 && _pos < _head) {   // the start: only the vector table's first words are kept
        if (_pos < 8) ((uint8_t*)_vec)[_pos] = *src;
        _pos++; src++; len--;
      }
      if (len == 0) return true;
      if (flash_nrf5x_write(_tail + _pos - _head, src, len) < 0) return false;
      _pos += len;
      return true;
    }
#ifdef QSPI_STORAGE
    if (onQspi()) {
      while (len > 0) {
        if (_pos % QSPI_SECTOR == 0) {
          if (!qspiFlush() || !_qspi->erase(QSPI_OTA_ADDR + _pos)) return false;
        }
        size_t room = QSPI_SECTOR - _pos % QSPI_SECTOR;
        if (room > sizeof(_qbuf) - _qbuf_len) room = sizeof(_qbuf) - _qbuf_len;
        size_t n = len < room ? len : room;
        memcpy(&_qbuf[_qbuf_len], src, n);
        _qbuf_len += n;
        _pos += n; src += n; len -= n;
        if ((_qbuf_len == sizeof(_qbuf) || _pos % QSPI_SECTOR == 0) && !qspiFlush()) return false;
      }
      return true;
    }
#endif
    if (flash_nrf5x_write(_stage + _pos, src, len) < 0) return false;
    _pos += len;
    return true;
  }

  bool finishNew() override {
#ifdef QSPI_STORAGE
    if (onQspi() && _head == 0 && !qspiFlush()) return false;
#endif
    if (_head > 0) {
      if (!flushPatchPage()) return false;
      if (_pos == _new_size) {   // the whole delta is in: its page goes back to the heap before makeHead()
        free(_patch_page);
        _patch_page = NULL;
      }
    }
    flash_nrf5x_flush();
    return _pos == _new_size;
  }

  bool readNew(uint32_t offset, uint8_t* dest, size_t len) override {
    if (_head > 0) {
      if (offset < _head) return false;
      memcpy(dest, (const void*) (_tail + offset - _head), len);
      return true;
    }
#ifdef QSPI_STORAGE
    if (onQspi()) return _qspi->read(QSPI_OTA_ADDR + offset, dest, len);
#endif
    memcpy(dest, (const void*) (_stage + offset), len);
    return true;
  }

  bool newLooksBootable() override {
    // vector table of an app for this chip and this flash layout: stack in RAM, reset handler in the app
    if (_head > 0) return bootable(_vec[0], _vec[1]);
    uint32_t v[2];
    if (!readNew(0, (uint8_t*)v, sizeof(v))) return false;
    return bootable(v[0], v[1]);
  }

  void abortNew() override {
    flash_nrf5x_flush();
    stopInPlace();
    _pos = 0;
#ifdef QSPI_STORAGE
    _qbuf_len = 0;
#endif
  }

  void commit() override {
    flash_nrf5x_flush();
    Nrf52OtaPiece piece;
    piece.pages = pageUp(_new_size) / NRF52_OTA_PAGE;
#ifdef QSPI_STORAGE
    if (onQspi()) {
      if (stagedBootloader(3)) {
        commitStagedQspi();
        return;   // only when it failed
      }
      uint32_t* buf = (uint32_t*) malloc(NRF52_OTA_PAGE);
      if (buf == NULL || !_qspi->on()) { free(buf); return; }
      uint16_t crc = 0xFFFF;
      for (uint32_t off = 0; off < _new_size; off += NRF52_OTA_PAGE) {
        uint32_t n = _new_size - off < NRF52_OTA_PAGE ? _new_size - off : NRF52_OTA_PAGE;
        if (!_qspi->read(QSPI_OTA_ADDR + off, (uint8_t*) buf, n)) { free(buf); return; }
        crc = nrf52Crc16(crc, (const uint8_t*) buf, n);
        nrf52FeedWatchdog();
      }
      _qspi->hold(true);
      piece.src = QSPI_OTA_ADDR;
      piece.qspi = 1;
      run(&piece, 1, buf, crc);
      _qspi->hold(false);
      free(buf);
      return;
    }
#endif
    if (stagedBootloader(1)) {
      ota_stage_t t;
      fillRecord(t, 0, _stage, internalCrc32(_stage, _new_size));
      t.head_crc = ota_stage_head_crc(&t);
      stopSoftDevice();
      if (writeRecord(t)) resetNow();
      return;
    }
    piece.src = _stage;
    piece.qspi = 0;
    run(&piece, 1, NULL, nrf52Crc16(0xFFFF, (const uint8_t*) _stage, _new_size));
  }

  // head: the first _head bytes of the new image, in RAM; the rest is at _tail
  void commitInPlace(const uint8_t* head) override {
    flash_nrf5x_flush();
    uint16_t crc = nrf52Crc16(0xFFFF, head, _head);
    crc = nrf52Crc16(crc, (const uint8_t*) _tail, _new_size - _head);
    Nrf52OtaPiece pieces[2];
    pieces[0].src = (uint32_t) head;
    pieces[0].pages = _head / NRF52_OTA_PAGE;
    pieces[0].qspi = 0;
    pieces[1].src = _tail;
    pieces[1].pages = pageUp(_new_size - _head) / NRF52_OTA_PAGE;
    pieces[1].qspi = 0;
    run(pieces, 2, NULL, crc);
  }

  // the record of the last staged install, if this app is what it installed
  const ota_stage_t* lastInstall() {
    const ota_stage_t* r = ota_stage_find();
    return r != NULL && r->installed == OTA_STAGE_SET && r->dst == appStart() ? r : NULL;
  }

  bool isUnconfirmed() override {
    const ota_stage_t* r = lastInstall();
    return r != NULL && ota_stage_on_trial(r);
  }

  void confirm() override {
    const ota_stage_t* r = lastInstall();
    if (r != NULL && ota_stage_on_trial(r)) markWord(&r->confirmed);
  }

  // the image that ran before the last update (kept in the QSPI flash): the bootloader copies it back
  bool rollback() {
    const ota_stage_t* r = lastInstall();
    if (r == NULL || !(r->flags & OTA_STAGE_BACKUP) || r->rb_started == OTA_STAGE_SET || r->failed == OTA_STAGE_SET) {
      return false;
    }
    if (r->rb_request != OTA_STAGE_SET && !markWord(&r->rb_request)) return false;
    resetNow();
    return true;
  }
};
