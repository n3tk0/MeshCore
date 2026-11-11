#include "BQ25798.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

BQ25798::BQ25798(BQ25798Bus& bus) : _bus(bus) {
  defaults(_s);
  memset(&_r, 0, sizeof(_r));
  memset(_status, 0, sizeof(_status));
  _found = _applied = _ce_on = _adc_pending = _full = _shutdown_sent = _low_timing = _configured = _adc_continuous = false;
  _mppt_reg = 0;
  _prev_state = 0;
  _next_poll = _adc_at = _low_since = 0;
  _reconfigs = 0;
  _errors = 0;
  _polls = _adc_count = 0;
}

void BQ25798::defaults(BQ25798Settings& s) {
  memset(&s, 0, sizeof(s));
  s.version = BQ_SETTINGS_VERSION;
  s.chem = BQ_CHEM_LIION;
  s.ichg_ma = 1000;
  s.vindpm_mv = 0;    // MPPT
  s.ntc = 0;          // no NTC on J3: TS_IGNORE
  s.shipfet = 1;      // Q2 fitted, JP3 open (as JLC builds the board)
  s.cutoff_mv = defaultCutoff(s.chem);
}

bool BQ25798::valid(const BQ25798Settings& s) {
  if (s.version != BQ_SETTINGS_VERSION || s.chem > BQ_CHEM_LIFEPO4) return false;
  if (s.ichg_ma < BQ_ICHG_MIN || s.ichg_ma > BQ_ICHG_MAX) return false;
  if (s.vindpm_mv != 0 && (s.vindpm_mv < BQ_VINDPM_MIN || s.vindpm_mv > BQ_VINDPM_MAX)) return false;
  if (s.ntc > 1 || s.shipfet > 1) return false;
  if (s.cutoff_mv != 0 && (s.cutoff_mv < BQ_CUTOFF_MIN || s.cutoff_mv > cutoffMax(s.chem))) return false;
  return true;
}

bool BQ25798::read(uint8_t reg, uint8_t* buf, uint8_t n) {
  if (_bus.readRegs(reg, buf, n)) return true;
  _errors++;
  return false;
}

bool BQ25798::write8(uint8_t reg, uint8_t v) {
  if (_bus.writeRegs(reg, &v, 1)) return true;
  _errors++;
  return false;
}

bool BQ25798::write16(uint8_t reg, uint16_t v) {
  uint8_t b[2] = { (uint8_t)(v >> 8), (uint8_t)v };   // MSB first, at the lower address
  if (_bus.writeRegs(reg, b, 2)) return true;
  _errors++;
  return false;
}

void BQ25798::setCE(bool on) {
  _ce_on = on;
  _bus.setChargeEnable(on);
}

bool BQ25798::begin(uint32_t now) {
  uint8_t part;
  _found = false;
  if (!read(BQ_REG_PART, &part, 1) || ((part >> 3) & 0x07) != 3) return false;   // PN 011b = BQ25798
  _found = true;
  // a new start (also when it comes back after I2C errors): nothing carried over from before
  _low_timing = _adc_pending = _shutdown_sent = _adc_continuous = false;
  _r.valid = false;
  // the watchdog off at once: its 40 s would otherwise run out while the node boots
  uint8_t c1;
  if (read(BQ_REG_CTRL1, &c1, 1) && (c1 & 0x3F) != 0) write8(BQ_REG_CTRL1, c1 & 0xC0);   // VAC_OVP 26 V
  _next_poll = now;
  return true;
}

void BQ25798::apply(const BQ25798Settings& s) {
  if (!valid(s)) return;
  bool vreg_changes = !_applied || s.chem != _s.chem;
  _s = s;
  _applied = true;
  if (!_found) return;
  if (vreg_changes) setCE(false);   // nothing charges until the new VREG reads back
  uint8_t st[7];
  if (read(BQ_REG_STAT0, st, sizeof(st))) memcpy(_status, st, sizeof(st));
  enforce();
}

