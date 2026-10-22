// Host test of the bootloader's staged install (../ota_stage.c), with power cuts.
//   cc -O1 -I fake -o test_ota_stage test_ota_stage.c ../ota_stage.c && ./test_ota_stage
// The internal flash and the QSPI peripheral's page are mapped at their real addresses (Linux), the
// registers are fakes: every NVMC access may be "the power cut", which longjmps out of the run.

#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "fake/nrf.h"
#include "fake/dfu_types.h"
#include "fake/ota_stage.h"

void ota_stage_run(void);

#define FLASH_LO   0x26000u
#define FLASH_HI   0x100000u
#define APP        DFU_BANK_0_REGION_START
#define QSPI_SIZE  0x200000u
#define BACKUP_AT  0x130000u

static fake_nvmc_t nvmc;
static fake_qspi_t qspi;
fake_wdt_t fake_wdt;
static uint8_t* qflash;
static long ops, cut_at = -1;
static jmp_buf cut;
static int failures;
static int qspi_asleep;   // the flash in deep power down: ACTIVATE never gets READY until 0xAB

// the flash's SPI pins as qspi_wake() drives them: 0xAB with CSN low wakes it
static uint8_t gpio[64], wake_byte, wake_bits;
void nrf_gpio_cfg_output(uint32_t pin) { (void) pin; }
void nrf_gpio_pin_write(uint32_t pin, uint32_t v) {
  if (pin == 21 && v && !gpio[21] && !gpio[25]) { wake_byte = (uint8_t)(wake_byte << 1 | gpio[20]); wake_bits++; }   // SCK rises
  if (pin == 25 && v && !gpio[25] && wake_bits == 8 && wake_byte == 0xAB) qspi_asleep = 0;                       // CSN rises
  if (pin == 25 && !v) wake_bits = 0;
  gpio[pin] = v ? 1 : 0;
}
void nrf_gpio_pin_set(uint32_t pin) { nrf_gpio_pin_write(pin, 1); }
void nrf_gpio_pin_clear(uint32_t pin) { nrf_gpio_pin_write(pin, 0); }

fake_nvmc_t* fake_nvmc(void) {
  if (nvmc.ERASEPAGE) {
    memset((void*)(uintptr_t) nvmc.ERASEPAGE, 0xFF, 4096);
    nvmc.ERASEPAGE = 0;
  }
  nvmc.READY = 1;
  if (cut_at >= 0 && ops++ == cut_at) longjmp(cut, 1);
  return &nvmc;
}

fake_qspi_t* fake_qspi(void) {
  if (qspi.TASKS_ACTIVATE) { qspi.TASKS_ACTIVATE = 0; if (!qspi_asleep) qspi.EVENTS_READY = 1; }
  if (qspi.CINSTRCONF) { qspi.CINSTRCONF = 0; qspi.CINSTRDAT0 = 0; qspi.EVENTS_READY = 1; }
  if (qspi.TASKS_READSTART) {
    qspi.TASKS_READSTART = 0;
    if (qspi.READ.SRC + qspi.READ.CNT <= QSPI_SIZE)
      memcpy((void*)(uintptr_t) qspi.READ.DST, qflash + qspi.READ.SRC, qspi.READ.CNT);
    qspi.EVENTS_READY = 1;
  }
  return &qspi;
}

#define CHECK(c) do { if (!(c)) { printf("  FAIL %s:%d %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static uint8_t* mem(uint32_t a) { return (uint8_t*)(uintptr_t) a; }

static void fill(uint8_t* p, uint32_t n, uint32_t seed) {
  for (uint32_t i = 0; i < n; i++) { seed = seed * 1103515245u + 12345u; p[i] = (uint8_t)(seed >> 16); }
}

static uint8_t img_old[300 * 1024], img_new[310 * 1024];

// a node running img_old, with bank_0 valid and a CRC from an earlier DFU
static void reset_node(void) {
  memset(mem(FLASH_LO), 0xFF, FLASH_HI - FLASH_LO);
  memset(qflash, 0xFF, QSPI_SIZE);
  memcpy(mem(APP), img_old, sizeof(img_old));
  uint32_t* s = (uint32_t*) mem(OTA_STAGE_PAGE);
  s[0] = 0xBEEF0001u;   // bank_0 = valid, bank_0_crc = 0xBEEF
  s[1] = 0xFFFF;
  s[2] = sizeof(img_old);
  ops = 0;
  cut_at = -1;
}

