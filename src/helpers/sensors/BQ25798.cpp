#include "BQ25798.h"

#include <math.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

BQ25798::BQ25798(BQ25798Bus& bus) : _bus(bus) {
  defaults(_s);
  memset(&_r, 0, sizeof(_r));
  memset(_status, 0, sizeof(_status));
  _jumper = -1;
  _jumper_read = false;
  _found = _applied = _ce_on = _adc_pending = _full = _shutdown_sent = _low_timing = _configured = _adc_continuous = false;
  _mppt_reg = 0;
  _prev_state = 0;
  _next_poll = _adc_at = _low_since = 0;
  _reconfigs = 0;
  _errors = 0;
  _polls = _adc_count = 0;
  _ts_in_shot = _ts_night_any = false;
  _ts_night_at = _tcell_at = 0;
  _r.tcell_dc = BQ_TCELL_NONE;
  _now = 0;
  _poorsrc_count = 0;
  _poorsrc_at = 0;
  _shot_unl = _unl_now = _unl_last = _voc_valid = _hiz_seen = false;
  _voc_mv = 0;
  _hiz_hold = 0;
  _vindpm_reg = 0;
  _auto_vin_mv = _auto_vin_count = 0;
  _sc_state = _sc_idx = _sc_tries = 0;
  _sc_lo = _sc_table_lo = 0;
  _sc_forced = _sc_hiz_set = _sc_mppt_was = false;
  _sc_at = 0;
  _sc_voc = 0;
  memset(_sc_p, 0, sizeof(_sc_p));
  memset(_sc_table, 0, sizeof(_sc_table));
  _sc_okcount = 0;
  _sc_cond = false;
  _sc_cond_at = _sc_not_before = 0;
  _sc_first = true;
  _sc_have = _sc_ran = false;
  _sc_fail = 0;
  _sc_done_at = _sc_ok_at = 0;
  _sc_best_x10 = 0;
  _sc_best_mw = _sc_81_mw = 0;
  _sc_chosen = 0;
  _ref_ok = false;
  _ref_ma = 0;
  _ref_at = 0;
  memset(&_cal, 0, sizeof(_cal));
  IbatCal::buildDefault(_def);
}

void BQ25798::defaults(BQ25798Settings& s) {
  memset(&s, 0, sizeof(s));
  s.version = BQ_SETTINGS_VERSION;
  s.chem = BQ_CHEM_LIION;
  s.ichg_ma = 1000;
  s.vindpm_mv = 0;    // MPPT
  s.voc_pct = BQ_VOC_PCT_DEFAULT;
  s.mppt_auto = 0;
  s.ntc = 0;          // no NTC on J3: TS_IGNORE
  s.shipfet = 1;      // Q2 fitted, JP3 open (as JLC builds the board)
  s.cutoff_mv = defaultCutoff(s.chem);
}

