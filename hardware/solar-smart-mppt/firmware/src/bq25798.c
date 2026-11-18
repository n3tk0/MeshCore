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
  if (ok) {
    memcpy(b->r, tmp, sizeof(tmp));
    b->errors = 0;
    b->secs++;
    bool poor = tmp[BQ_REG_FLAG0] & 0x10;   // the read above cleared it: count it here
    if (poor) {
      if (b->poorsrc_count < 0xFFFF) b->poorsrc_count++;
      b->poorsrc_secs = b->secs;
    }
    bool hiz = tmp[BQ_REG_CTRL0] & 0x04;
    bool unl = (tmp[BQ_REG_STAT0] & 0x01) && (hiz || !(tmp[BQ_REG_STAT0] & 0x08) || poor);
    // VBUS is the open-circuit voltage when the converter did not load it for the whole last second:
    // not loading now and at the refresh before, ADC running, and not just restarted by our EN_HIZ clear
    b->voc_mv = bq_u16(b, BQ_REG_VBUS);
    b->voc_ok = unl && b->unl_prev && !b->hiz_cleared && (tmp[BQ_REG_ADC_CTRL] & 0x80) && b->voc_mv >= BQ_VOC_MIN_MV;
    if (hiz || !unl) b->hiz_cleared = false;   // the chip took the input down again (or it is gone): next result is clean
    b->unl_prev = b->unl = unl;
  } else if (b->errors < 255) b->errors++;
  b->ok = ok;
  return ok;
}

uint16_t bq_vindpm_from_voc(uint16_t voc_mv) {
  uint32_t mv = (uint32_t)voc_mv * BQ_VOC_PCT / 100 / 100 * 100;
  return (uint16_t)(mv < BQ_VINDPM_MIN_MV ? BQ_VINDPM_MIN_MV : mv > BQ_VINDPM_MAX_MV ? BQ_VINDPM_MAX_MV : mv);
}

uint16_t bq_poorsrc_age_min(const bq_t *b) {
  if (!b->poorsrc_count) return 0xFFFF;
  uint32_t m = (b->secs - b->poorsrc_secs) / 60u;
  return (uint16_t)(m > 0xFFFE ? 0xFFFE : m);
}

uint16_t bq_why(const bq_t *b) {
  if (!bq_vbus_present(b)) return 0;
  uint16_t w = 0;
  if (b->r[BQ_REG_CTRL0] & 0x04) w |= ST2_HIZ;
  if (!(b->r[BQ_REG_STAT0] & 0x08)) w |= ST2_NO_PG;
  if (b->poorsrc_count && bq_poorsrc_age_min(b) < BQ_POORSRC_KEEP_MIN) w |= ST2_POOR_SRC;
  return w;
}

bool bq_vbus_present(const bq_t *b) { return b->ok && (b->r[BQ_REG_STAT0] & 0x01); }