static ota_stage_t* write_record(uint32_t flags, uint32_t src) {
  ota_stage_t* r = ota_stage_free_slot();
  ota_stage_t t;
  memset(&t, 0xFF, sizeof(t));
  t.magic = OTA_STAGE_MAGIC;
  t.version = OTA_STAGE_VERSION;
  t.flags = flags;
  t.dst = APP;
  t.src = src;
  t.size = sizeof(img_new);
  t.crc = ota_stage_crc32(0, img_new, sizeof(img_new));
  if (flags & OTA_STAGE_BACKUP) {
    t.backup = BACKUP_AT;
    t.backup_size = sizeof(img_old);
    t.backup_crc = ota_stage_crc32(0, img_old, sizeof(img_old));
  }
  t.head_crc = ota_stage_head_crc(&t);
  memcpy(r, &t, sizeof(t));
  return r;
}

static void stage_qspi(int backup) {
  memcpy(qflash, img_new, sizeof(img_new));
  if (backup) memcpy(qflash + BACKUP_AT, img_old, sizeof(img_old));
}

static int app_is(const uint8_t* img, uint32_t n) { return memcmp(mem(APP), img, n) == 0; }
static uint32_t settings0(void) { return *(uint32_t*) mem(OTA_STAGE_PAGE); }

// one start of the bootloader; 1 when the power was cut
static int boot(long cut_after) {
  ops = 0;
  cut_at = cut_after;
  if (setjmp(cut)) { cut_at = -1; return 1; }
  ota_stage_run();
  cut_at = -1;
  return 0;
}

static long count_ops(void) { long n; cut_at = 1L << 40; ops = 0; ota_stage_run(); n = ops; cut_at = -1; return n; }

static void test_nothing(void) {
  printf("no record\n");
  reset_node();
  boot(-1);
  CHECK(app_is(img_old, sizeof(img_old)));
  CHECK(settings0() == 0xBEEF0001u);
}

static void test_internal(void) {
  printf("internal flash source\n");
  reset_node();
  uint32_t src = APP + 0x50000;
  memcpy(mem(src), img_new, sizeof(img_new));
  memset(mem(src + sizeof(img_new)), 0xAA, 2048);   // whatever follows the image in its last page
  const ota_stage_t* r = write_record(0, src);
  boot(-1);
  CHECK(app_is(img_new, sizeof(img_new)));
  for (int i = 0; i < 2048; i++) CHECK(mem(APP + sizeof(img_new))[i] == 0xFF);
  CHECK(r->installed == OTA_STAGE_SET && r->failed == OTA_STAGE_UNSET);
  CHECK(settings0() == 0x00000001u);   // valid, CRC check off
  boot(-1);   // nothing more: no backup, no trial
  CHECK(app_is(img_new, sizeof(img_new)) && r->boots == OTA_STAGE_UNSET);
}

static void test_bad_source(void) {
  printf("bad source: nothing touched\n");
  reset_node();
  stage_qspi(1);
  qflash[1234] ^= 1;
  const ota_stage_t* r = write_record(OTA_STAGE_SRC_QSPI | OTA_STAGE_BACKUP, 0);
  boot(-1);
  CHECK(app_is(img_old, sizeof(img_old)));
  CHECK(r->failed == OTA_STAGE_SET && r->started == OTA_STAGE_UNSET);
  CHECK(settings0() == 0xBEEF0001u);
}

