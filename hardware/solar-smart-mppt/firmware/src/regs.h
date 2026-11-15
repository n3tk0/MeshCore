// I2C target register models on J5:
//  0x41  INA260 emulation (MeshCore reads it with ENV_INCLUDE_INA260, no firmware change)
//  0x42  our own map: status, settings, commands, BQ25798 register mirror
// Registers are 16 bit, sent MSB first. A write is <pointer> [<msb> <lsb>]...; a read returns the
// register at the pointer. On 0x42 the pointer moves on by one after every register (read or write);
// on 0x41 it stays (like the real INA260).
#pragma once
#include "smppt.h"

#define ADDR_INA260 0x41
#define ADDR_SMPPT  0x42

// ---- 0x42 map ----
#define R_ID          0x00   // 0x534D "SM"
#define R_VERSION     0x01   // major << 8 | minor
#define R_STATUS      0x02   // ST_*
#define R_SOC         0x03   // percent * 100
#define R_REMAIN      0x04   // mAh
#define R_VPACK       0x05   // mV
#define R_VCELL1      0x06   // mV (2S)
#define R_VCELL2      0x07   // mV (2S)
#define R_IBAT        0x08   // mA, signed, + = charging
#define R_VBUS        0x09   // mV (panel)
#define R_IBUS        0x0A   // mA
#define R_TEMP        0x0B   // 0.1 degC, signed
#define R_CHEM        0x0C   // active chemistry << 8 | JP4
#define R_BQ_STAT     0x0D   // CHG_STAT (REG1C 7:5)
#define R_BQ_FAULT    0x0E   // REG20 << 8 | REG21
#define R_CAP_LEARNT  0x0F   // mAh learnt from a full charge down to empty, 0 = not yet
#define R_CYCLES      0x10   // full-equivalent cycles * 10

#define R_SET_FIRST   0x20
#define R_CHEM_SET    0x20   // 0 Li-ion, 1 LiFePO4, 2 LTO 2S, 0xFF = by JP4
#define R_CAPACITY    0x21
#define R_VCHG        0x22
#define R_ICHG        0x23
#define R_IIN         0x24
#define R_MPPT        0x25   // VOC_PCT code 0..7
#define R_VINDPM      0x26   // 0 = MPPT
#define R_NTC         0x27   // 0 auto, 1 off, 2 on
#define R_OD_OFF      0x28
#define R_OD_ON       0x29
#define R_SHIP        0x2A
#define R_J7_OFF      0x2B
#define R_J7_ON       0x2C
#define R_IDSG_MAX    0x2D
#define R_ISC         0x2E
#define R_BAL_DELTA   0x2F
#define R_BAL_START   0x30
#define R_ZERO_UV     0x31   // signed
#define R_SHUNT_UOHM  0x32
#define R_INA_SIGN    0x33
#define R_EXPERT_N    0x34
#define R_EXPERT0     0x35   // .. 0x3C: (BQ reg << 8) | value
#define R_SET_LAST    (R_EXPERT0 + EXPERT_MAX - 1)

#define R_UNLOCK      0x60   // write 0xA55A, then one protected command within 2 s
#define R_CMD         0x61
#define R_RESULT      0x62   // ERR_* of the last command / write, 0xFFFF while running
#define R_BQ_FIRST    0x80   // 0x80 + n = BQ25798 register n (byte), refreshed every second
#define R_BQ_LAST     (0x80 + 0x48)

#define UNLOCK_KEY 0xA55A

enum {   // R_CMD
  CMD_NONE = 0,
  CMD_SAVE = 1,          // protected
  CMD_LOAD = 2,
  CMD_DEFAULTS = 3,      // protected
  CMD_COUNTER_RESET = 4, // charge unknown until the next full charge
  CMD_SET_FULL = 5,      // battery is full now
  CMD_ZERO = 6,          // protected: current is 0 A now, store the offset
  CMD_SHIP = 7,          // protected
  CMD_BSL = 8,           // protected: reboot into the ROM bootloader
  CMD_REBOOT = 9,        // protected
};

// ---- INA260 ----
#define INA_CONFIG   0x00
#define INA_CURRENT  0x01   // 1.25 mA/bit, signed
#define INA_VOLTAGE  0x02   // 1.25 mV/bit
#define INA_POWER    0x03   // 10 mW/bit
#define INA_MASK     0x06
#define INA_ALERT    0x07
#define INA_MFR      0xFE   // 0x5449 "TI"
#define INA_DIE      0xFF   // 0x2270
#define INA_CONFIG_DEFAULT 0x6127

// Shared state the register model works on. The main loop owns 'live' and 'set'; a write from the bus
// goes to 'set' at once (the main loop applies it), commands are left in pending_cmd.
typedef struct {
  live_t live;
  settings_t set;
  gauge_state_t gauge;
  uint8_t bq[0x49];          // BQ25798 register mirror
  uint16_t ina_config, ina_mask, ina_alert;
  volatile uint8_t pending_cmd;
  volatile uint16_t result;
  bool unlocked;             // UNLOCK_KEY written at unlock_at_ms, not used yet
  uint32_t unlock_at_ms;
  uint32_t now_ms;           // set by the main loop
} regs_t;

extern regs_t R;

uint16_t regs_read(uint8_t addr7, uint8_t reg);
void regs_write(uint8_t addr7, uint8_t reg, uint16_t value);

// I2C framing, called from the target interrupt
// start of a transfer, once its direction is known
void i2cproto_start(uint8_t addr7, bool read);
void i2cproto_rx(uint8_t byte);
uint8_t i2cproto_tx(void);
// end of a transfer (STOP or repeated START); unsent = bytes left in the TX FIFO
void i2cproto_end(uint8_t unsent);