bool BQ25798::valid(const BQ25798Settings& s) {
  if (s.version != BQ_SETTINGS_VERSION || s.chem > BQ_CHEM_LIFEPO4) return false;
  if (s.ichg_ma < BQ_ICHG_MIN || s.ichg_ma > BQ_ICHG_MAX) return false;
  if (s.vindpm_mv != 0 && (s.vindpm_mv < BQ_VINDPM_MIN || s.vindpm_mv > BQ_VINDPM_MAX)) return false;
  if (vocCode(s.voc_pct) < 0 || s.mppt_auto > 1) return false;
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

// REG1B..REG22 in one transfer. Reading REG22 clears its flags, so a poor source is counted here,
// for every reader (loop() and apply()), and kept for the status line.
bool BQ25798::readStatus() {
  uint8_t st[BQ_STATUS_LEN];
  if (!read(BQ_REG_STAT0, st, sizeof(st))) return false;
  memcpy(_status, st, sizeof(st));
  if (st[BQ_REG_FLAG0 - BQ_REG_STAT0] & 0x10) {
    if (_poorsrc_count < 0xFFFF) _poorsrc_count++;
    _poorsrc_at = _now;
  }
  return true;
}

int BQ25798::vocCode(int pct) {
  return pct == 63 ? 1 : pct == 69 ? 2 : pct == 75 ? 3 : pct == 81 ? 4 : pct == 88 ? 5 : -1;
}

uint8_t BQ25798::mpptBase(uint8_t voc_pct) {
  int code = vocCode(voc_pct);
  return (uint8_t)((code < 0 ? 4 : code) << 5) | BQ_MPPT_DLYRATE;
}

bool BQ25798::fromFile(const void* data, int len, BQ25798Settings& s) {
  const int OLD_LEN = (int)offsetof(BQ25798Settings, voc_pct);   // the file before voc_pct
  if (len < OLD_LEN) return false;
  defaults(s);
  memcpy(&s, data, len < (int)sizeof(s) ? len : (int)sizeof(s));
  // fields past the end of an older file: their defaults
  if (len <= (int)offsetof(BQ25798Settings, voc_pct)) s.voc_pct = BQ_VOC_PCT_DEFAULT;
  if (len <= (int)offsetof(BQ25798Settings, mppt_auto)) s.mppt_auto = 0;
  // the calibration is all or nothing: a file cut short inside it has none
  if (len < (int)(offsetof(BQ25798Settings, cal) + sizeof(IbatLegacyCal))) memset(&s.cal, 0, sizeof(s.cal));
  if (s.voc_pct == 0) s.voc_pct = BQ_VOC_PCT_DEFAULT;
  if (valid(s)) return true;
  defaults(s);
  return false;
}

uint16_t BQ25798::vindpmFromVoc(uint16_t voc_mv, uint8_t voc_pct) {
  // exact percentages in 0.01 %: 62.5, 68.75, 75, 81.25, 87.5
  uint32_t p = voc_pct == 63 ? 6250 : voc_pct == 69 ? 6875 : voc_pct == 75 ? 7500 : voc_pct == 88 ? 8750 : 8125;
  uint32_t mv = (uint32_t)voc_mv * p / 10000 / 100 * 100;
  return (uint16_t)(mv < BQ_VINDPM_MIN ? BQ_VINDPM_MIN : mv > BQ_VINDPM_MAX ? BQ_VINDPM_MAX : mv);
}

bool BQ25798::begin(uint32_t now) {
  uint8_t part;
  _found = false;
  if (!read(BQ_REG_PART, &part, 1) || ((part >> 3) & 0x07) != 3) return false;   // PN 011b = BQ25798
  _found = true;
  if (!_jumper_read) {   // the first start only: the pin is not driven yet
    _jumper = _bus.readCeJumper();
    _jumper_read = true;
  }
  // a new start (also when it comes back after I2C errors): nothing carried over from before
  _low_timing = _adc_pending = _shutdown_sent = _adc_continuous = false;
  _shot_unl = _unl_now = _unl_last = _voc_valid = false;
  _hiz_hold = 0;
  _sc_state = 0;   // a scan that was running: enforce() puts HIZ / EN_MPPT back
  _sc_cond = false;
  _sc_first = true;
  _now = now;
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
  if (_sc_state) scanFinish(_now, BQ_SCF_ABORT);   // the scan's restore uses the old settings: first
  _s = s;
  memset(&_s.cal, 0, sizeof(_s.cal));   // the calibration lives in _cal
  _applied = true;
  if (!_found) return;
  if (vreg_changes) setCE(false);   // nothing charges until the new VREG reads back
  readStatus();
  enforce();
}

// The VBUS ADC result is the panel's open-circuit voltage only if the converter was not loading the
// panel when it was converted: a one-shot that was started while it was off (_shot_unl), or continuous
// conversion with the converter off at the last poll as well (_unl_last: EN_HIZ was kept, not cleared).
void BQ25798::harvestVoc(bool unloaded, bool mppt_running) {
  _voc_valid = false;
  if (!unloaded || mppt_running || _s.vindpm_mv != 0) return;
  uint8_t a;
  if (!read(BQ_REG_ADC_CTRL, &a, 1)) return;
  bool fresh;
  if (_adc_continuous) fresh = _unl_last && (a & 0xC0) == 0x80;
  else fresh = _adc_pending && _shot_unl && !(a & 0x80);   // started without load and done
  uint8_t b[2];
  if (!fresh || !read(BQ_REG_VBUS_ADC, b, 2)) return;
  uint16_t mv = (uint16_t)((b[0] << 8) | b[1]);
  if (mv < BQ_VOC_MIN_MV) return;   // zeros: the channel is off or the result was cleared
  _voc_mv = mv;
  _voc_valid = true;
}

void BQ25798::enforce() {
  _unl_last = _unl_now;
  _unl_now = false;
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
  if (chargeHeld() && _ce_on) setCE(false);
  if (vreg != want_vreg) {
    setCE(false);
    if (write16(BQ_REG_VREG, want_vreg)) {
      uint8_t b[2];
      if (read(BQ_REG_VREG, b, 2)) vreg = ((b[0] << 8) | b[1]) & 0x7FF;
    }
  }

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

  // MPPT mode with the panel there but not loaded (EN_HIZ, no PG, poor source) and MPPT not running:
  // VBUS is the open-circuit voltage. The chip's own VINDPM (VBUS0 - 0.7 V, 3.6 V at the clamp) pulls a
  // weak panel under VPOORSRC again and again, so set the VOC_PCT share (81 % by default) of Voc before EN_HIZ is cleared below.
  bool hiz = c[BQ_REG_CTRL0] & 0x04;
  bool mppt_mode = _s.vindpm_mv == 0;
  // EN_MPPT alone does not mean MPPT runs: the chip clears it only when VBUS goes under VBUS_PRESENT, so a
  // poor-source HIZ with VBUS still up leaves it set. Running = EN_MPPT with PG and no EN_HIZ. A latched
  // EN_MPPT with HIZ / no PG takes the recovery below: VINDPM, then EN_HIZ. EN_MPPT is left set: the
  // datasheet (7.3.6.3) does not block VINDPM writes while it is on, the chip's own Voc cycle only rewrites
  // VINDPM every VOC_RATE, and once PG = 1 it is simply running again.
  bool mppt_running = (c[BQ_REG_MPPT] & 1) && powerGood() && !hiz;
  bool unloaded = vbusPresent() && (hiz || !powerGood());   // the latched poor-source flag alone is not "off"
  _hiz_seen = hiz;
  _vindpm_reg = c[BQ_REG_VINDPM];
  bool hold_hiz = false;
  if (mppt_mode && !mppt_running && unloaded) {
    harvestVoc(unloaded, mppt_running);
    bool set = false;
    if (_voc_valid) {
      uint16_t vin = vindpmFromVoc(_voc_mv, _s.voc_pct);
      uint8_t reg = (uint8_t)(vin / 100);
      if (reg == _vindpm_reg || write8(BQ_REG_VINDPM, reg)) {
        if (reg != _vindpm_reg) _auto_vin_count++;
        _vindpm_reg = reg;
        _auto_vin_mv = vin;
        _voc_valid = false;
        _hiz_hold = 0;
        set = true;
      }
    }
    if (!set && hiz) {
      // no no-load VBUS result yet: stay in HIZ for the one-shot of this poll. Only BQ_HIZ_HOLD_POLLS
      // of every BQ_HIZ_HOLD_PERIOD polls, so an ADC that gives nothing costs a retry now and then,
      // not the 10 s retry of the chip's poor source qualification (7 min by itself)
      if (_hiz_hold >= BQ_HIZ_HOLD_PERIOD) _hiz_hold = 0;
      hold_hiz = _hiz_hold < BQ_HIZ_HOLD_POLLS;
      _hiz_hold++;
    }
  } else {
    _voc_valid = false;
    _hiz_hold = 0;
  }

  // EN_CHG on, EN_ICO and EN_HIZ off (only one of ICO / VINDPM detection / MPPT may run)
  uint8_t c0 = (c[BQ_REG_CTRL0] & ~0x14) | 0x20 | (hold_hiz ? 0x04 : 0);
  if (c0 != c[BQ_REG_CTRL0]) write8(BQ_REG_CTRL0, c0);
  _unl_now = unloaded && !(hiz && !hold_hiz);   // the converter restarts when EN_HIZ goes: not a no-load poll

  // SFET_PRESENT as the board is built; EN_IBAT: the discharge current is measured on battery alone too
  uint8_t c5 = (c[BQ_REG_CTRL5] & ~0xA0) | (_s.shipfet ? 0x80 : 0) | 0x20;
  if (c5 != c[BQ_REG_CTRL5]) write8(BQ_REG_CTRL5, c5);

  uint8_t m = c[BQ_REG_MPPT];
  if (_s.vindpm_mv == 0) {
    // the chip clears EN_MPPT when the panel goes (every evening) and refuses it while VBAT < VSYSMIN
    bool can = vbusPresent() && powerGood() && !(_status[BQ_REG_STAT3 - BQ_REG_STAT0] & 0x10);
    uint8_t base = mpptBase(_s.voc_pct);
    if ((m & 0xFE) != base || (can && !(m & 1))) {
      uint8_t v = base | (can ? 1 : (m & 1));
      if (write8(BQ_REG_MPPT, v)) m = v;
      if (can) read(BQ_REG_MPPT, &m, 1);   // what the chip took (not taken: tried again in 10 s)
    }
  } else {
    uint8_t base = mpptBase(_s.voc_pct);
    if (m != base && write8(BQ_REG_MPPT, base)) m = base;
    uint8_t want = (uint8_t)(_s.vindpm_mv / 100);
    if (vbusPresent() && c[BQ_REG_VINDPM] != want) write8(BQ_REG_VINDPM, want);   // set again at each plug-in
  }
  _mppt_reg = m;

  uint8_t n1 = (c[BQ_REG_NTC1] & ~0x01) | (_s.ntc ? 0 : 1);
  if (n1 != c[BQ_REG_NTC1]) write8(BQ_REG_NTC1, n1);

  if (!chargeHeld() && vreg == want_vreg && !_ce_on) setCE(true);

  _configured = true;
}

// the results: false when they read as zeros (SYS feeds this MCU, so it is never 0 V while we run)
bool BQ25798::readAdc(uint32_t now) {
  uint8_t d[18];
  if (!read(BQ_REG_IBUS_ADC, d, sizeof(d))) return true;   // an I2C error, not zeros
  uint16_t vsys = (uint16_t)((d[12] << 8) | d[13]);
  if (vsys == 0) return false;
  _r.ibus_ma = (int16_t)((d[0] << 8) | d[1]);
  _r.ibat_raw_ma = (int16_t)((d[2] << 8) | d[3]);
  _r.ibat_ma = IbatCal::apply(_cal, _def, _r.ibat_raw_ma);
  _r.vbus_mv = (uint16_t)((d[4] << 8) | d[5]);
  _r.vbat_mv = (uint16_t)((d[10] << 8) | d[11]);
  _r.vsys_mv = vsys;
  _r.tdie_dc = (int16_t)((d[16] << 8) | d[17]) * 5;   // 0.5 °C a step
  if (_ts_in_shot) {
    _r.ts_pm = (uint16_t)((((uint32_t)((d[14] << 8) | d[15])) * 1000 + 512) / 1024);   // 0.0976563 % a step
    _r.tcell_dc = ntcTemp(_r.ts_pm);
    _tcell_at = now;
  } else if (!_s.ntc || (uint32_t)(now - _tcell_at) >= BQ_TCELL_KEEP_MS) {
    _r.ts_pm = 0;
    _r.tcell_dc = BQ_TCELL_NONE;   // not measured lately: no old value
  }
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
  // TS (the NTC) with sun; on battery alone it keeps REGN on and stops the ADC under 3.2 V, so then
  // only one conversion every 10 min, above 3.3 V (a hung one is started again without TS below)
  bool night_ts = _s.ntc && !vbusPresent() && !_adc_continuous && !(a[0] & 0x80) && _r.valid
                  && _r.vbat_mv > BQ_TS_NIGHT_MIN_MV
                  && (!_ts_night_any || (uint32_t)(now - _ts_night_at) >= BQ_TS_NIGHT_MS);
  uint8_t want_dis0 = (_s.ntc && vbusPresent() && !_hiz_seen) || night_ts ? BQ_ADC_DIS0_TS : BQ_ADC_DIS0_VAL;
  if (a[1] != want_dis0 && !write8(BQ_REG_ADC_DIS0, want_dis0)) want_dis0 = a[1];
  if (a[2] != BQ_ADC_DIS1_VAL) write8(BQ_REG_ADC_DIS1, BQ_ADC_DIS1_VAL);
  if (_adc_continuous) {
    _ts_in_shot = (a[1] & 0x04) == 0 && (want_dis0 & 0x04) == 0;   // on since the last look too
    if ((a[0] & 0xC0) == 0x80) readAdc(now);           // running: the latest values
    else write8(BQ_REG_ADC_CTRL, BQ_ADC_CONTINUOUS);   // off again (a reset): back on
    _adc_pending = true;                                // stale counts from the last good result
    return;
  }
  if ((a[0] & 0x80) && _ts_in_shot && (want_dis0 & 0x04)) {
    // the sun went during a conversion with TS on: on battery under 3.2 V it would never end, so start
    // again without TS (the stale timer keeps counting from the old start)
    if (write8(BQ_REG_ADC_CTRL, BQ_ADC_START)) {
      _ts_in_shot = false;
      _shot_unl = false;
    }
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
    _shot_unl = _unl_now;   // enforce() of this poll ran first: no load, EN_HIZ still set
    _ts_in_shot = (want_dis0 & 0x04) == 0;
    if (night_ts && _ts_in_shot) {
      _ts_night_any = true;
      _ts_night_at = now;
    }
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
  if (!_found) return;
  if (_sc_state && (int32_t)(now - _sc_at) >= 0) {
    _now = now;
    scanTick(now);
  }
  if (_sc_state) return;   // the 10 s poll waits for the scan (~30 s)
  if ((int32_t)(now - _next_poll) < 0) return;
  _next_poll = now + BQ_POLL_MS;
  _polls++;
  _now = now;
  if (!readStatus()) return;
  if (_applied) enforce();
  else { _unl_last = _unl_now; _unl_now = false; }
  adc(now);
  uint8_t state = chargeState();
  if (state == 7 && _prev_state != 7) _full = true;   // charge done: the cell is full
  _prev_state = state;
  lowBattery(now);
  if (_applied) scanSchedule(now);
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

int16_t BQ25798::ntcTemp(uint16_t ts_pm) {
  if (ts_pm > 840 || ts_pm < 50) return BQ_TCELL_NONE;
  float p = ts_pm / 1000.0f;
  float low = BQ_NTC_R_TOP * p / (1.0f - p);            // R_BOTTOM || NTC
  if (low >= BQ_NTC_R_BOTTOM) return BQ_TCELL_NONE;
  float ntc = 1.0f / (1.0f / low - 1.0f / BQ_NTC_R_BOTTOM);
  float k = 1.0f / (1.0f / 298.15f + logf(ntc / BQ_NTC_R25) / BQ_NTC_BETA);
  float c = (k - 273.15f) * 10.0f;
  if (c < -400 || c > 1000) return BQ_TCELL_NONE;
  return (int16_t)(c < 0 ? c - 0.5f : c + 0.5f);
}

// The line in full (trim 0); when it does not fit: without the settings that are in `charger` anyway
// (ntc, shipfet, cut-off: trim 1), then without the die temperature (trim 2), then without the cell
// temperature (trim 3). The reasons for no charging and the warnings (JP2, FAULT, reset) stay.
int BQ25798::formatStatus(char* buf, int len) const {
  int n = 0;
  for (uint8_t trim = 0; trim <= 3; trim++) {
    n = formatLine(buf, len, trim);
    if (n < len - 1) break;   // fits (n == len - 1 may have been cut)
  }
  return n;
}

int BQ25798::formatLine(char* buf, int len, uint8_t trim) const {
  int n = 0;
  buf[0] = 0;
  if (!_found) return addf(buf, len, 0, "no BQ25798 at 0x6B");
  if (_r.valid) {
    if (vbusPresent()) n = addf(buf, len, n, "sun %u.%02uV %dmA", _r.vbus_mv / 1000, _r.vbus_mv % 1000 / 10, _r.ibus_ma);
    else n = addf(buf, len, n, "no sun");
    int cur = batteryCurrentMa();   // the reference sensor's when there is one: the BQ's own then follows
    n = addf(buf, len, n, ", batt %u.%03uV %+dmA", _r.vbat_mv / 1000, _r.vbat_mv % 1000, cur);
    if (cur != _r.ibat_raw_ma && trim < 3) n = addf(buf, len, n, " (bq %+d)", _r.ibat_raw_ma);
  } else {
    n = addf(buf, len, n, "%s, batt ?", vbusPresent() ? "sun" : "no sun");
  }
  n = addf(buf, len, n, " %s", _ce_on ? stateName(chargeState()) : chargeHeld() ? "HELD: JP2 lifepo4" : "CE-off");
  if (_s.vindpm_mv) n = addf(buf, len, n, ", vin %u.%uV", _s.vindpm_mv / 1000, _s.vindpm_mv % 1000 / 100);
  else {
    if (_s.mppt_auto) n = addf(buf, len, n, ", mppt auto %u%%%s", _s.voc_pct, mpptOn() ? "" : " off");
    else {
      n = addf(buf, len, n, ", mppt %s", mpptOn() ? "on" : "off");
      if (_s.voc_pct != BQ_VOC_PCT_DEFAULT) n = addf(buf, len, n, " %u%%", _s.voc_pct);
    }
    if (_sc_state) n = addf(buf, len, n, ", scan %u/%u", (unsigned)(_sc_idx > BQ_SCAN_N ? BQ_SCAN_N : _sc_idx) + 1, BQ_SCAN_N + 1);
  }
  if (vbusPresent()) {
    // the panel is there: why it may not charge (MPPT off: the input limit the chip is holding)
    if (!_s.vindpm_mv && (!mpptOn() || _hiz_seen || !powerGood()) && _vindpm_reg) n = addf(buf, len, n, ", vin %u.%uV", (unsigned)_vindpm_reg / 10, (unsigned)_vindpm_reg % 10);
    if (_poorsrc_count && (uint32_t)(_now - _poorsrc_at) < BQ_POORSRC_KEEP_MS) {
      n = addf(buf, len, n, ", poor src x%u %um", (unsigned)(_poorsrc_count > 999 ? 999 : _poorsrc_count),
               (unsigned)((_now - _poorsrc_at) / 60000UL));
    }
    if (_hiz_seen) n = addf(buf, len, n, ", hiz");
    if (!powerGood()) n = addf(buf, len, n, ", no PG");
  }
  if (_r.valid && trim < 2) n = addf(buf, len, n, ", die %dC", _r.tdie_dc / 10);
  if (_r.valid && _r.tcell_dc != BQ_TCELL_NONE && trim < 3) n = addf(buf, len, n, ", cell %dC", _r.tcell_dc / 10);
  else if (_r.valid && ntcOpen() && trim < 3) n = addf(buf, len, n, ", NTC open?");
  n = addf(buf, len, n, ", %s %u.%02uV %umA", _s.chem == BQ_CHEM_LIFEPO4 ? "lifepo4" : "liion",
           vregFor(_s.chem) / 1000, vregFor(_s.chem) % 1000 / 10, _s.ichg_ma);
  if (trim < 1) n = addf(buf, len, n, ", ntc %s, shipfet %s, cut %umV", _s.ntc ? "on" : "off", _s.shipfet ? "on" : "off", _s.cutoff_mv);
  if (_jumper == 0 && _s.chem == BQ_CHEM_LIFEPO4) n = addf(buf, len, n, ", JP2 1-2!");
  if (_status[5] || (_status[6] & 0xF4)) n = addf(buf, len, n, ", FAULT %02X/%02X", _status[5], _status[6]);
  if (_reconfigs) n = addf(buf, len, n, ", reset x%u", _reconfigs);
  return n;
}

// ---- MPPT scan ----
//
// BEGIN: EN_MPPT off, EN_HIZ on (the panel unloaded) -> Voc conversion -> VINDPM = share0 x Voc, EN_HIZ off ->
// per share: settle, one-shot ADC, P = VBUS x IBUS -> the first share again -> compare, pick, restore.
// EN_HIZ / EN_MPPT / VINDPM are put back by scanFinish() on every way out; should its writes fail (I2C),
// the next 10 s poll's enforce() clears EN_HIZ and sets EN_MPPT again, and the chip's own Voc cycle
// rewrites VINDPM.

uint8_t BQ25798::nearestVocPct(uint16_t share_x10) {
  static const uint8_t pct[5] = { 63, 69, 75, 81, 88 };
  static const uint16_t x100[5] = { 6250, 6875, 7500, 8125, 8750 };
  int s = (int)share_x10 * 10, best = 0, bd = 100000;
  for (int i = 0; i < 5; i++) {
    int d = s > x100[i] ? s - x100[i] : x100[i] - s;
    if (d < bd) { bd = d; best = i; }
  }
  return pct[best];
}

// the panel there, loaded and held (PG), not in HIZ; for the automatic start also fast charge with MPPT on
bool BQ25798::scanConditions(bool need_fast) const {
  if (!_found || !_applied || _s.vindpm_mv != 0) return false;
  if (!vbusPresent() || !powerGood() || _hiz_seen || !_r.valid) return false;
  if (need_fast && (!_ce_on || chargeState() != 3 || !mpptOn())) return false;
  return true;
}

int BQ25798::scanStart() {
  if (!_found || !_applied || _s.vindpm_mv != 0) return -1;
  if (_sc_state) return -3;
  if (!vbusPresent() || !powerGood() || _hiz_seen) return -2;
  _sc_state = BQ_SC_BEGIN;
  _sc_forced = true;
  _sc_at = _now;   // at the next loop()
  _sc_tries = 0;
  _sc_idx = 0;
  return 0;
}

// at the end of each 10 s poll: start a scan in auto mode when it is time
void BQ25798::scanSchedule(uint32_t now) {
  if (!vbusPresent()) _sc_first = true;   // the next sun session scans again after ~2 min
  if (!scanConditions(true)) {
    _sc_cond = false;
    return;
  }
  if (!_sc_cond) { _sc_cond = true; _sc_cond_at = now; }
  if (!_s.mppt_auto || _sc_state) return;
  if ((uint32_t)(now - _sc_cond_at) < (_sc_first ? BQ_SCAN_FIRST_MS : BQ_SCAN_HOLD_MS)) return;
  if (!_sc_first && (int32_t)(now - _sc_not_before) < 0) return;
  _sc_state = BQ_SC_BEGIN;
  _sc_forced = false;
  _sc_at = now;
  _sc_tries = 0;
  _sc_idx = 0;
}

bool BQ25798::scanSetVindpm(unsigned idx) {
  uint32_t mv = (uint32_t)_sc_voc * scanShareX10(idx) / 1000 / 100 * 100;
  if (mv < BQ_VINDPM_MIN) mv = BQ_VINDPM_MIN;
  if (mv > BQ_VINDPM_MAX) mv = BQ_VINDPM_MAX;
  return write8(BQ_REG_VINDPM, (uint8_t)(mv / 100));
}

int BQ25798::scanAdc(uint32_t* mw, uint16_t* vbus) {
  uint8_t a;
  if (!read(BQ_REG_ADC_CTRL, &a, 1)) return -1;
  if (_adc_continuous ? (a & 0xC0) != 0x80 : (a & 0x80)) return 0;   // converting / not running
  uint8_t d[6];
  if (!read(BQ_REG_IBUS_ADC, d, sizeof(d))) return -1;
  int16_t ibus = (int16_t)((d[0] << 8) | d[1]);
  uint16_t v = (uint16_t)((d[4] << 8) | d[5]);
  if (v == 0) return -1;   // zeros: the channel is off
  *vbus = v;
  *mw = ibus > 0 ? (uint32_t)v * (uint32_t)ibus / 1000 : 0;
  return 1;
}

// the end, whatever the way: the result (fail = 0) and everything put back
void BQ25798::scanFinish(uint32_t now, uint8_t fail) {
  uint8_t st = _sc_state;
  _sc_state = BQ_SC_IDLE;
  _sc_ran = true;
  _sc_fail = fail;
  _sc_done_at = now;
  if (fail != BQ_SCF_NOCOND) {   // not started after all: the schedule stays as it was
    _sc_first = false;
    _sc_not_before = now + (fail == BQ_SCF_LOWV ? BQ_SCAN_PERIOD_MS : fail ? BQ_SCAN_RETRY_MS : BQ_SCAN_PERIOD_MS);
  }
  if (!fail) {
    unsigned best = _sc_lo;
    for (unsigned i = 0; i < BQ_SCAN_N; i++) {
      _sc_table[i] = i < _sc_lo ? 0 : _sc_p[i];
      if (i >= _sc_lo && _sc_p[i] > _sc_p[best]) best = i;
    }
    _sc_table_lo = _sc_lo;
    _sc_best_x10 = scanShareX10(best);
    _sc_best_mw = _sc_p[best];
    // 80.0 and 82.5 % (only the measured ones)
    _sc_81_mw = _sc_lo <= 7 ? (_sc_p[7] + _sc_p[8]) / 2 : _sc_lo == 8 ? _sc_p[8] : 0;
    _sc_have = true;
    _sc_ok_at = now;
    _sc_okcount++;
    if (_s.mppt_auto && _s.vindpm_mv == 0) {
      _s.voc_pct = nearestVocPct(_sc_best_x10);
      _sc_chosen = _s.voc_pct;
    }
  }
  _adc_pending = false;   // a conversion of ours is not one of adc()'s
  _ts_in_shot = false;
  if (st == BQ_SC_BEGIN || st == BQ_SC_IDLE) return;   // nothing changed on the chip yet
  // put back: VINDPM for the share in force, EN_HIZ off, EN_MPPT on (what it was)
  if (_sc_voc >= BQ_VOC_MIN_MV && _s.vindpm_mv == 0) {
    uint8_t reg = (uint8_t)(vindpmFromVoc(_sc_voc, _s.voc_pct) / 100);
    if (write8(BQ_REG_VINDPM, reg)) _vindpm_reg = reg;
  }
  if (_sc_hiz_set) {
    uint8_t c0;
    if (read(BQ_REG_CTRL0, &c0, 1) && write8(BQ_REG_CTRL0, c0 & ~0x04)) _sc_hiz_set = false;
  }
  uint8_t v = mpptBase(_s.voc_pct) | (_sc_mppt_was ? 1 : 0);
  if (write8(BQ_REG_MPPT, v)) {
    uint8_t m;
    _mppt_reg = read(BQ_REG_MPPT, &m, 1) ? m : v;
  }
  _unl_now = _unl_last = _voc_valid = false;
  _hiz_hold = 0;
  _hiz_seen = false;
  _next_poll = now + 1000;   // enforce() soon: it sees what is left of a failed restore
}

void BQ25798::scanTick(uint32_t now) {
  _sc_at = now + 100;
  uint8_t c[2];
  if (!readStatus() || !read(BQ_REG_CTRL0, c, sizeof(c))) { scanFinish(now, BQ_SCF_I2C); return; }
  bool hiz = c[0] & 0x04;
  if (c[1] & 0x07) { scanFinish(now, BQ_SCF_RESET); return; }   // watchdog bits back: a power-on reset
  uint8_t st = _sc_state;
  if (!vbusPresent()) { scanFinish(now, st == BQ_SC_BEGIN ? BQ_SCF_NOCOND : BQ_SCF_SUN); return; }
  if (st >= BQ_SC_STEP_ADC) {   // the converter loads the panel from here on
    if (!powerGood() || hiz) { scanFinish(now, BQ_SCF_SUN); return; }
    if (!_sc_forced && chargeState() != 3) { scanFinish(now, BQ_SCF_STATE); return; }
  }
  switch (st) {
  case BQ_SC_BEGIN: {
    uint8_t m;
    if (!read(BQ_REG_MPPT, &m, 1)) { scanFinish(now, BQ_SCF_I2C); return; }
    bool ok = powerGood() && !hiz && _ce_on && _s.vindpm_mv == 0 && (_sc_forced || (chargeState() == 3 && (m & 1)));
    if (!ok) { scanFinish(now, BQ_SCF_NOCOND); return; }
    _sc_mppt_was = m & 1;
    _sc_voc = 0;
    _sc_lo = 0;
    _sc_idx = 0;
    _sc_tries = 0;
    _sc_state = BQ_SC_VOC;   // from here on scanFinish() puts things back
    _sc_hiz_set = true;
    if (!write8(BQ_REG_MPPT, mpptBase(_s.voc_pct)) || !write8(BQ_REG_CTRL0, c[0] | 0x04)) {
      scanFinish(now, BQ_SCF_I2C);
      return;
    }
    _sc_at = now + BQ_SCAN_HIZ_MS;
    return;
  }
  case BQ_SC_VOC:
  case BQ_SC_STEP_ADC:
    if (!_adc_continuous && !write8(BQ_REG_ADC_CTRL, BQ_ADC_START)) { scanFinish(now, BQ_SCF_I2C); return; }
    _sc_state = st == BQ_SC_VOC ? BQ_SC_VOC_READ : BQ_SC_STEP_READ;
    _sc_tries = 0;
    _sc_at = now + BQ_SCAN_ADC_MS;
    return;
  case BQ_SC_VOC_READ: {
    uint32_t mw;
    uint16_t v;
    int r = scanAdc(&mw, &v);
    if (r < 0) { scanFinish(now, BQ_SCF_I2C); return; }
    if (r == 0) {
      if (++_sc_tries > 6) scanFinish(now, BQ_SCF_NOVOC);
      else _sc_at = now + BQ_SCAN_ADC_MS;
      return;
    }
    if (v < BQ_VOC_MIN_MV) { scanFinish(now, BQ_SCF_NOVOC); return; }
    _sc_voc = v;
    // steps whose VINDPM would be under BQ_SCAN_MIN_VINDPM (VPOORSRC region) are skipped
    _sc_lo = 0;
    while (_sc_lo < BQ_SCAN_N && (uint32_t)v * scanShareX10(_sc_lo) / 1000 / 100 * 100 < BQ_SCAN_MIN_VINDPM) _sc_lo++;
    if (BQ_SCAN_N - _sc_lo < BQ_SCAN_MIN_STEPS) { scanFinish(now, BQ_SCF_LOWV); return; }
    // VINDPM first, then EN_HIZ off (the weak panel would go under VPOORSRC at the chip's own VINDPM)
    if (!scanSetVindpm(_sc_lo) || !write8(BQ_REG_CTRL0, c[0] & ~0x04)) { scanFinish(now, BQ_SCF_I2C); return; }
    _sc_hiz_set = false;
    _sc_idx = _sc_lo;
    _sc_state = BQ_SC_STEP_ADC;
    _sc_at = now + BQ_SCAN_SETTLE_MS;
    return;
  }
  case BQ_SC_STEP_READ: {
    uint32_t mw;
    uint16_t v;
    int r = scanAdc(&mw, &v);
    if (r < 0) { scanFinish(now, BQ_SCF_I2C); return; }
    if (r == 0) {
      if (++_sc_tries > 6) scanFinish(now, BQ_SCF_I2C);
      else _sc_at = now + BQ_SCAN_ADC_MS;
      return;
    }
    _sc_p[_sc_idx++] = mw;   // [N] is the first share again
    if (_sc_idx <= BQ_SCAN_N) {
      if (!scanSetVindpm(_sc_idx < BQ_SCAN_N ? _sc_idx : _sc_lo)) { scanFinish(now, BQ_SCF_I2C); return; }
      _sc_state = BQ_SC_STEP_ADC;
      _sc_at = now + BQ_SCAN_SETTLE_MS;
      return;
    }
    uint32_t p0 = _sc_p[_sc_lo], p1 = _sc_p[BQ_SCAN_N];
    uint32_t hi = p0 > p1 ? p0 : p1, d = p0 > p1 ? p0 - p1 : p1 - p0;
    uint32_t best = 0;
    for (unsigned i = _sc_lo; i < BQ_SCAN_N; i++) if (_sc_p[i] > best) best = _sc_p[i];
    if (hi < BQ_SCAN_MIN_MW || best < BQ_SCAN_MIN_MW) scanFinish(now, BQ_SCF_DARK);
    else if (d * 100 > hi * BQ_SCAN_DRIFT_PCT) scanFinish(now, BQ_SCF_CLOUDS);
    else scanFinish(now, BQ_SCF_NONE);
    return;
  }
  default:
    scanFinish(now, BQ_SCF_ABORT);
    return;
  }
}

int BQ25798::formatScan(char* buf, int len) const {
  static const char* why[11] = { "", "aborted", "clouds", "sun lost", "left fast charge", "I2C", "no Voc",
                                 "too dark", "chip reset", "not in fast charge", "panel too low" };
  int n = 0;
  buf[0] = 0;
  if (_sc_state) return addf(buf, len, 0, "scan running %u/%u", (unsigned)(_sc_idx > BQ_SCAN_N ? BQ_SCAN_N : _sc_idx) + 1, BQ_SCAN_N + 1);
  if (!_sc_ran) return addf(buf, len, 0, "no scan yet%s", _s.mppt_auto ? " (auto: 2 min after fast charge starts)" : "");
  unsigned f = _sc_fail < 11 ? _sc_fail : 0;
  unsigned ago = (unsigned)((_now - _sc_done_at) / 60000UL);
  if (!_sc_have) return addf(buf, len, 0, "scan failed: %s %um ago", why[f], ago);
  // the last good table first; a later failure is a short note at the end, dropped when it does not fit
  n = addf(buf, len, n, "scan %um ago: best %u.%u%% %umW, 81%% %umW |", (unsigned)((_now - _sc_ok_at) / 60000UL),
           _sc_best_x10 / 10, _sc_best_x10 % 10, (unsigned)_sc_best_mw, (unsigned)_sc_81_mw);
  for (unsigned i = _sc_table_lo; i < BQ_SCAN_N; i++) n = addf(buf, len, n, " %u:%u", scanShareX10(i) / 10, (unsigned)_sc_table[i]);
  if (_sc_fail) {
    char note[40];
    int w = snprintf(note, sizeof(note), "; last try: %s %um", why[f], ago);
    if (w > 0 && n + w < len) { memcpy(buf + n, note, w + 1); n += w; }
  }
  return n;
}

// ---- IBAT calibration ----

int16_t BQ25798::batteryCurrentMa() const {
  if (!_r.valid) return 0;
  return hasBatteryReference() ? _ref_ma : _r.ibat_ma;
}

void BQ25798::setBatteryReference(int16_t ma, uint32_t now) {
  _ref_ok = true;
  _ref_ma = ma;
  _ref_at = now;
  if (_r.valid) _learn.feed(_cal, _r.ibat_raw_ma, ma);
}

void BQ25798::clearBatteryReference() {
  _ref_ok = false;
  _learn.gap();
}

int BQ25798::ibatCalManual(int true_ma) {
  if (!_r.valid) return -1;
  IbatCurve c = _cal;
  int r = IbatCal::setManual(c, _def, _r.ibat_raw_ma, true_ma);
  if (r != IBAT_SET_OK) return r;
  _cal = c;
  _r.ibat_ma = IbatCal::apply(_cal, _def, _r.ibat_raw_ma);
  return IBAT_SET_OK;
}

void BQ25798::ibatCalClear(bool auto_only) {
  if (auto_only) IbatCal::clearAuto(_cal);
  else memset(&_cal, 0, sizeof(_cal));
  _r.ibat_ma = IbatCal::apply(_cal, _def, _r.ibat_raw_ma);
  _learn.gap();
}

bool BQ25798::ibatCurveLoad(const uint8_t* data, int len) {
  IbatCurve c;
  bool ok = IbatCal::fromFile(data, len, c);
  _cal = c;   // empty when the file is bad
  _learn.synced(_cal, 0, false);
  _r.ibat_ma = IbatCal::apply(_cal, _def, _r.ibat_raw_ma);
  return ok;
}

void BQ25798::ibatCurveImportLegacy(const IbatLegacyCal& L) {
  IbatCal::importLegacy(_cal, L);
  _learn.synced(IbatCurve(), 0, false);   // the file (if any) does not have it: save it
  if (!IbatCal::empty(_cal)) _learn.markDirty();
  _r.ibat_ma = IbatCal::apply(_cal, _def, _r.ibat_raw_ma);
}

// x10 -> "-2.4" / "12" (no ".0")
static int addx10(char* buf, int len, int n, int v) {
  int a = v < 0 ? -v : v;
  const char* sg = v < 0 ? "-" : "";
  if (a % 10) return addf(buf, len, n, "%s%d.%d", sg, a / 10, a % 10);
  return addf(buf, len, n, "%s%d", sg, a / 10);
}

static const char* dirName(int dir) { return dir == IBAT_CHG ? "chg" : "dis"; }

// "chg auto 5p": where the points of this direction come from (manual > auto > build > default) and how many count
static int formatDir(char* buf, int len, int n, const IbatCurve& st, const IbatCurve& def, int dir) {
  IbatEff e = IbatCal::build(st, def, dir);
  IbatFit f = IbatCal::fit(e, dir);
  n = addf(buf, len, n, "%s %s %up", dirName(dir),
           f.points == 0 ? "default" : f.source == 2 ? "manual" : f.source == 1 ? "auto" : "build", (unsigned)e.m);
  if (f.rejected || e.ignored) n = addf(buf, len, n, " BAD%u", (unsigned)__builtin_popcount(e.ignored));
  return n;
}

// "CHG_OFFSET_X10=0 CHG_GAIN_X1000=1000": the line outside the covered range (the two-point fit, or the build's)
static int formatFit(char* buf, int len, int n, const IbatCurve& st, const IbatCurve& def, int dir) {
  IbatFit f = IbatCal::fit(IbatCal::build(st, def, dir), dir);
  const char* d = dir == IBAT_CHG ? "CHG" : "DIS";
  return addf(buf, len, n, "%s_OFFSET_X10=%d %s_GAIN_X1000=%d", d, (int)lroundf(f.offset_ma * 10.0f), d, (int)lroundf(f.gain * 1000.0f));
}

int BQ25798::formatIbat(char* buf, int len) const {
  buf[0] = 0;
  if (!_found) return addf(buf, len, 0, "no BQ25798 at 0x6B");
  int n = 0;
  if (!_r.valid) n = addf(buf, len, n, "bq ?");
  else {
    n = addf(buf, len, n, "bq %+d>%+d", _r.ibat_raw_ma, _r.ibat_ma);
    if (hasBatteryReference()) n = addf(buf, len, n, " ina %+d", _ref_ma);
  }
  n = addf(buf, len, n, " | ");
  n = formatDir(buf, len, n, _cal, _def, IBAT_CHG);
  n = addf(buf, len, n, " | ");
  n = formatDir(buf, len, n, _cal, _def, IBAT_DIS);
  // the line outside the curve, as build flags: only when it still fits whole
  char tmp[96];
  int w = formatFit(tmp, sizeof(tmp), 0, _cal, _def, IBAT_CHG);
  tmp[w++] = ' ';
  w = formatFit(tmp, sizeof(tmp), w, _cal, _def, IBAT_DIS);
  if (n + 3 + w < len) n = addf(buf, len, n, " | %s", tmp);
  return n;
}

int BQ25798::formatIbatCurve(char* buf, int len, int dir, int page) const {
  buf[0] = 0;
  IbatEff e = IbatCal::build(_cal, _def, dir);
  int from = page == 2 ? 5 : 0, to = page == 2 ? IbatCal::NPTS[dir] : 5;
  int n = addf(buf, len, 0, "%s", dirName(dir));
  bool any = false;
  for (int i = from; i < to; i++) {
    const IbatPoint* p = IbatCal::chosen(_cal, _def, dir, i);
    if (!p) {
      const IbatPoint& q = _cal.p[dir][i];   // collected, not usable yet
      if (!q.n) continue;
      p = &q;
    }
    any = true;
    n = addf(buf, len, n, " %d:", (int)IbatCal::PT_MA[dir][i]);
    n = addx10(buf, len, n, p->raw_x10);
    n = addf(buf, len, n, ">");
    n = addx10(buf, len, n, p->ref_x10);
    bool use = IbatCal::usable(*p, i) || p->src == IBAT_SRC_BUILD;
    if (p->src == IBAT_SRC_MANUAL) n = addf(buf, len, n, "m");
    else if (p->src == IBAT_SRC_BUILD) n = addf(buf, len, n, "d");
    else n = addf(buf, len, n, "a%u%s", (unsigned)p->n, use ? "" : "*");
    if (e.ignored & (1u << i)) n = addf(buf, len, n, "!");
  }
  if (!any) n = addf(buf, len, n, " -");
  if (page == 1) {
    for (int i = 5; i < IBAT_NP; i++) if (_cal.p[dir][i].n || _def.p[dir][i].n) { n = addf(buf, len, n, " ..2"); break; }
  }
  return n;
}

int BQ25798::formatIbatExport(char* buf, int len, int dir, int page) const {
  buf[0] = 0;
  IbatEff e = IbatCal::build(_cal, _def, dir);
  int from = page == 2 ? 5 : 0, to = page == 2 ? IBAT_NP : 5;
  int cnt = 0;
  for (int k = 0; k < e.m; k++) if (e.idx[k] >= from && e.idx[k] < to) cnt++;
  if (cnt == 0) return addf(buf, len, 0, "no points for %s%s", dirName(dir), page == 2 ? " 2" : "");
  int n = addf(buf, len, 0, "-D BQ25798_IBAT_%s_CURVE%s='\"", dir == IBAT_CHG ? "CHG" : "DIS", page == 2 ? "2" : "");
  int sgn = dir == IBAT_CHG ? 1 : -1;
  bool first = true;
  for (int k = 0; k < e.m; k++) {
    if (e.idx[k] < from || e.idx[k] >= to) continue;
    char tmp[24];
    int w = addx10(tmp, sizeof(tmp), 0, sgn * (int)e.raw[k]);
    tmp[w++] = ':';
    w = addx10(tmp, sizeof(tmp), w, sgn * (int)e.ref[k]);
    if (n + w + 1 + 3 >= len) { buf[0] = 0; return -1; }   // never a cut-off curve
    n = addf(buf, len, n, "%s%s", first ? "" : ",", tmp);
    first = false;
  }
  if (n + 3 >= len) { buf[0] = 0; return -1; }
  n = addf(buf, len, n, "\"'");
  if (page == 1) {   // points on page 2 too
    for (int k = 0; k < e.m; k++) if (e.idx[k] >= 5) { n = addf(buf, len, n, " ..2"); break; }
  }
  return n;
}