void BQ25798::enforce() {
  uint8_t c[BQ_CONFIG_LEN];
  if (!read(BQ_REG_VSYSMIN, c, sizeof(c))) return;

  // the watchdog is back on: a power-on reset (or REG_RST) since the last look
  bool reset_seen = (c[BQ_REG_CTRL1] & 0x07) != 0;
  if (reset_seen) {
    if (_configured) _reconfigs++;
    write8(BQ_REG_CTRL1, c[BQ_REG_CTRL1] & 0xC0);   // watchdog off, VAC_OVP 26 V, backup bits as they are
  }

  uint16_t want_vreg = vregFor(_s.chem) / 10;
  uint16_t vreg = ((c[BQ_REG_VREG] << 8) | c[BQ_REG_VREG + 1]) & 0x7FF;
  if (vreg != want_vreg) {
    setCE(false);
    if (write16(BQ_REG_VREG, want_vreg)) {
      uint8_t b[2];
      if (read(BQ_REG_VREG, b, 2)) vreg = ((b[0] << 8) | b[1]) & 0x7FF;
    }
  }
  if (vreg == want_vreg && !_ce_on) setCE(true);

  // VSYSMIN under a LiFePO4 cell's plateau: MPPT does not run while VBAT < VSYSMIN (power-on: 3.5 V)
  uint8_t want_sys = (uint8_t)((vsysminFor(_s.chem) - 2500) / 250);
  if ((c[BQ_REG_VSYSMIN] & 0x3F) != want_sys && write8(BQ_REG_VSYSMIN, want_sys)) {
    read(BQ_REG_STAT3, &_status[BQ_REG_STAT3 - BQ_REG_STAT0], 1);   // VSYS_STAT follows at once (MPPT below)
  }

  uint16_t want_ichg = _s.ichg_ma / 10;
  if ((((c[BQ_REG_ICHG] << 8) | c[BQ_REG_ICHG + 1]) & 0x1FF) != want_ichg) write16(BQ_REG_ICHG, want_ichg);

  // D+/D- are not connected: no USB detection, which could set the input limit to 500 mA
  if (c[BQ_REG_CTRL2] & 0x40) write8(BQ_REG_CTRL2, c[BQ_REG_CTRL2] & ~0x40);
  if ((((c[BQ_REG_IINDPM] << 8) | c[BQ_REG_IINDPM + 1]) & 0x1FF) != BQ_IINDPM_MA / 10) {
    write16(BQ_REG_IINDPM, BQ_IINDPM_MA / 10);
  }

  // EN_CHG on, EN_ICO off (only one of ICO / VINDPM detection / MPPT may run)
  uint8_t c0 = (c[BQ_REG_CTRL0] & ~0x10) | 0x20;
  if (c0 != c[BQ_REG_CTRL0]) write8(BQ_REG_CTRL0, c0);

  // SFET_PRESENT as the board is built; EN_IBAT: the discharge current is measured on battery alone too
  uint8_t c5 = (c[BQ_REG_CTRL5] & ~0xA0) | (_s.shipfet ? 0x80 : 0) | 0x20;
  if (c5 != c[BQ_REG_CTRL5]) write8(BQ_REG_CTRL5, c5);

  uint8_t m = c[BQ_REG_MPPT];
  if (_s.vindpm_mv == 0) {
    // the chip clears EN_MPPT when the panel goes (every evening) and refuses it while VBAT < VSYSMIN
    bool can = vbusPresent() && powerGood() && !(_status[BQ_REG_STAT3 - BQ_REG_STAT0] & 0x10);
    if ((m & 0xFE) != BQ_MPPT_BASE || (can && !(m & 1))) {
      uint8_t v = BQ_MPPT_BASE | (can ? 1 : (m & 1));
      if (write8(BQ_REG_MPPT, v)) m = v;
      if (can) read(BQ_REG_MPPT, &m, 1);   // what the chip took (not taken: tried again in 10 s)
    }
  } else {
    if (m != BQ_MPPT_BASE && write8(BQ_REG_MPPT, BQ_MPPT_BASE)) m = BQ_MPPT_BASE;
    uint8_t want = (uint8_t)(_s.vindpm_mv / 100);
    if (vbusPresent() && c[BQ_REG_VINDPM] != want) write8(BQ_REG_VINDPM, want);   // set again at each plug-in
  }
  _mppt_reg = m;

  uint8_t n1 = (c[BQ_REG_NTC1] & ~0x01) | (_s.ntc ? 0 : 1);
  if (n1 != c[BQ_REG_NTC1]) write8(BQ_REG_NTC1, n1);

  _configured = true;
}

