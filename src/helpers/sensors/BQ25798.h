#pragma once

#include <stdint.h>

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
#define BQ_CUTOFF_MIN          2900

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
#define BQ_REG_ADC_CTRL        0x2E
#define BQ_REG_ADC_DIS0        0x2F
#define BQ_REG_ADC_DIS1        0x30
#define BQ_REG_IBUS_ADC        0x31   // ... 0x42: IBUS IBAT VBUS VAC1 VAC2 VBAT VSYS TS TDIE, 16 bit each
#define BQ_REG_PART            0x48

#define BQ_CONFIG_LEN          0x19   // 0x00..0x18 read back in one go

// values written
#define BQ_IINDPM_MA           3000   // the panel limits itself through VINDPM / MPPT
#define BQ_MPPT_BASE           0x8A   // VOC_PCT 0.8125, VOC_DLY 300 ms, VOC_RATE 2 min (EN_MPPT apart)
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
};
#define BQ_SETTINGS_VERSION    1

struct BQ25798Reading {
  bool     valid;       // at least one ADC result
  uint16_t vbat_mv, vsys_mv, vbus_mv;
  int16_t  ibat_ma;     // + = charging the battery, - = the battery feeds the node
  int16_t  ibus_ma;     // from the panel
  int16_t  tdie_dc;     // die temperature, 0.1 °C
  uint16_t ts_pm;       // TS in 0.1 % of REGN, from the last conversion that had TS on (0 = none)
  int16_t  tcell_dc;    // cell temperature from the NTC, 0.1 °C, or BQ_TCELL_NONE
};

class BQ25798 {
  BQ25798Bus& _bus;
  BQ25798Settings _s;
  BQ25798Reading _r;
  bool     _ts_in_shot;  // the running conversion has the TS channel on
  bool     _found, _applied, _ce_on, _adc_pending, _adc_continuous, _full, _shutdown_sent, _low_timing, _configured;
  uint8_t  _status[7];   // 0x1B..0x21
  uint8_t  _mppt_reg;    // REG15 as last read
  uint8_t  _prev_state;  // charge state at the last poll
  int8_t   _jumper;      // readCeJumper() at the first begin(); -1 = unknown
  bool     _jumper_read; // ... only once: after that the MCU drives the pin
  uint32_t _next_poll, _adc_at, _low_since;
  uint16_t _reconfigs;   // times the setup was found back at reset values
  uint32_t _errors;      // failed register transfers
  uint32_t _polls, _adc_count;

  bool read(uint8_t reg, uint8_t* buf, uint8_t n);
  bool write8(uint8_t reg, uint8_t v);
  bool write16(uint8_t reg, uint16_t v);
  void setCE(bool on);
  void enforce();
  void adc(uint32_t now);
  bool readAdc(uint32_t now);
  void lowBattery(uint32_t now);

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
  bool due(uint32_t now) const { return _found && (int32_t)(now - _next_poll) >= 0; }
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
  uint8_t faults0() const { return _status[5]; }
  uint8_t faults1() const { return _status[6]; }
  uint16_t reconfigs() const { return _reconfigs; }
  uint32_t errors() const { return _errors; }
  uint32_t polls() const { return _polls; }          // loop() runs that talked to the chip
  uint32_t adcCount() const { return _adc_count; }   // ADC results read
  bool shutdownSent() const { return _shutdown_sent; }
  static const char* stateName(uint8_t st);
  // TS in 0.1 % of REGN -> 0.1 °C; BQ_TCELL_NONE for an open (no NTC, > 84 %) or shorted (< 5 %) NTC
  static int16_t ntcTemp(uint16_t ts_pm);
  bool ntcOpen() const { return _s.ntc && _r.ts_pm > 840; }

  // true once each time a charge ends ("done"): the gauge takes the cell as full
  bool takeFull() { bool f = _full; _full = false; return f; }

  // 0 = ok (the ship FET opens in 10 s), -1 = sun on the panel (the chip ignores it), -2 = no ship FET,
  // -3 = I2C error
  int shipMode();

  // one register, for `charger reg` (enforce() puts the managed bits back at the next poll)
  bool regRead(uint8_t reg, uint8_t* v) { return _found && read(reg, v, 1); }
  bool regWrite(uint8_t reg, uint8_t v) { return _found && write8(reg, v); }

  // one line: "sun 17.9V 0.31A, batt 3.95V +1.05A fast, mppt, die 41C, liion 4.20V 1000mA"
  int formatStatus(char* buf, int len) const;
};