static void test_wrong_dst(void) {
  printf("record for another app start\n");
  reset_node();
  stage_qspi(0);
  ota_stage_t* r = write_record(OTA_STAGE_SRC_QSPI, 0);
  memset(r, 0xFF, sizeof(*r));
  ota_stage_t t;
  memset(&t, 0xFF, sizeof(t));
  t.magic = OTA_STAGE_MAGIC; t.version = OTA_STAGE_VERSION; t.flags = OTA_STAGE_SRC_QSPI;
  t.dst = 0x26000; t.src = 0; t.size = sizeof(img_new); t.crc = ota_stage_crc32(0, img_new, sizeof(img_new));
  t.head_crc = ota_stage_head_crc(&t);
  memcpy(r, &t, sizeof(t));
  boot(-1);
  CHECK(app_is(img_old, sizeof(img_old)) && r->failed == OTA_STAGE_SET);
}

static void test_flash_asleep(void) {
  printf("QSPI flash in deep power down (the app leaves it so)\n");
  reset_node();
  stage_qspi(1);
  const ota_stage_t* r = write_record(OTA_STAGE_SRC_QSPI | OTA_STAGE_BACKUP, 0);
  qspi_asleep = 1;
  boot(-1);
  CHECK(!qspi_asleep && app_is(img_new, sizeof(img_new)) && r->installed == OTA_STAGE_SET);
}

static void test_trial_and_rollback(void) {
  printf("QSPI: trial starts, then back to the old image\n");
  reset_node();
  stage_qspi(1);
  const ota_stage_t* r = write_record(OTA_STAGE_SRC_QSPI | OTA_STAGE_BACKUP, 0);
  boot(-1);   // install (the first trial start is the next one)
  CHECK(app_is(img_new, sizeof(img_new)) && r->installed == OTA_STAGE_SET);
  for (uint32_t i = 1; i <= OTA_STAGE_TRIAL_BOOTS; i++) {
    boot(-1);
    CHECK(app_is(img_new, sizeof(img_new)) && ota_stage_boots_used(r) == i);
  }
  boot(-1);   // never confirmed
  CHECK(app_is(img_old, sizeof(img_old)) && r->rb_done == OTA_STAGE_SET);
  boot(-1);
  CHECK(app_is(img_old, sizeof(img_old)));
  CHECK((settings0() & 0xFFFF) == 1);
}

static void test_confirmed(void) {
  printf("QSPI: confirmed, later `ota rollback`\n");
  reset_node();
  stage_qspi(1);
  ota_stage_t* r = write_record(OTA_STAGE_SRC_QSPI | OTA_STAGE_BACKUP, 0);
  boot(-1);
  boot(-1);
  r->confirmed = OTA_STAGE_SET;   // the app
  for (int i = 0; i < 6; i++) boot(-1);
  CHECK(app_is(img_new, sizeof(img_new)) && ota_stage_boots_used(r) == 1 && r->rb_started == OTA_STAGE_UNSET);
  r->rb_request = OTA_STAGE_SET;
  boot(-1);
  CHECK(app_is(img_old, sizeof(img_old)) && r->rb_done == OTA_STAGE_SET);
}

static void test_half_written_record(void) {
  printf("half-written newer record: the older one is not acted on\n");
  reset_node();
  stage_qspi(1);
  const ota_stage_t* r = write_record(OTA_STAGE_SRC_QSPI | OTA_STAGE_BACKUP, 0);
  boot(-1);   // installed, on trial
  ota_stage_t* half = ota_stage_free_slot();
  half->magic = OTA_STAGE_MAGIC;   // power cut right after the first word
  CHECK(ota_stage_find() == 0);
  for (uint32_t i = 0; i <= OTA_STAGE_TRIAL_BOOTS; i++) boot(-1);
  CHECK(app_is(img_new, sizeof(img_new)) && r->boots == OTA_STAGE_UNSET && r->rb_started == OTA_STAGE_UNSET);
  const ota_stage_t* next = write_record(OTA_STAGE_SRC_QSPI | OTA_STAGE_BACKUP, 0);   // the next update
  CHECK(next != half && ota_stage_find() == next);
}

static void test_app_changed(void) {
  printf("QSPI: app flashed another way while on trial\n");
  reset_node();
  stage_qspi(1);
  const ota_stage_t* r = write_record(OTA_STAGE_SRC_QSPI | OTA_STAGE_BACKUP, 0);
  boot(-1);
  mem(APP)[100] ^= 0xFF;   // e.g. SWD
  for (int i = 0; i < 6; i++) boot(-1);
  CHECK(r->failed == OTA_STAGE_SET && r->rb_started == OTA_STAGE_UNSET);
  CHECK(mem(APP)[100] == (img_new[100] ^ 0xFF));
}