// SFET_PRESENT: with it the chip unlocks ship mode but also stops on battery under 3.2 V (starts over
// 3.4 V; without it 2.4 / 2.6 V, SLUSDV2C 6.5): mid-plateau for LiFePO4, so not for LiFePO4
bool bq_sfet(uint8_t chem) { return chem != CHEM_LFP; }

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
  // TS reads 0 while the ADC (or its TS channel) is off: judge the NTC only with TS converting
  if ((b->r[BQ_REG_ADC_CTRL] & 0x80) && !(b->r[BQ_REG_ADC_DIS0] & 0x04) && bq_u16(b, BQ_REG_TS) != 0)
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
  bool mppt_mode = s->vindpm_mv == 0;
  bool mppt_running = b->r[BQ_REG_MPPT] & 1;
  if (!mppt_mode) set_bits(img, BQ_REG_VINDPM, 0xFF, (uint8_t)(s->vindpm_mv / 100));
  // EN_MPPT only with the panel qualified (PG) and the battery over VSYSMIN: the chip drops it when VBUS goes
  // and refuses it under VSYSMIN; without them it is left as the chip has it (the VINDPM below takes over)
  bool can = bq_vbus_present(b) && (b->r[BQ_REG_STAT0] & 0x08) && !(b->r[BQ_REG_STAT3] & 0x10);
  set_bits(img, BQ_REG_MPPT, 0xE1, (uint8_t)(s->mppt_code << 5 | (mppt_mode && (can || mppt_running) ? 1 : 0)));
  // MPPT mode, MPPT not running, panel there but not loaded (EN_HIZ / no PG / poor source): the chip's own
  // VINDPM (VBUS0 - 0.7 V, 3.6 V at the clamp) pulls a weak panel under VPOORSRC again and again. Take 80 %
  // of the open-circuit voltage instead; REG05 goes before REG0F in the write loop below, so it is in place
  // before EN_HIZ is cleared. EN_HIZ is held a little to get a no-load VBUS result first.
  bool hold_hiz = false;
  if (mppt_mode && !mppt_running && b->unl) {
    bool set = false;
    if (b->voc_ok) {
      uint16_t vin = bq_vindpm_from_voc(b->voc_mv);
      if (img[BQ_REG_VINDPM] != (uint8_t)(vin / 100)) b->auto_vin_count++;
      set_bits(img, BQ_REG_VINDPM, 0xFF, (uint8_t)(vin / 100));
      b->auto_vin_mv = vin;
      b->voc_ok = false;
      b->hiz_cnt = 0;
      set = true;
    }
    if (!set && (b->r[BQ_REG_CTRL0] & 0x04)) {
      if (b->hiz_cnt >= BQ_HIZ_HOLD_PERIOD) b->hiz_cnt = 0;
      hold_hiz = b->hiz_cnt < BQ_HIZ_HOLD_POLLS;
      if (b->hiz_cnt < 0xFF) b->hiz_cnt++;
    }
  } else {
    b->hiz_cnt = 0;
  }
  set_bits(img, BQ_REG_NTC1, 0x01, ts_ignore ? 1 : 0);
  set_bits(img, BQ_REG_CTRL1, 0x07, 5);             // watchdog 40 s
  if (!b->ship_sent) set_bits(img, BQ_REG_CTRL2, 0x06, 0);   // never ship/shutdown from here
  // no D+/D- detection (not connected: it could drop IINDPM to 500 mA)
  set_bits(img, BQ_REG_CTRL2, 0x40, 0);
  set_bits(img, BQ_REG_CTRL5, 0x80, bq_sfet(chem) ? 0x80 : 0);
  set_bits(img, BQ_REG_ADC_CTRL, 0x80, 0x80);       // ADC on, continuous
  // TS channel only with VBUS: on battery alone it keeps REGN on and stops the ADC under 3.2 V
  set_bits(img, BQ_REG_ADC_DIS0, 0x04, bq_vbus_present(b) ? 0 : 0x04);
  // EN_CHG ours; EN_ICO off (MPPT runs instead); EN_HIZ always 0: a read that caught HIZ set (input
  // fault, VOC check) must never be written back and leave the charger in HIZ
  set_bits(img, BQ_REG_CTRL0, 0x34, charge_enable ? 0x20 : 0x00);
  if (hold_hiz) img[BQ_REG_CTRL0] |= 0x04;

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
  if (ok && img[BQ_REG_CTRL0] != b->r[BQ_REG_CTRL0]) {
    bool clears_hiz = (b->r[BQ_REG_CTRL0] & 0x04) && !(img[BQ_REG_CTRL0] & 0x04);
    ok = wr8(b, BQ_REG_CTRL0, img[BQ_REG_CTRL0]);
    if (ok && clears_hiz) b->hiz_cleared = true;   // the converter runs again: its VBUS is loaded
  }
  return ok;
}

bool bq_kick_watchdog(bq_t *b) {
  uint8_t v = (uint8_t)(b->r[BQ_REG_CTRL1] | 0x08);
  return b->bus->write(BQ_REG_CTRL1, &v, 1);   // WD_RST clears itself, keep r[] as it was
}

bool bq_ship(bq_t *b) {
  if (!(b->r[BQ_REG_CTRL5] & 0x80)) return false;   // no SFET_PRESENT: SDRV_CTRL is locked at 00
  // SDRV_CTRL = 10 (ship), SDRV_DLY = 1 (no 10 s delay)
  if (!wr8(b, BQ_REG_CTRL2, (uint8_t)((b->r[BQ_REG_CTRL2] & ~0x07) | 0x05))) return false;
  b->ship_sent = true;   // our supply may hold for a while: don't write it back to 00 meanwhile
  return true;
}
