/*
 * Staged LoRa OTA install for MeshCore repeaters (see ota_stage.h).
 *
 * Runs at every start, before the DFU checks. Nothing to do unless the app left a record in the
 * settings page. The copy is done here, with nothing else running: the source is never written, so a
 * power cut only makes it start again at the next start.
 */

#include <stdbool.h>
#include <stdint.h>
#include "nrf.h"
#include "boards.h"
#include "dfu_types.h"
#include "ota_stage.h"

#define PAGE            4096u
#define SETTINGS_WORD0  (*(volatile uint32_t*) OTA_STAGE_PAGE)   // u16 bank_0, u16 bank_0_crc

// read by the app: this bootloader does staged installs (and from the QSPI flash, with rollback)
#ifdef OTA_STAGE_QSPI_SCK
  #define FEATURES 3u
#else
  #define FEATURES 1u
#endif
__attribute__((section(".otaStageInfo"), used))
const uint32_t ota_stage_info[2] = { OTA_STAGE_BL_INFO_MAGIC, FEATURES };

static uint32_t buf[PAGE / 4];

static void wdt_feed(void) {
  if (NRF_WDT->RUNSTATUS) {
    for (uint32_t r = 0; r < 8; r++) {
      if (NRF_WDT->RREN & (1UL << r)) NRF_WDT->RR[r] = WDT_RR_RR_Reload;
    }
  }
}

static void nvmc(uint32_t mode) {
  NRF_NVMC->CONFIG = mode;
  while (NRF_NVMC->READY == 0) { }
}

static void flash_word(volatile uint32_t* at, uint32_t v) {
  nvmc(NVMC_CONFIG_WEN_Wen);
  *at = v;
  while (NRF_NVMC->READY == 0) { }
  nvmc(NVMC_CONFIG_WEN_Ren);
}

static void mark(const uint32_t* state) {
  flash_word((volatile uint32_t*) state, OTA_STAGE_SET);
}

// the copy runs the app's code over: the stock check must not trip on an old CRC (0 = no check)
static void no_crc_check(void) {
  if ((SETTINGS_WORD0 & 0xFFFF0000u) != 0) flash_word(&SETTINGS_WORD0, SETTINGS_WORD0 & 0x0000FFFFu);
}

// half an app: the stock code then stays in DFU (BLE OTA) until a normal update
static void no_valid_app(void) {
  flash_word(&SETTINGS_WORD0, SETTINGS_WORD0 & 0xFFFF0000u);
}

/*------------------------------------------------------------------*/
/* QSPI flash (Xiao nRF52840): reads only, single line, 8 MHz
 *------------------------------------------------------------------*/
#ifdef OTA_STAGE_QSPI_SCK

static bool qspi_wait(void) {
  for (uint32_t n = 0; n < 8000000u; n++) {
    if (NRF_QSPI->EVENTS_READY) {
      NRF_QSPI->EVENTS_READY = 0;
      return true;
    }
  }
  return false;
}

static bool qspi_cmd(uint8_t opcode, uint32_t len) {   // len: opcode + data bytes
  NRF_QSPI->EVENTS_READY = 0;
  NRF_QSPI->CINSTRCONF = opcode | (len << QSPI_CINSTRCONF_LENGTH_Pos)
                         | QSPI_CINSTRCONF_LIO2_Msk | QSPI_CINSTRCONF_LIO3_Msk;
  return qspi_wait();
}

static bool qspi_on;

static bool qspi_begin(void) {
  if (qspi_on) return true;
  NRF_QSPI->PSEL.SCK = OTA_STAGE_QSPI_SCK;
  NRF_QSPI->PSEL.CSN = OTA_STAGE_QSPI_CSN;
  NRF_QSPI->PSEL.IO0 = OTA_STAGE_QSPI_IO0;
  NRF_QSPI->PSEL.IO1 = OTA_STAGE_QSPI_IO1;
  NRF_QSPI->PSEL.IO2 = OTA_STAGE_QSPI_IO2;
  NRF_QSPI->PSEL.IO3 = OTA_STAGE_QSPI_IO3;
  NRF_QSPI->IFCONFIG0 = 0;                                     // FASTREAD, PP, 24 bit addresses
  NRF_QSPI->IFCONFIG1 = (1u << QSPI_IFCONFIG1_SCKDELAY_Pos) | (3u << QSPI_IFCONFIG1_SCKFREQ_Pos);   // 32/4 MHz
  NRF_QSPI->ENABLE = 1;
  NRF_QSPI->EVENTS_READY = 0;
  NRF_QSPI->TASKS_ACTIVATE = 1;
  if (!qspi_wait()) return false;
  qspi_cmd(0xAB, 1);                                           // out of deep power down (the app leaves it there)
  NRFX_DELAY_US(50);
  for (uint32_t n = 0; n < 1000000u; n++) {                    // a write may still be going on
    if (!qspi_cmd(0x05, 2)) return false;                      // read status
    if ((NRF_QSPI->CINSTRDAT0 & 1u) == 0) {
      qspi_on = true;
      return true;
    }
  }
  return false;
}

static void qspi_end(void) {
  if (!qspi_on) return;
  NRF_QSPI->TASKS_DEACTIVATE = 1;
  *(volatile uint32_t*) 0x40029010ul = 1;   // anomaly 122: QSPI keeps drawing current otherwise
  *(volatile uint32_t*) 0x40029054ul = 1;
  NRF_QSPI->ENABLE = 0;
  qspi_on = false;
}

static bool qspi_read(uint32_t addr, uint32_t len) {
  NRF_QSPI->READ.SRC = addr;
  NRF_QSPI->READ.DST = (uint32_t)(uintptr_t) buf;
  NRF_QSPI->READ.CNT = len;
  NRF_QSPI->EVENTS_READY = 0;
  NRF_QSPI->TASKS_READSTART = 1;
  return qspi_wait();
}

