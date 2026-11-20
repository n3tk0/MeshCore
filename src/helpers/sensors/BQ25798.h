#pragma once

#include <stdint.h>
#include <helpers/sensors/IbatCal.h>

// TI BQ25798: I2C buck-boost charger with solar MPPT (xiao-solar-lite, hardware/xiao-solar-lite).
//
// The chip charges on its own after power-on, with the values the PROG resistor sets (1S: 4.2 V,
// 1 A) and MPPT off. Firmware sets it up and keeps it set up:
//   - the I2C watchdog is turned off, so a hung or restarting MCU does not drop the setup
//     (a watchdog expiry would put VREG back to 4.2 V: bad for LiFePO4);
//   - every 10 s the setup registers are read back and anything at its reset value again (a power-on
//     reset when both the battery and the panel were gone, REG_RST) is written again;
//   - MPPT is switched on again each morning: the chip clears EN_MPPT itself when VBUS goes away and
//     will not take it while the battery is under VSYSMIN;
//   - ~CE (charge enable, active low) stays high until VREG reads back right, so a LiFePO4 cell is
//     never charged towards 4.2 V (JP2 at 2-3 holds CE high until then on the board);
//   - the CE jumper is read once at the first start, before the pin is driven: JP2 at 2-3 (LiFePO4)
//     with the chemistry still Li-ion (the default, or a mistake) keeps charging off until
//     `charger chem lifepo4`;
//   - MPPT mode (`charger vin mppt`) with a weak panel: the chip sets VINDPM = VBUS0 - 0.7 V at plug-in
//     (3.6 V at the clamp), which loads the panel under VPOORSRC: poor source, EN_HIZ = 1, PG = 0, and
//     MPPT (enabled only with PG) never runs. While the converter is not loading the panel (HIZ / no PG /
//     poor source) the VBUS ADC is the panel's open-circuit voltage: VINDPM is set to the VOC_PCT share of it (81 % default, rounded
//     down to 100 mV, 3.6 .. 22 V) BEFORE EN_HIZ is cleared, at every drop-out, until MPPT takes over;
//   - MPPT scan (`charger vin mppt auto`, `charger scan`): in fast charge with MPPT running, the driver
//     measures Voc (EN_HIZ for ~1.2 s), switches EN_MPPT off and steps a fixed VINDPM from 62.5 to 90 % of Voc
//     in 2.5 % steps (~2 s a step, one-shot ADC: VBUS x IBUS), re-measures the first step at the end (power
//     moved > 20 %: clouds, the scan is thrown away), then sets VOC_PCT to the code nearest to the best
//     share (auto mode only) and EN_MPPT back on. A small state machine with its own fast tick (due()), the
//     10 s poll waits during the ~30 s; first scan ~2 min after the conditions are met, then hourly;
//   - one-shot ADC every 10 s: battery and panel voltage / current, SYS, die temperature (continuous
//     instead, should the results ever read as zeros after a one-shot);
//   - an empty battery with no sun for 5 min: shutdown mode (the ship FET cuts the battery; only
//     panel voltage wakes it), when a ship FET is fitted.
//
// Pure logic over a register bus, so it runs in the native unit tests.

#define BQ25798_ADDR           0x6B

#define BQ_CHEM_LIION          0
#define BQ_CHEM_LIFEPO4        1

#define BQ_POLL_MS             10000UL
#define BQ_LOW_BATT_MS         (5 * 60000UL)   // under the cut-off with no sun this long: shutdown
#define BQ_ADC_STALE_MS        60000UL         // no ADC result this long: the battery is under 2.9 V