// the results: false when they read as zeros (SYS feeds this MCU, so it is never 0 V while we run)
bool BQ25798::readAdc(uint32_t now) {
  uint8_t d[18];
  if (!read(BQ_REG_IBUS_ADC, d, sizeof(d))) return true;   // an I2C error, not zeros
  uint16_t vsys = (uint16_t)((d[12] << 8) | d[13]);
  if (vsys == 0) return false;
  _r.ibus_ma = (int16_t)((d[0] << 8) | d[1]);
  _r.ibat_ma = (int16_t)((d[2] << 8) | d[3]);
  _r.vbus_mv = (uint16_t)((d[4] << 8) | d[5]);
  _r.vbat_mv = (uint16_t)((d[10] << 8) | d[11]);
  _r.vsys_mv = vsys;
  _r.tdie_dc = (int16_t)((d[16] << 8) | d[17]) * 5;   // 0.5 °C a step
  _r.valid = true;
  _adc_count++;
  _adc_at = now;
  return true;
}

// One shot every 10 s: the chip clears ADC_EN when it is done (datasheet 7.3.10) and the results stay.
// Should a chip read zeros once ADC_EN drops, it goes to continuous conversion (about 1 mA more).
void BQ25798::adc(uint32_t now) {
  uint8_t a[3];
  if (!read(BQ_REG_ADC_CTRL, a, sizeof(a))) return;
  if (a[1] != BQ_ADC_DIS0_VAL) write8(BQ_REG_ADC_DIS0, BQ_ADC_DIS0_VAL);
  if (a[2] != BQ_ADC_DIS1_VAL) write8(BQ_REG_ADC_DIS1, BQ_ADC_DIS1_VAL);
  if (_adc_continuous) {
    if ((a[0] & 0xC0) == 0x80) readAdc(now);           // running: the latest values
    else write8(BQ_REG_ADC_CTRL, BQ_ADC_CONTINUOUS);   // off again (a reset): back on
    _adc_pending = true;                                // stale counts from the last good result
    return;
  }
  if (a[0] & 0x80) return;   // still converting (or postponed: battery under 2.9 V, no sun)
  if (_adc_pending && !readAdc(now)) {
    _adc_continuous = true;
    if (write8(BQ_REG_ADC_CTRL, BQ_ADC_CONTINUOUS) && !_r.valid) _adc_at = now;
    return;
  }
  _adc_pending = false;
  if (write8(BQ_REG_ADC_CTRL, BQ_ADC_START)) {
    _adc_pending = true;
    if (!_r.valid) _adc_at = now;   // stale is counted from the first start
  }
}

void BQ25798::lowBattery(uint32_t now) {
  if (vbusPresent()) _shutdown_sent = false;   // the chip ignores shutdown with the panel up
  if (!_applied || !_s.shipfet || _s.cutoff_mv == 0 || _shutdown_sent || vbusPresent() || !_r.valid) {
    _low_timing = false;
    return;
  }
  // an ADC that has not finished for a minute on battery alone: the cell is under 2.9 V
  bool stale = _adc_pending && (uint32_t)(now - _adc_at) >= BQ_ADC_STALE_MS;
  bool low = stale || (_r.vbat_mv > 1000 && _r.vbat_mv < _s.cutoff_mv);
  if (!low) {
    _low_timing = false;
    return;
  }
  if (!_low_timing) {
    _low_timing = true;
    _low_since = now;
    return;
  }
  if ((uint32_t)(now - _low_since) < BQ_LOW_BATT_MS) return;
  uint8_t c2;
  if (!read(BQ_REG_CTRL2, &c2, 1)) return;
  // SDRV_CTRL = shutdown, SDRV_DLY = 0: the ship FET opens in 10 s; only panel voltage wakes the chip
  if (write8(BQ_REG_CTRL2, (c2 & ~0x07) | (1 << 1))) _shutdown_sent = true;
}