#endif

// one page of the source into buf (len: a multiple of 4)
static bool src_read(uint32_t flags, uint32_t addr, uint32_t len) {
  if (flags & OTA_STAGE_SRC_QSPI) {
#ifdef OTA_STAGE_QSPI_SCK
    return qspi_begin() && qspi_read(addr, len);
#else
    return false;
#endif
  }
  if (addr < DFU_BANK_0_REGION_START || addr + len > BOOTLOADER_REGION_START) return false;
  for (uint32_t w = 0; w < len / 4; w++) buf[w] = ((const uint32_t*)(uintptr_t) addr)[w];
  return true;
}

static bool src_crc(uint32_t flags, uint32_t addr, uint32_t size, uint32_t want) {
  uint32_t crc = 0;
  for (uint32_t off = 0; off < size; off += PAGE) {
    uint32_t n = size - off < PAGE ? size - off : PAGE;
    if (!src_read(flags, addr + off, (n + 3) & ~3u)) return false;
    crc = ota_stage_crc32(crc, (const uint8_t*) buf, n);
    wdt_feed();
  }
  return crc == want;
}

static bool app_crc(uint32_t size, uint32_t want) {
  return ota_stage_crc32(0, (const uint8_t*)(uintptr_t) DFU_BANK_0_REGION_START, size) == want;
}

// the image at src over the app, page by page; pages already the same are left as they are
static bool copy(uint32_t flags, uint32_t src, uint32_t size) {
  uint32_t dst = DFU_BANK_0_REGION_START;
  for (uint32_t off = 0; off < size; off += PAGE) {
    wdt_feed();
    if (!src_read(flags, src + off, PAGE)) return false;
    volatile uint32_t* to = (volatile uint32_t*)(uintptr_t)(dst + off);
    bool same = true;
    for (uint32_t w = 0; w < PAGE / 4 && same; w++) same = to[w] == buf[w];
    if (same) continue;
    nvmc(NVMC_CONFIG_WEN_Een);
    NRF_NVMC->ERASEPAGE = dst + off;
    while (NRF_NVMC->READY == 0) { }
    nvmc(NVMC_CONFIG_WEN_Wen);
    for (uint32_t w = 0; w < PAGE / 4; w++) {
      to[w] = buf[w];
      while (NRF_NVMC->READY == 0) { }
    }
    nvmc(NVMC_CONFIG_WEN_Ren);
  }
  return true;
}

static bool fits(uint32_t size) {
  return size > 0 && DFU_BANK_0_REGION_START + size <= BOOTLOADER_REGION_START;
}

// the old image back from the QSPI flash (only with OTA_STAGE_BACKUP)
static void roll_back(const ota_stage_t* r) {
  if (!fits(r->backup_size) || !src_crc(r->flags, r->backup, r->backup_size, r->backup_crc)) {
    // the backup is gone: fine if the new image is in place, not when either copy stopped half way
    if (r->rb_started == OTA_STAGE_SET || r->installed != OTA_STAGE_SET) no_valid_app();
    mark(&r->failed);
    return;
  }
  if (r->rb_started != OTA_STAGE_SET) mark(&r->rb_started);
  no_crc_check();
  for (int tries = 0; tries < 2; tries++) {
    if (copy(r->flags, r->backup, r->backup_size) && app_crc(r->backup_size, r->backup_crc)) {
      mark(&r->rb_done);
      return;
    }
  }
  no_valid_app();
  mark(&r->failed);
}

static void install(const ota_stage_t* r) {
  bool backup = (r->flags & OTA_STAGE_BACKUP) != 0;
  if (!fits(r->size) || !src_crc(r->flags, r->src, r->size, r->crc)) {
    if (r->started != OTA_STAGE_SET) {
      mark(&r->failed);   // nothing done: the old app runs on
    } else if (backup) {
      roll_back(r);
    } else {
      no_valid_app();
      mark(&r->failed);
    }
    return;
  }
  if (r->started != OTA_STAGE_SET) mark(&r->started);
  no_crc_check();
  for (int tries = 0; tries < 2; tries++) {
    if (copy(r->flags, r->src, r->size) && app_crc(r->size, r->crc)) {
      mark(&r->installed);
      return;
    }
  }
  if (backup) {
    roll_back(r);
  } else {
    no_valid_app();
    mark(&r->failed);
  }
}

void ota_stage_run(void) {
  const ota_stage_t* r = ota_stage_find();
  if (r == 0 || r->failed == OTA_STAGE_SET || r->rb_done == OTA_STAGE_SET) return;
  if (r->dst != DFU_BANK_0_REGION_START) {
    mark(&r->failed);
    return;
  }

  if (r->rb_started == OTA_STAGE_SET) {
    roll_back(r);   // a power cut while going back
  } else if (r->installed != OTA_STAGE_SET) {
    install(r);
  } else if ((r->flags & OTA_STAGE_BACKUP) && r->rb_request == OTA_STAGE_SET) {
    roll_back(r);   // `ota rollback`
  } else if (ota_stage_on_trial(r)) {
    if (!app_crc(r->size, r->crc)) {
      mark(&r->failed);   // the app was changed some other way (e.g. by SWD): no more trial
    } else if (ota_stage_boots_used(r) >= OTA_STAGE_TRIAL_BOOTS) {
      roll_back(r);
    } else {
      uint32_t used = ota_stage_boots_used(r);
      flash_word((volatile uint32_t*) &r->boots, r->boots & ~(1u << used));
    }
  }
#ifdef OTA_STAGE_QSPI_SCK
  qspi_end();
#endif
  wdt_feed();
}