#define BQ_ICHG_MIN            100
#define BQ_ICHG_MAX            2000
#define BQ_VINDPM_MIN          3600
#define BQ_VINDPM_MAX          22000
#define BQ_VOC_PCT_DEFAULT     81              // MPPT VOC_PCT (and auto VINDPM) default: 81.25 %; selectable 63|69|75|81|88
#define BQ_VOC_MIN_MV          3000            // a VBUS result under this is not an open-circuit reading
#define BQ_HIZ_HOLD_POLLS      2               // EN_HIZ is kept this many polls to get a no-load VBUS result ...
#define BQ_HIZ_HOLD_PERIOD     6               // ... in every this many polls of a poor-source loop
#define BQ_POORSRC_KEEP_MS     (30 * 60000UL)  // `charger` shows a poor source seen this long ago
#define BQ_CUTOFF_MIN          2900

// MPPT scan
#define BQ_SCAN_N              12              // shares 62.5, 65.0 ... 90.0 % (0.1 % units below)
#define BQ_SCAN_SHARE0         625
#define BQ_SCAN_SHARE_STEP     25
#define BQ_SCAN_SETTLE_MS      1500            // after a VINDPM change, before the ADC
#define BQ_SCAN_ADC_MS         300             // a one-shot conversion
#define BQ_SCAN_HIZ_MS         1200            // EN_HIZ before the Voc conversion
#define BQ_SCAN_FIRST_MS       120000UL        // first scan of a sun session: conditions held this long
#define BQ_SCAN_HOLD_MS        20000UL         // later scans: conditions held this long
#define BQ_SCAN_PERIOD_MS      3600000UL       // then once an hour
#define BQ_SCAN_RETRY_MS       600000UL        // after a scan that was thrown away
#define BQ_SCAN_DRIFT_PCT      20              // first step re-measured at the end: more change = clouds
#define BQ_SCAN_MIN_VINDPM     3900            // steps with a lower VINDPM are skipped (VPOORSRC region: HIZ / PG loss)
#define BQ_SCAN_MIN_STEPS      3               // fewer steps left: no scan
#define BQ_SCAN_MIN_MW         100             // under this the panel gives nothing to compare
// scan state machine
#define BQ_SC_IDLE             0
#define BQ_SC_BEGIN            1               // conditions checked, EN_MPPT off + EN_HIZ on
#define BQ_SC_VOC              2               // start the Voc conversion
#define BQ_SC_VOC_READ         3
#define BQ_SC_STEP_ADC         4               // VINDPM settled: start a conversion
#define BQ_SC_STEP_READ        5
// why a scan was thrown away (`charger scan`)
#define BQ_SCF_NONE            0
#define BQ_SCF_ABORT           1               // settings changed / driver restarted
#define BQ_SCF_CLOUDS          2               // first step moved > 20 % between start and end
#define BQ_SCF_SUN             3               // VBUS / PG gone, or EN_HIZ set again
#define BQ_SCF_STATE           4               // not in fast charge any more
#define BQ_SCF_I2C             5
#define BQ_SCF_NOVOC           6
#define BQ_SCF_DARK            7               // too little power to compare
#define BQ_SCF_RESET           8               // the chip was reset
#define BQ_SCF_NOCOND          9               // the conditions were not met at the start
#define BQ_SCF_LOWV            10              // fewer than 3 steps with VINDPM >= BQ_SCAN_MIN_VINDPM: panel too low

// registers
#define BQ_REG_VSYSMIN         0x00
#define BQ_REG_VREG            0x01   // 16 bit, 10 mV
#define BQ_REG_ICHG            0x03   // 16 bit, 10 mA
#define BQ_REG_VINDPM          0x05   // 100 mV
#define BQ_REG_IINDPM          0x06   // 16 bit, 10 mA
#define BQ_REG_CTRL0           0x0F
#define BQ_REG_CTRL1           0x10
#define BQ_REG_CTRL2           0x11
#define BQ_REG_CTRL5           0x14
#define BQ_REG_MPPT            0x15
#define BQ_REG_NTC1            0x18
#define BQ_REG_STAT0           0x1B
#define BQ_REG_STAT1           0x1C
#define BQ_REG_STAT3           0x1E
#define BQ_REG_FAULT0          0x20
#define BQ_REG_FAULT1          0x21
#define BQ_REG_FLAG0           0x22   // POORSRC_FLAG bit 4 (reading clears the flags)
#define BQ_STATUS_LEN          8      // 0x1B..0x22 read in one go
#define BQ_REG_ADC_CTRL        0x2E
#define BQ_REG_ADC_DIS0        0x2F
#define BQ_REG_ADC_DIS1        0x30
#define BQ_REG_IBUS_ADC        0x31   // ... 0x42: IBUS IBAT VBUS VAC1 VAC2 VBAT VSYS TS TDIE, 16 bit each
#define BQ_REG_VBUS_ADC        0x35
#define BQ_REG_PART            0x48