void BQ25798::loop(uint32_t now) {
  if (!_found || (int32_t)(now - _next_poll) < 0) return;
  _next_poll = now + BQ_POLL_MS;
  _polls++;
  uint8_t st[7];
  if (!read(BQ_REG_STAT0, st, sizeof(st))) return;
  memcpy(_status, st, sizeof(st));
  if (_applied) enforce();
  adc(now);
  uint8_t state = chargeState();
  if (state == 7 && _prev_state != 7) _full = true;   // charge done: the cell is full
  _prev_state = state;
  lowBattery(now);
}

bool BQ25798::mpptOn() const {
  return _s.vindpm_mv == 0 && (_mppt_reg & 1);
}

int BQ25798::shipMode() {
  if (!_s.shipfet) return -2;
  uint8_t st;
  if (!read(BQ_REG_STAT0, &st, 1)) return -3;
  if (st & 0x01) return -1;
  uint8_t c2;
  if (!read(BQ_REG_CTRL2, &c2, 1)) return -3;
  return write8(BQ_REG_CTRL2, (c2 & ~0x07) | (2 << 1)) ? 0 : -3;   // ship mode in 10 s
}

const char* BQ25798::stateName(uint8_t st) {
  static const char* names[8] = { "idle", "trickle", "pre", "fast", "taper", "?", "topoff", "done" };
  return names[st & 7];
}

static int addf(char* buf, int len, int n, const char* fmt, ...) {
  if (n >= len - 1) return n;
  va_list ap;
  va_start(ap, fmt);
  int w = vsnprintf(buf + n, len - n, fmt, ap);
  va_end(ap);
  if (w < 0) return n;
  n += w;
  return n >= len ? len - 1 : n;
}

int BQ25798::formatStatus(char* buf, int len) const {
  int n = 0;
  buf[0] = 0;
  if (!_found) return addf(buf, len, 0, "no BQ25798 at 0x6B");
  if (_r.valid) {
    if (vbusPresent()) n = addf(buf, len, n, "sun %u.%02uV %dmA", _r.vbus_mv / 1000, _r.vbus_mv % 1000 / 10, _r.ibus_ma);
    else n = addf(buf, len, n, "no sun");
    n = addf(buf, len, n, ", batt %u.%03uV %+dmA", _r.vbat_mv / 1000, _r.vbat_mv % 1000, _r.ibat_ma);
  } else {
    n = addf(buf, len, n, "%s, batt ?", vbusPresent() ? "sun" : "no sun");
  }
  n = addf(buf, len, n, " %s", _ce_on ? stateName(chargeState()) : "CE-off");
  if (_s.vindpm_mv) n = addf(buf, len, n, ", vin %u.%uV", _s.vindpm_mv / 1000, _s.vindpm_mv % 1000 / 100);
  else n = addf(buf, len, n, ", mppt %s", mpptOn() ? "on" : "off");
  if (_r.valid) n = addf(buf, len, n, ", die %dC", _r.tdie_dc / 10);
  n = addf(buf, len, n, ", %s %u.%02uV %umA", _s.chem == BQ_CHEM_LIFEPO4 ? "lifepo4" : "liion",
           vregFor(_s.chem) / 1000, vregFor(_s.chem) % 1000 / 10, _s.ichg_ma);
  n = addf(buf, len, n, ", ntc %s, shipfet %s, cut %umV", _s.ntc ? "on" : "off", _s.shipfet ? "on" : "off", _s.cutoff_mv);
  if (_status[5] || (_status[6] & 0xF4)) n = addf(buf, len, n, ", FAULT %02X/%02X", _status[5], _status[6]);
  if (_reconfigs) n = addf(buf, len, n, ", reset x%u", _reconfigs);
  return n;
}
