#include "bq25798.h"
#include <string.h>

void bq_init(bq_t *b, const bq_bus_t *bus) {
  memset(b, 0, sizeof(*b));
  b->bus = bus;
}

uint16_t bq_u16(const bq_t *b, uint8_t reg) { return (uint16_t)(b->r[reg] << 8 | b->r[reg + 1]); }
int16_t bq_i16(const bq_t *b, uint8_t reg) { return (int16_t)bq_u16(b, reg); }

uint16_t bq_ts_x10(const bq_t *b) {
  // 0.0976563 % per bit = 1/1024 of 100 %
  return (uint16_t)((uint32_t)bq_u16(b, BQ_REG_TS) * 1000u / 1024u);
}

bool bq_refresh(bq_t *b) {
  uint8_t tmp[BQ_NREGS];
  // two blocks keep each transfer short
  bool ok = b->bus->read(0x00, tmp, 0x30) && b->bus->read(0x30, tmp + 0x30, BQ_NREGS - 0x30);
  if (ok) { memcpy(b->r, tmp, sizeof(tmp)); b->errors = 0; }
  else { if (b->errors < 255) b->errors++; b->configured = false; }
  b->ok = ok;
  return ok;
}

uint16_t bq_vsysmin_mv(uint8_t chem) {
  // below the BMS cut-off of the chemistry; LTO stays under the 5.5 V parts on SYS
  return chem == CHEM_LFP ? 2500 : chem == CHEM_LTO2S ? 3500 : 3000;
}

static bool wr8(bq_t *b, uint8_t reg, uint8_t v) {
  if (!b->bus->write(reg, &v, 1)) return false;
  b->r[reg] = v;
  return true;
}

static bool wr16(bq_t *b, uint8_t reg, uint16_t v) {
  uint8_t d[2] = { (uint8_t)(v >> 8), (uint8_t)v };
  if (!b->bus->write(reg, d, 2)) return false;
  b->r[reg] = d[0];
  b->r[reg + 1] = d[1];
  return true;
}

static void set_bits(uint8_t *img, uint8_t reg, uint8_t mask, uint8_t val) {
  img[reg] = (uint8_t)((img[reg] & ~mask) | (val & mask));
}

static void set16(uint8_t *img, uint8_t reg, uint16_t mask, uint16_t val) {
  set_bits(img, reg, (uint8_t)(mask >> 8), (uint8_t)(val >> 8));
  set_bits(img, (uint8_t)(reg + 1), (uint8_t)mask, (uint8_t)val);
}

// 16-bit registers: written as one 2-byte transfer
static bool is_pair_hi(uint8_t reg) { return reg == BQ_REG_VREG || reg == BQ_REG_ICHG || reg == BQ_REG_IINDPM; }

bool bq_apply(bq_t *b, const settings_t *s, uint8_t chem, bool charge_enable) {
  if (!b->ok) return false;
  b->ntc_present = bq_ts_x10(b) < BQ_TS_OPEN_X10;
  bool ts_ignore = s->ntc_mode == NTC_OFF || (s->ntc_mode == NTC_AUTO && !b->ntc_present);

  // wanted image: the chip as read, then the stored expert writes, then our own fields on top
  // (so an expert write can change the other bits of a register, never ours)
  uint8_t img[BQ_NREGS];
  memcpy(img, b->r, sizeof(img));
  for (int i = 0; i < s->expert_n && i < EXPERT_MAX; i++) {
    uint8_t reg = (uint8_t)(s->expert[i] >> 8);
    if (reg < BQ_NREGS) img[reg] = (uint8_t)s->expert[i];
  }
  set_bits(img, BQ_REG_VSYSMIN, 0x3F, (uint8_t)((bq_vsysmin_mv(chem) - 2500) / 250));
  set16(img, BQ_REG_VREG, 0x07FF, (uint16_t)(s->vchg_mv / 10));
  set16(img, BQ_REG_ICHG, 0x01FF, (uint16_t)(s->ichg_ma / 10));
  set16(img, BQ_REG_IINDPM, 0x01FF, (uint16_t)(s->iin_ma / 10));
  if (s->vindpm_mv) set_bits(img, BQ_REG_VINDPM, 0xFF, (uint8_t)(s->vindpm_mv / 100));
  set_bits(img, BQ_REG_MPPT, 0xE1, (uint8_t)(s->mppt_code << 5 | (s->vindpm_mv ? 0 : 1)));
  set_bits(img, BQ_REG_NTC1, 0x01, ts_ignore ? 1 : 0);
  set_bits(img, BQ_REG_CTRL1, 0x07, 5);             // watchdog 40 s
  set_bits(img, BQ_REG_CTRL2, 0x06, 0);             // never ship/shutdown from here
  set_bits(img, BQ_REG_ADC_CTRL, 0x80, 0x80);       // ADC on, continuous
  set_bits(img, BQ_REG_CTRL0, 0x20, charge_enable ? 0x20 : 0x00);

  bool ok = true;
  for (uint8_t reg = 0; ok && reg < BQ_NREGS; reg++) {
    if (reg == BQ_REG_CTRL0) continue;              // charging last
    if (is_pair_hi(reg)) {
      if (img[reg] != b->r[reg] || img[reg + 1] != b->r[reg + 1])
        ok = wr16(b, reg, (uint16_t)(img[reg] << 8 | img[reg + 1]));
      reg++;
      continue;
    }
    if (img[reg] != b->r[reg]) ok = wr8(b, reg, img[reg]);
  }
  if (ok && img[BQ_REG_CTRL0] != b->r[BQ_REG_CTRL0]) ok = wr8(b, BQ_REG_CTRL0, img[BQ_REG_CTRL0]);
  b->configured = ok;
  return ok;
}

bool bq_kick_watchdog(bq_t *b) {
  uint8_t v = (uint8_t)(b->r[BQ_REG_CTRL1] | 0x08);
  return b->bus->write(BQ_REG_CTRL1, &v, 1);   // WD_RST clears itself, keep r[] as it was
}

bool bq_ship(bq_t *b) {
  // SDRV_CTRL = 10 (ship), SDRV_DLY = 1 (no 10 s delay)
  return wr8(b, BQ_REG_CTRL2, (uint8_t)((b->r[BQ_REG_CTRL2] & ~0x07) | 0x05));
}