#define BQ_CONFIG_LEN          0x19   // 0x00..0x18 read back in one go

// values written
#define BQ_IINDPM_MA           3000   // the panel limits itself through VINDPM / MPPT
#define BQ_MPPT_DLYRATE        0x0A   // REG15 bits 4:1: VOC_DLY 300 ms, VOC_RATE 2 min (VOC_PCT 7:5, EN_MPPT 0 apart)
#define BQ_MPPT_BASE           0x8A   // the default: VOC_PCT 0.8125 + BQ_MPPT_DLYRATE (see BQ25798::mpptBase)
#define BQ_ADC_START           0xD0   // ADC_EN, one shot, 14 bit (12 ms a channel)
#define BQ_ADC_CONTINUOUS      0x90   // ADC_EN, continuous, 14 bit (fallback, see adc())
#define BQ_ADC_DIS0_VAL        0x04   // TS off: with it on, the ADC stops under 3.2 V on battery alone
#define BQ_ADC_DIS0_TS         0x00   // TS on: only with an NTC and sun (REGN is up then anyway)

// the NTC divider on TS (xiao-solar-lite / xiao-solar: REGN - 5.1k - TS - 30k || NTC - GND)
#ifndef BQ_NTC_R_TOP
#define BQ_NTC_R_TOP           5100.0f
#endif
#ifndef BQ_NTC_R_BOTTOM
#define BQ_NTC_R_BOTTOM        30000.0f
#endif
#ifndef BQ_NTC_R25
#define BQ_NTC_R25             10000.0f   // 103AT
#endif
#ifndef BQ_NTC_BETA
#define BQ_NTC_BETA            3435.0f
#endif
#define BQ_TS_NIGHT_MS         600000UL   // on battery alone: one conversion with TS this often ...
#define BQ_TS_NIGHT_MIN_MV     3300       // ... and only above this (with TS on the ADC stops under 3.2 V)
#define BQ_TCELL_KEEP_MS       900000UL   // a cell temperature is shown this long after its conversion
#define BQ_TCELL_NONE          INT16_MIN  // no reading (no NTC setting, no sun, open or shorted NTC)
#define BQ_ADC_DIS1_VAL        0xF0   // D+, D-, VAC2, VAC1 off

class BQ25798Bus {
public:
  virtual bool readRegs(uint8_t reg, uint8_t* buf, uint8_t n) = 0;
  virtual bool writeRegs(uint8_t reg, const uint8_t* buf, uint8_t n) = 0;
  virtual void setChargeEnable(bool on) { (void)on; }   // the ~CE pin (low = charge), if the MCU has it
  // the ~CE jumper as the pin reads before the MCU drives it: 1 = pulled high (LiFePO4, JP2 2-3),
  // 0 = pulled low (Li-ion, JP2 1-2), -1 = no pin / unknown
  virtual int8_t readCeJumper() { return -1; }
  virtual ~BQ25798Bus() { }
};

