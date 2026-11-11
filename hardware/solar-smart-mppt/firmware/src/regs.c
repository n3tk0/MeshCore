#include "regs.h"
#include "settings.h"
#include <string.h>

regs_t R;

static uint16_t *set_field(settings_t *s, uint8_t reg) {
  switch (reg) {
    case R_CAPACITY:   return &s->capacity_mah;
    case R_VCHG:       return &s->vchg_mv;
    case R_ICHG:       return &s->ichg_ma;
    case R_IIN:        return &s->iin_ma;
    case R_VINDPM:     return &s->vindpm_mv;
    case R_OD_OFF:     return &s->od_off_mv;
    case R_OD_ON:      return &s->od_on_mv;
    case R_SHIP:       return &s->ship_mv;
    case R_J7_OFF:     return &s->j7_off_mv;
    case R_J7_ON:      return &s->j7_on_mv;
    case R_IDSG_MAX:   return &s->idsg_max_ma;
    case R_ISC:        return &s->isc_ma;
    case R_BAL_DELTA:  return &s->bal_delta_mv;
    case R_BAL_START:  return &s->bal_start_mv;
    case R_ZERO_UV:    return (uint16_t *)&s->zero_uv;
    case R_SHUNT_UOHM: return &s->shunt_uohm;
    default:
      if (reg >= R_EXPERT0 && reg < R_EXPERT0 + EXPERT_MAX) return &s->expert[reg - R_EXPERT0];
      return 0;
  }
}

static int32_t clamp16(int32_t v) { return v > 32767 ? 32767 : v < -32768 ? -32768 : v; }

uint16_t regs_read(uint8_t addr7, uint8_t reg) {
  const live_t *L = &R.live;
  if (addr7 == ADDR_INA260) {
    switch (reg) {
      case INA_CONFIG:  return R.ina_config;
      case INA_CURRENT: {
        int32_t ma = R.set.ina_sign == INA_SIGN_CHARGE_POSITIVE ? L->ibat_ma : -L->ibat_ma;
        return (uint16_t)clamp16(ma * 4 / 5);                  // 1.25 mA per bit
      }
      case INA_VOLTAGE: return (uint16_t)((uint32_t)L->vpack_mv * 4 / 5);   // 1.25 mV per bit
      case INA_POWER: {
        int32_t ma = L->ibat_ma < 0 ? -L->ibat_ma : L->ibat_ma;
        return (uint16_t)((uint32_t)L->vpack_mv * (uint32_t)ma / 10000u);  // 10 mW per bit
      }
      case INA_MASK:    return R.ina_mask;
      case INA_ALERT:   return R.ina_alert;
      case INA_MFR:     return 0x5449;
      case INA_DIE:     return 0x2270;
      default:          return 0;
    }
  }
  switch (reg) {
    case R_ID:        return 0x534D;
    case R_VERSION:   return (FW_VERSION_MAJOR << 8) | FW_VERSION_MINOR;
    case R_STATUS:    return L->status;
    case R_SOC:       return L->soc_x100;
    case R_REMAIN:    return L->remaining_mah;
    case R_VPACK:     return L->vpack_mv;
    case R_VCELL1:    return L->vcell1_mv;
    case R_VCELL2:    return L->vcell2_mv;
    case R_IBAT:      return (uint16_t)L->ibat_ma;
    case R_VBUS:      return L->vbus_mv;
    case R_IBUS:      return (uint16_t)L->ibus_ma;
    case R_TEMP:      return (uint16_t)L->temp_x10;
    case R_CHEM:      return (uint16_t)(L->chem << 8 | L->chem_jumper);
    case R_BQ_STAT:   return L->bq_chg_stat;
    case R_BQ_FAULT:  return (uint16_t)(L->bq_fault0 << 8 | L->bq_fault1);
    case R_CAP_LEARNT: return R.gauge.capacity_learnt_mah;
    case R_CYCLES:    return R.gauge.cycles_x10;
    case R_CHEM_SET:  return R.set.chem;
    case R_MPPT:      return R.set.mppt_code;
    case R_NTC:       return R.set.ntc_mode;
    case R_INA_SIGN:  return R.set.ina_sign;
    case R_EXPERT_N:  return R.set.expert_n;
    case R_UNLOCK:    return (int32_t)(R.unlock_until_ms - R.now_ms) > 0 ? 1 : 0;
    case R_CMD:       return R.pending_cmd;
    case R_RESULT:    return R.result;
    default: {
      uint16_t *f = set_field(&R.set, reg);
      if (f) return *f;
      if (reg >= R_BQ_FIRST && reg <= R_BQ_LAST) return R.bq[reg - R_BQ_FIRST];
      return 0;
    }
  }
}

static bool protected_cmd(uint8_t c) {
  return c == CMD_SAVE || c == CMD_DEFAULTS || c == CMD_ZERO || c == CMD_SHIP || c == CMD_BSL ||
         c == CMD_REBOOT;
}