static void test_both_sources_bad(void) {
  printf("QSPI: source and backup go bad half way: no app, DFU\n");
  reset_node();
  stage_qspi(1);
  ota_stage_t* r = write_record(OTA_STAGE_SRC_QSPI | OTA_STAGE_BACKUP, 0);
  long total = count_ops();   // a full install, to cut it half way
  reset_node();
  stage_qspi(1);
  r = write_record(OTA_STAGE_SRC_QSPI | OTA_STAGE_BACKUP, 0);
  CHECK(boot(total / 2) == 1 && r->started == OTA_STAGE_SET);
  qflash[100] ^= 1;
  qflash[BACKUP_AT + 100] ^= 1;
  boot(-1);
  CHECK(r->failed == OTA_STAGE_SET);
  CHECK((settings0() & 0xFFFF) == 0);   // the stock code stays in DFU
}

// a power cut at every step of a run (sampled), then normal starts: always ends with the expected app
static void test_power_cuts(const char* name, int qspi_src, int rollback) {
  printf("power cuts: %s\n", name);
  uint32_t flags = qspi_src ? (OTA_STAGE_SRC_QSPI | (rollback ? OTA_STAGE_BACKUP : 0)) : 0;
  uint32_t src = qspi_src ? 0 : APP + 0x50000;
  // how many steps the run under test takes
  reset_node();
  if (qspi_src) stage_qspi(rollback); else memcpy(mem(src), img_new, sizeof(img_new));
  ota_stage_t* r = write_record(flags, src);
  if (rollback) { boot(-1); r->rb_request = OTA_STAGE_SET; }
  long total = count_ops();
  int tested = 0;
  for (long k = 0; k < total; k += (k < 64 ? 1 : 1 + total / 300)) {
    reset_node();
    if (qspi_src) stage_qspi(rollback); else memcpy(mem(src), img_new, sizeof(img_new));
    r = write_record(flags, src);
    if (rollback) { boot(-1); r->rb_request = OTA_STAGE_SET; }
    if (!boot(k)) continue;
    boot(-1);
    tested++;
    if (rollback) {
      if (!(app_is(img_old, sizeof(img_old)) && r->rb_done == OTA_STAGE_SET)) {
        printf("  FAIL cut at %ld of %ld\n", k, total); failures++; break;
      }
    } else if (r->started == OTA_STAGE_UNSET) {
      if (!app_is(img_old, sizeof(img_old))) { printf("  FAIL cut at %ld: old app changed\n", k); failures++; break; }
    } else if (!(app_is(img_new, sizeof(img_new)) && r->installed == OTA_STAGE_SET && (settings0() & 0xFFFF) == 1)) {
      printf("  FAIL cut at %ld of %ld\n", k, total); failures++; break;
    }
  }
  printf("  %d cuts out of %ld steps\n", tested, total);
}

int main(void) {
  void* f = mmap((void*)(uintptr_t) FLASH_LO, FLASH_HI - FLASH_LO, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
  void* q = mmap((void*) 0x40029000ul, 4096, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);   // anomaly 122 registers
  if (f != (void*)(uintptr_t) FLASH_LO || q != (void*) 0x40029000ul) { perror("mmap"); return 2; }
  qflash = malloc(QSPI_SIZE);
  fill(img_old, sizeof(img_old), 1);
  fill(img_new, sizeof(img_new), 2);

  test_nothing();
  test_internal();
  test_bad_source();
  test_wrong_dst();
  test_flash_asleep();
  test_trial_and_rollback();
  test_confirmed();
  test_half_written_record();
  test_app_changed();
  test_both_sources_bad();
  test_power_cuts("install from internal flash", 0, 0);
  test_power_cuts("install from QSPI", 1, 0);
  test_power_cuts("rollback from QSPI", 1, 1);

  printf(failures ? "%d FAILED\n" : "all passed\n", failures);
  return failures ? 1 : 0;
}