struct BQ25798Settings {
  uint8_t  version;     // BQ_SETTINGS_VERSION: the layout of the saved file
  uint8_t  chem;        // BQ_CHEM_*
  uint16_t ichg_ma;     // charge current
  uint16_t vindpm_mv;   // 0 = MPPT, else a fixed input voltage limit
  uint8_t  ntc;         // 1 = an NTC is on J3 (TS is used), 0 = TS_IGNORE
  uint8_t  shipfet;     // 1 = Q2 fitted (SFET_PRESENT), 0 = JP3 closed
  uint16_t cutoff_mv;   // battery empty: shutdown under this with no sun; 0 = off
  uint8_t  voc_pct;     // MPPT VOC_PCT: 63, 69, 75, 81 or 88 (62.5 / 68.75 / 75 / 81.25 / 87.5 %); 0 = default (81)
  uint8_t  mppt_auto;   // 1 = `charger vin mppt auto`: starts from voc_pct 81 (also after a restart), the scan moves it; 0 (also in older files) = fixed
  IbatLegacyCal cal;    // the IBAT calibration of build 934d2da (two points per direction); now only read (imported into the /ibatcal curve), written as zeros
  uint8_t  ina_pos;     // BQ_INA_BAT (0, default) | BQ_INA_SUN: where the INA219 is (battery line or solar input); a file without it: 0
  uint8_t  reserved;    // 0
};
// The /charger file is the struct as is. Files of 10, 11, 12 and 36 bytes (before voc_pct / mppt_auto / cal /
// ina_pos) are still read: fromFile().
#define BQ_INA_BAT             0   // the INA219 is in the battery line: IBAT is calibrated from it, it is the battery gauge
#define BQ_INA_SUN             1   // ... on the solar input: IBUS is calibrated from it, IBAT is left alone
#define BQ_SETTINGS_VERSION    1

struct BQ25798Reading {
  bool     valid;       // at least one ADC result
  uint16_t vbat_mv, vsys_mv, vbus_mv;
  int16_t  ibat_ma;     // + = charging the battery, - = the battery feeds the node; the IBAT ADC with the calibration applied
  int16_t  ibat_raw_ma; // ... as the ADC gave it
  int16_t  ibus_ma;     // from the panel; the IBUS ADC with the calibration applied
  int16_t  ibus_raw_ma; // ... as the ADC gave it
  int16_t  tdie_dc;     // die temperature, 0.1 °C
  uint16_t ts_pm;       // TS in 0.1 % of REGN, from a conversion with TS on in the last 15 min (0 = none)
  int16_t  tcell_dc;    // cell temperature from the NTC, 0.1 °C, or BQ_TCELL_NONE
};