void regs_write(uint8_t addr7, uint8_t reg, uint16_t v) {
  if (addr7 == ADDR_INA260) {
    // only the writable INA260 registers; they change nothing here, but a driver reads them back
    if (reg == INA_CONFIG) R.ina_config = (v & 0x8000) ? INA_CONFIG_DEFAULT : v;
    else if (reg == INA_MASK) R.ina_mask = v;
    else if (reg == INA_ALERT) R.ina_alert = v;
    return;
  }
  if (reg == R_UNLOCK) {
    R.unlock_until_ms = v == UNLOCK_KEY ? R.now_ms + 2000 : R.now_ms;
    return;
  }
  if (reg == R_CMD) {
    if (R.pending_cmd != CMD_NONE) { R.result = ERR_BUSY; return; }
    if (v == CMD_NONE || v > CMD_REBOOT) { R.result = ERR_UNKNOWN_CMD; return; }
    if (protected_cmd((uint8_t)v)) {
      if ((int32_t)(R.unlock_until_ms - R.now_ms) <= 0) { R.result = ERR_LOCKED; return; }
      R.unlock_until_ms = R.now_ms;   // one command per unlock
    }
    R.result = 0xFFFF;
    R.pending_cmd = (uint8_t)v;
    return;
  }
  if (reg < R_SET_FIRST || reg > R_SET_LAST) { R.result = ERR_BAD_REG; return; }

  // a settings field: change a copy, check it, take it
  settings_t tmp = R.set;
  uint8_t chem = R.live.chem;
  switch (reg) {
    case R_CHEM_SET: {
      if (v != CHEM_BY_JUMPER && v >= CHEM_COUNT) { R.result = ERR_RANGE; return; }
      uint8_t newc = v == CHEM_BY_JUMPER ? R.live.chem_jumper : (uint8_t)v;
      if (newc != chem) {
        // other chemistry: its own voltage limits (keeps currents, capacity, calibration)
        settings_t d;
        settings_defaults(&d, newc);
        tmp.vchg_mv = d.vchg_mv; tmp.od_off_mv = d.od_off_mv; tmp.od_on_mv = d.od_on_mv;
        tmp.ship_mv = d.ship_mv; tmp.j7_off_mv = d.j7_off_mv; tmp.j7_on_mv = d.j7_on_mv;
        chem = newc;
      }
      tmp.chem = (uint8_t)v;
      break;
    }
    case R_MPPT:     tmp.mppt_code = (uint8_t)v; break;
    case R_NTC:      tmp.ntc_mode = (uint8_t)v; break;
    case R_INA_SIGN: tmp.ina_sign = (uint8_t)v; break;
    case R_EXPERT_N: tmp.expert_n = (uint8_t)v; break;
    default: {
      uint16_t *f = set_field(&tmp, reg);
      if (!f) { R.result = ERR_BAD_REG; return; }
      *f = v;
    }
  }
  if (v > 0xFF && (reg == R_MPPT || reg == R_NTC || reg == R_INA_SIGN || reg == R_EXPERT_N)) {
    R.result = ERR_RANGE;
    return;
  }
  int err = settings_check(&tmp, chem);
  if (err) { R.result = (uint16_t)err; return; }
  R.set = tmp;
  R.settings_changed = true;
  R.result = ERR_OK;
}

// ---------------- framing ----------------
// The I2C ISR calls these. A read may be prefetched by one byte (the TX FIFO holds the next byte
// before the host asks for it), so the pointer is set from the bytes really sent at the end.

static uint8_t cur_addr, ptr_reg, read_base;
static bool have_ptr, msb_pending, reading;
static uint8_t msb;
static uint16_t tx_word;
static uint16_t tx_count;

void i2cproto_start(uint8_t addr7, bool read) {
  cur_addr = addr7;
  reading = read;
  tx_count = 0;
  if (read) read_base = ptr_reg;
  else { have_ptr = false; msb_pending = false; }
}

void i2cproto_rx(uint8_t b) {
  if (!have_ptr) { ptr_reg = b; have_ptr = true; msb_pending = false; return; }
  if (!msb_pending) { msb = b; msb_pending = true; return; }
  regs_write(cur_addr, ptr_reg, (uint16_t)(msb << 8 | b));
  msb_pending = false;
  if (cur_addr == ADDR_SMPPT) ptr_reg++;
}

uint8_t i2cproto_tx(void) {
  uint8_t reg = cur_addr == ADDR_SMPPT ? (uint8_t)(read_base + tx_count / 2) : read_base;
  uint8_t b;
  if ((tx_count & 1) == 0) { tx_word = regs_read(cur_addr, reg); b = (uint8_t)(tx_word >> 8); }
  else b = (uint8_t)tx_word;
  tx_count++;
  return b;
}

void i2cproto_end(uint8_t unsent) {
  if (reading && cur_addr == ADDR_SMPPT) {
    uint16_t sent = tx_count >= unsent ? (uint16_t)(tx_count - unsent) : 0;
    ptr_reg = (uint8_t)(read_base + sent / 2);
  }
  msb_pending = false;
  reading = false;
  tx_count = 0;
}