class BQ25798 {
  BQ25798Bus& _bus;
  BQ25798Settings _s;
  BQ25798Reading _r;
  bool     _ts_in_shot;  // the running conversion has the TS channel on
  bool     _ts_night_any;
  uint32_t _ts_night_at, _tcell_at;   // the last night conversion with TS, the last TS result
  bool     _found, _applied, _ce_on, _adc_pending, _adc_continuous, _full, _shutdown_sent, _low_timing, _configured;
  uint8_t  _status[BQ_STATUS_LEN];   // 0x1B..0x22 (the last one: REG22 flags as read, cleared by the read)
  uint8_t  _mppt_reg;    // REG15 as last read
  uint8_t  _prev_state;  // charge state at the last poll
  int8_t   _jumper;      // readCeJumper() at the first begin(); -1 = unknown
  bool     _jumper_read; // ... only once: after that the MCU drives the pin
  uint32_t _next_poll, _adc_at, _low_since;
  uint16_t _reconfigs;   // times the setup was found back at reset values
  uint32_t _errors;      // failed register transfers
  uint32_t _polls, _adc_count;
  uint32_t _now;               // the last loop() time
  // poor source (REG22 POORSRC_FLAG is read-to-clear: counted at every read of the status)
  uint16_t _poorsrc_count;
  uint32_t _poorsrc_at;
  // auto VINDPM in MPPT mode
  bool     _shot_unl;          // the running one-shot started while the converter did not load the panel
  bool     _unl_now, _unl_last;// the converter did not load the panel at this / the last enforce()
  bool     _voc_valid;         // _voc_mv: a VBUS result taken without load, not used yet
  uint16_t _voc_mv;
  uint8_t  _hiz_hold;          // polls of this drop-out so far (EN_HIZ is held in the first ones)
  bool     _hiz_seen;          // EN_HIZ was set at the last enforce() (before it was cleared)
  uint8_t  _vindpm_reg;        // REG05 as last read / written
  uint16_t _auto_vin_mv;       // the last VINDPM set from Voc (0 = none yet)
  uint16_t _auto_vin_count;
  // MPPT scan: state machine (BQ_SC_*), see scanTick()
  uint8_t  _sc_state, _sc_idx, _sc_tries;
  uint8_t  _sc_lo, _sc_table_lo;    // first step measured (lower ones skipped: VINDPM too low); of the last accepted scan
  bool     _sc_forced, _sc_hiz_set, _sc_mppt_was;
  uint32_t _sc_at;                  // the next tick
  uint16_t _sc_voc;                 // mV
  uint32_t _sc_p[BQ_SCAN_N + 1];    // mW per step, [N] = the first step again at the end
  uint16_t _sc_okcount;             // scans that gave a result
  // schedule
  bool     _sc_cond;                // the conditions held at the last poll
  uint32_t _sc_cond_at, _sc_not_before;
  bool     _sc_first;               // no scan yet in this sun session
  // last result
  uint32_t _sc_table[BQ_SCAN_N];    // mW per share of the last accepted scan
  bool     _sc_have;
  uint8_t  _sc_fail;                // BQ_SCF_*: why the last scan was thrown away (0 = it was not)
  bool     _sc_ran;
  uint32_t _sc_done_at, _sc_ok_at;  // end of the last scan (accepted or not), of the last accepted one
  uint16_t _sc_best_x10;            // best share, 0.1 %
  uint32_t _sc_best_mw, _sc_81_mw;
  uint8_t  _sc_chosen;              // VOC_PCT label set by the last accepted scan in auto mode (0 = none)
  // IBAT calibration: a reference current (INA219 in the battery line), the auto learning
  IbatLearner _learn;
  IbatCurve _cal;                   // the stored calibration curve (/ibatcal)
  IbatCurve _def;                   // the build default curve (BQ25798_IBAT_*_CURVE)
  bool     _ref_ok;
  int16_t  _ref_ma;                 // + = charging (INA219 on bat) / + = from the panel (on sun)
  uint32_t _ref_at;
  // IBUS calibration (INA219 on the solar input): the same kind of curve, CHG half only
  IbatLearner _learnBus;
  IbatCurve _calBus;                // /ibuscal
  IbatCurve _defBus;                // BQ25798_IBUS_CURVE / _CURVE2

  bool read(uint8_t reg, uint8_t* buf, uint8_t n);
  bool write8(uint8_t reg, uint8_t v);
  bool write16(uint8_t reg, uint16_t v);
  void setCE(bool on);
  bool fresh() const { return _ref_ok && (int32_t)(_now - _ref_at) < (int32_t)(3 * BQ_POLL_MS); }
  bool readStatus();
  void harvestVoc(bool unloaded, bool mppt_running);
  void enforce();
  int formatLine(char* buf, int len, uint8_t trim) const;
  void adc(uint32_t now);
  bool readAdc(uint32_t now);
  void lowBattery(uint32_t now);
  bool scanConditions(bool need_fast) const;
  void scanSchedule(uint32_t now);
  void scanTick(uint32_t now);
  void scanFinish(uint32_t now, uint8_t fail);
  int scanAdc(uint32_t* mw, uint16_t* vbus);   // 1 ok, 0 not done yet, -1 error
  bool scanSetVindpm(unsigned idx);

public:
  explicit BQ25798(BQ25798Bus& bus);

  static void defaults(BQ25798Settings& s);
  static bool valid(const BQ25798Settings& s);
  static uint16_t vregFor(uint8_t chem) { return chem == BQ_CHEM_LIFEPO4 ? 3600 : 4200; }
  // LiFePO4: 3.0 V, not lower: on battery alone the ADC needs 2.9 V
  static uint16_t defaultCutoff(uint8_t chem) { return chem == BQ_CHEM_LIFEPO4 ? 3000 : 3300; }
  static uint16_t cutoffMax(uint8_t chem) { return chem == BQ_CHEM_LIFEPO4 ? 3300 : 3800; }   // well under VREG
  // LiFePO4 3.0 V (its plateau is 3.2-3.3 V); Li-ion keeps the 1S power-on value
  static uint16_t vsysminFor(uint8_t chem) { return chem == BQ_CHEM_LIFEPO4 ? 3000 : 3500; }

  // probe (part number) and stop the watchdog; charging stays as the CE jumper has it until apply()
  bool begin(uint32_t now);
  // the settings (loaded from flash or changed by CLI): written at once, then kept
  void apply(const BQ25798Settings& s);
  // the 10 s poll, or the scan's own tick while a scan runs (a few hundred ms apart)
  bool due(uint32_t now) const {
    return _found && ((int32_t)(now - _next_poll) >= 0 || (_sc_state && (int32_t)(now - _sc_at) >= 0));
  }
  void loop(uint32_t now);

  bool found() const { return _found; }
  bool applied() const { return _applied; }
  const BQ25798Settings& settings() const { return _s; }
  const BQ25798Reading& reading() const { return _r; }
  bool vbusPresent() const { return _status[0] & 0x01; }
  bool powerGood() const { return _status[0] & 0x08; }
  bool inVindpm() const { return _status[0] & 0x40; }
  uint8_t chargeState() const { return _status[1] >> 5; }
  bool chargingEnabled() const { return _ce_on; }
  int8_t ceJumper() const { return _jumper; }
  // JP2 says LiFePO4 but the settings say Li-ion: no charging until `charger chem lifepo4`
  bool chargeHeld() const { return _jumper == 1 && _s.chem != BQ_CHEM_LIFEPO4; }
  bool adcContinuous() const { return _adc_continuous; }
  bool mpptOn() const;
  uint8_t vocPct() const { return _s.voc_pct; }
  bool mpptAuto() const { return _s.mppt_auto; }
  // MPPT scan. scanStart(): 0 = started (runs from the next loop(), ~30 s; any mppt mode, the result is
  // kept; with `vin mppt auto` it also sets VOC_PCT), -1 = fixed VINDPM mode, -2 = no panel / PG / EN_HIZ,
  // -3 = already running
  int scanStart();
  bool scanning() const { return _sc_state != 0; }
  bool scanHasResult() const { return _sc_have; }
  uint16_t scanCount() const { return _sc_okcount; }
  uint8_t scanChosen() const { return _sc_chosen; }
  // the share (0.1 %) scanned at step i, and the power there in mW (last accepted scan)
  static uint16_t scanShareX10(unsigned i) { return (uint16_t)(BQ_SCAN_SHARE0 + BQ_SCAN_SHARE_STEP * i); }
  uint32_t scanPowerMw(unsigned i) const { return i < BQ_SCAN_N ? _sc_table[i] : 0; }
  // the VOC_PCT label (63|69|75|81|88) for the share nearest to share_x10 (0.1 %)
  static uint8_t nearestVocPct(uint16_t share_x10);
  // `charger scan` with no scan running: "scan 12m ago: best 75% 1234mW, 81% 1200mW | 62:870 65:900 ..."
  int formatScan(char* buf, int len) const;
  uint8_t faults0() const { return _status[5]; }
  uint8_t faults1() const { return _status[6]; }
  uint16_t reconfigs() const { return _reconfigs; }
  uint32_t errors() const { return _errors; }
  uint32_t polls() const { return _polls; }          // loop() runs that talked to the chip
  uint32_t adcCount() const { return _adc_count; }   // ADC results read
  bool shutdownSent() const { return _shutdown_sent; }
  uint16_t poorSourceCount() const { return _poorsrc_count; }   // poor-source detections seen since boot
  uint16_t autoVindpm() const { return _auto_vin_mv; }          // last VINDPM set from Voc, mV (0 = none)
  uint16_t autoVindpmCount() const { return _auto_vin_count; }
  // VINDPM (REG05) for a panel open-circuit voltage: voc_pct (63|69|75|81|88, the VOC_PCT setting) of it,
  // rounded down to 100 mV, 3600..22000
  static uint16_t vindpmFromVoc(uint16_t voc_mv, uint8_t voc_pct = BQ_VOC_PCT_DEFAULT);
  // the VOC_PCT setting: 63|69|75|81|88 -> REG15 bits 7:5 (1..5), -1 for anything else
  static int vocCode(int pct);
  // REG15 without EN_MPPT for a setting (VOC_PCT bits 7:5 + the delay / rate bits)
  static uint8_t mpptBase(uint8_t voc_pct);
  // the settings from the /charger file's bytes (len as read): false for a short or invalid file;
  // a 10-byte file from before voc_pct gets the default 81, one without the calibration none (a partial one is dropped)
  static bool fromFile(const void* data, int len, BQ25798Settings& s);
  static const char* stateName(uint8_t st);
  // TS in 0.1 % of REGN -> 0.1 °C; BQ_TCELL_NONE for an open (no NTC, > 84 %) or shorted (< 5 %) NTC
  static int16_t ntcTemp(uint16_t ts_pm);
  bool ntcOpen() const { return _s.ntc && _r.ts_pm > 840; }

  // The battery current, + = charging: a reference sensor's (setBatteryReference, an INA219 in the battery
  // line) while it is fresh, else the IBAT ADC with the calibration applied.
  int16_t batteryCurrentMa() const;
  // Call once per new ADC result (adcCount() moved): the reference current (+ = charging, as the sensor
  // measures it now). With a stable pair the calibration learns from it (auto mode).
  void setBatteryReference(int16_t ma, uint32_t now);
  void clearBatteryReference();   // the sensor is gone / failed: the corrected IBAT ADC counts again (also the sun reference)

  // ---- INA219 position and the IBUS calibration (see IbatCal.h) ----
  bool inaSun() const { return _s.ina_pos == BQ_INA_SUN; }
  // Call once per new ADC result with the INA219 on the solar input (`charger ina sun`): the panel current in mA
  // (+ = from the panel); with a stable pair the IBUS curve learns from it. Ignored when the INA219 is on bat.
  void setSunReference(int16_t ma, uint32_t now);
  bool hasSunReference() const { return inaSun() && fresh(); }
  const IbatCurve& ibusCurve() const { return _calBus; }
  const IbatCurve& ibusDefaultCurve() const { return _defBus; }
  int ibusCalManual(int true_ma);      // `charger ibus cal <mA>` (> 0): IBAT_SET_OK, IBAT_SET_* or -1 (no ADC result yet)
  void ibusCalClear(bool auto_only);
  bool ibusCalSaveDue(uint32_t now) const { return _learnBus.saveDue(now); }
  bool ibusCalDirty() const { return _learnBus.dirty(); }
  void ibusCalSaveFailed(uint32_t now) { _learnBus.saveFailed(now); }
  void ibusCalSaved(uint32_t now) { _learnBus.synced(_calBus, now, true); }
  int ibusCurveToFile(uint8_t* buf) const { return IbatCal::toFile(_calBus, buf, IBUS_FILE_MAGIC); }   // IBAT_FILE_LEN bytes
  bool ibusCurveLoad(const uint8_t* data, int len);
  // `charger ibus`: "sun ina 312>330 ina 335 | auto 5p | OFFSET_X10=.. GAIN_X1000=.."
  int formatIbus(char* buf, int len) const;
  int formatIbusCurve(char* buf, int len, int page) const;    // "ibus 5:4.8>5a14 ..."
  int formatIbusExport(char* buf, int len, int page = 1) const;   // "-D BQ25798_IBUS_CURVE='\"..\"'"
  // `charger ina`: "ina bat" / "ina sun"
  int formatIna(char* buf, int len) const;
  bool hasBatteryReference() const { return !inaSun() && fresh(); }   // signed: the reference may be a ms newer than the last loop()
  const IbatCurve& ibatCurve() const { return _cal; }
  const IbatCurve& ibatDefaultCurve() const { return _def; }
  // `charger ibat cal <mA>`: the current raw IBAT against the true current (+ charge, - discharge), into
  // the point nearest to the true value. IBAT_SET_OK, or IBAT_SET_* / -1 (no ADC result yet)
  int ibatCalManual(int true_ma);
  void ibatCalClear(bool auto_only);   // `charger ibat clear [auto]`: the next save writes it
  // the curve is to be written to /ibatcal: a new auto point, at most hourly
  bool ibatCalSaveDue(uint32_t now) const { return _learn.saveDue(now); }
  bool ibatCalDirty() const { return _learn.dirty(); }
  void ibatCalSaveFailed(uint32_t now) { _learn.saveFailed(now); }   // the write failed: still unsaved, retry in an hour
  void ibatCalSaved(uint32_t now) { _learn.synced(_cal, now, true); }   // after a write of the curve
  // the /ibatcal bytes (IBAT_FILE_LEN) as they are to be written
  int ibatCurveToFile(uint8_t* buf) const { return IbatCal::toFile(_cal, buf); }
  // at boot: the file's bytes (false: none or damaged: the curve stays empty) and, then, the 934d2da
  // calibration of /charger (imported once, and written to /ibatcal at the next save)
  bool ibatCurveLoad(const uint8_t* data, int len);
  void ibatCurveImportLegacy(const IbatLegacyCal& L);
  // `charger ibat`: "bq -24>-12 ina -12 | chg auto 5p | dis build 2p | CHG_OFFSET_X10=0 CHG_GAIN_X1000=1000 DIS_OFFSET_X10=120 DIS_GAIN_X1000=1000"
  int formatIbat(char* buf, int len) const;
  // `charger ibat curve [chg|dis] [2]`: "dis 5:-6>-5a12 10:-11>-10m ..." (page 1 = the first 5 points, 2 = the rest)
  int formatIbatCurve(char* buf, int len, int dir, int page) const;
  // `charger ibat export chg|dis [2]`: the line for platformio.ini (page 1 = BQ25798_IBAT_<dir>_CURVE with the points 0..4,
  // page 2 = BQ25798_IBAT_<dir>_CURVE2 with the rest; both are read at boot); -1 when it does not fit
  int formatIbatExport(char* buf, int len, int dir, int page = 1) const;

  // true once each time a charge ends ("done"): the gauge takes the cell as full
  bool takeFull() { bool f = _full; _full = false; return f; }

  // 0 = ok (the ship FET opens in 10 s), -1 = sun on the panel (the chip ignores it), -2 = no ship FET,
  // -3 = I2C error
  int shipMode();

  // one register, for `charger reg` (enforce() puts the managed bits back at the next poll)
  bool regRead(uint8_t reg, uint8_t* v) { return _found && read(reg, v, 1); }
  bool regWrite(uint8_t reg, uint8_t v) { return _found && write8(reg, v); }

  // one line: "sun 17.9V 0.31A, batt 3.95V +1.05A fast, mppt, die 41C, liion 4.20V 1000mA";
  // with the panel there but no charging, the reason follows "mppt off": ", vin 3.6V" (REG05), ", poor src x3 2m"
  // (detections, minutes since the last), ", hiz", ", no PG". At most len-1 characters (the CLI gives 155).
  int formatStatus(char* buf, int len) const;
};
