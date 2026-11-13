# solar-smart-mppt firmware (MSPM0L1306)

Status: **v0.1, not tested on hardware** (the board is not built yet). Host tests pass; it compiles to ~14 KB.

What the MCU does:
- sets up the BQ25798 charger (charge voltage and current, input limit, MPPT, NTC) and keeps it set
  (checks every second, writes again after a chip or watchdog reset); `~CE` goes low only when our
  values are in the chip,
- BMS: under-voltage, over-voltage, software over-current, short circuit (COMP0), J7 switch, LTO 2S balancing,
- charge counter through the 20 mΩ shunt (learns the capacity from a full charge down to the cut-off),
- shows itself to the repeater as an **INA260 on 0x41** (MeshCore reads it with no change),
- has its own register map on **0x42** for status and settings; settings are kept in flash.

## Build

```
make SDK_ROOT=/path/to/mspm0-sdk      # build/smppt.hex, .bin, .elf
make test                             # host unit tests (gcc, ASan/UBSan)
```

Needs `gcc-arm-none-eabi` + `libnewlib-arm-none-eabi` and the TI MSPM0 SDK (only `source/ti/driverlib`,
`source/ti/devices/msp`, `source/third_party/CMSIS/Core/Include`; CI pins commit e6793f4).
CI: `.github/workflows/build-solar-smart-mppt-fw.yml` (tag `solar-smart-mppt-fw-v*` makes a release).

Flashing: SWD on the bottom pads (TP11 SWDIO, TP16 SWCLK, TP17 NRST, TP12 3V3M, TP13 GND) with an
XDS110 or a J-Link / CMSIS-DAP probe that knows the MSPM0. `smppt cmd bsl` (or command 8) starts the
ROM bootloader. Its I2C is on PA0/PA1, which are the J5 pins, so the host can flash the MSPM0 over J5
(ROM BSL I2C address 0x48, TI BSL protocol). NONMAIN flash is left at the factory defaults.
A blank chip starts the BSL by itself; one with broken firmware needs SWD (the BSL invoke pin PA18 is
not on J5).

## Files

| File | What |
|---|---|
| `src/main.c` | main loop: LED every 10 ms; every second ADC, BQ, BMS, gauge, live registers; commands |
| `src/settings.c` | defaults per chemistry, limits, the flash store (two 1 KB sectors at 0xF000, A/B, CRC) |
| `src/regs.c` | register maps 0x41 / 0x42 and the I2C framing |
| `src/bq25798.c` | BQ25798 driver (through a bus interface, so it is host tested) |
| `src/bms.c` | BMS decisions (pure logic) |
| `src/gauge.c` | charge counter and voltage table |
| `src/hal.c`, `analog.c`, `i2c_target.c` | MSPM0 glue: GPIO, I2C1 controller, timer, flash, OPA/ADC/COMP, I2C0 target |
| `test/test_main.c` | host tests (store with power cuts at every write, framing with prefetch, BMS, gauge, BQ fake chip) |

## Registers on 0x42

16-bit registers, MSB first. Write: `<reg> <msb> <lsb> [<msb> <lsb>]...`; read: write `<reg>`, then read
2 bytes per register. The pointer moves on by one register after each one (read or write).

| Reg | Name | | Reg | Name (settings, R/W) | Default Li-ion / LiFePO4 / LTO 2S |
|---|---|---|---|---|---|
| 0x00 | ID 0x534D | | 0x20 | chem: 0 Li-ion, 1 LiFePO4, 2 LTO 2S, 0xFF = by JP4 | 0xFF |
| 0x01 | version major<<8 \| minor | | 0x21 | capacity mAh (0 = learn) | 0 |
| 0x02 | status (below) | | 0x22 | vchg mV | 4200 / 3600 / 5600 |
| 0x03 | SOC % × 100 | | 0x23 | ichg mA | 1000 |
| 0x04 | remaining mAh | | 0x24 | iin mA (panel) | 3000 |
| 0x05 | pack mV | | 0x25 | mppt VOC_PCT code 0..7 (56.25..100 %) | 4 (81.25 %) |
| 0x06/07 | cell 1 / cell 2 mV (2S) | | 0x26 | vindpm mV, 0 = MPPT | 0 |
| 0x08 | battery mA, signed, + = charging | | 0x27 | ntc: 0 auto, 1 off, 2 on | 0 |
| 0x09 | panel mV | | 0x28/29 | od_off / od_on mV (pack) | 3000/3300, 2700/3000, 3600/4000 |
| 0x0A | panel mA | | 0x2A | ship mV | 2900 / 2600 / 3400 |
| 0x0B | temperature 0.1 °C (BQ die) | | 0x2B/2C | j7_off / j7_on mV | 3300/3500, 3000/3150, 4400/4700 |
| 0x0C | active chem << 8 \| JP4 | | 0x2D | idsg_max mA (0 = off) | 0 |
| 0x0D | BQ CHG_STAT | | 0x2E | isc mA (short trip) | 1500 |
| 0x0E | BQ REG20 << 8 \| REG21 | | 0x2F/30 | bal_delta / bal_start mV (LTO, per cell) | 30 / 2500 |
| 0x0F | learnt capacity mAh | | 0x31 | zero_uv (signed, from ZERO) | 0 |
| 0x10 | cycles × 10 | | 0x32 | shunt µΩ (gain trim) | 20000 |
| 0x60 | UNLOCK: write 0xA55A | | 0x33 | INA260 current sign: 0 = discharge + | 0 |
| 0x61 | CMD | | 0x34 | expert_n (0..8) | 0 |
| 0x62 | RESULT of the last write/command | | 0x35..3C | expert: BQ reg << 8 \| value | – |
| 0x80+n | BQ25798 register n (read every second) | | | | |

A settings write is checked at once (limits from the chemistry and the board: 3 A fuse, 3.2 A shunt
range, 5.5 V parts on SYS) and works at once; RESULT says why it was refused. It stays only after SAVE.
Changing `chem` loads that chemistry's voltage defaults (currents, capacity and calibration stay).

Expert writes (BQ25798 registers 0x03..0x18 except 0x0A cell count and 0x11) are applied after our own; the bits we manage
(VSYSMIN, VREG, ICHG, IINDPM, VINDPM, VOC_PCT/EN_MPPT, TS_IGNORE, WATCHDOG, ship, ADC_EN, EN_CHG) always win.

Commands (write to 0x61; `*` = write 0xA55A to 0x60 first, valid 2 s, one command):
1 SAVE\*, 2 LOAD, 3 DEFAULTS\* (also saved), 4 COUNTER_RESET, 5 SET_FULL, 6 ZERO\* (0 A now; needs SAVE),
7 SHIP\*, 8 BSL\*, 9 REBOOT\*. RESULT: 0 OK, 1 locked, 2 range, 3 flash, 4 bad register, 5 busy,
6 unknown command, 7 not now, 0xFFFF running.

Status bits: 0 charging, 1 full, 2 sun, 3 no NTC, 4 discharge FET off, 5 charge FET off, 6 short,
7 under-voltage, 8 over-voltage, 9 over-current, 10 balancing, 11 J7 on, 12 BQ fault, 13 BQ I2C error,
14 % from voltage, 15 settings not saved.

## INA260 on 0x41

CONFIG (0x00, reset bit works), CURRENT (0x01, 1.25 mA/bit, signed, + = discharge by default as MeshCore's
battery gauge expects), VOLTAGE (0x02, 1.25 mV/bit, pack), POWER (0x03, 10 mW/bit), MASK/ALERT
(stored only), 0xFE = 0x5449, 0xFF = 0x2270. The pointer stays where it was written, like the real chip.

## MeshCore CLI

`smppt` on the repeater (built with `ENV_INCLUDE_INA260`, the default): `smppt` (status), `smppt get <name>`,
`smppt set <name> <value>`, `smppt save`, `smppt cmd load|defaults|reset|full|zero|ship|bsl|reboot`,
`smppt bq <reg>`, `smppt names`.

## Behaviour

- **Chemistry:** JP4 is read at start. If it differs from the one saved with the settings, the jumper
  wins and the voltages go back to its defaults (a LiFePO4 never gets 4.2 V by mistake).
- **NTC (auto):** TS above 84 % of REGN means no NTC (R2/R3 give ~85.5 % open, ~59.5 % with 10k at
  25 °C): TS is ignored and status bit 3 is set. With an NTC the BQ25798 JEITA defaults apply.
- **Under-voltage:** with sun the discharge FET goes off and charging goes on. Without sun the MCU would
  lose its own supply (it sits behind the FET, R19 turns it back on, and it would start over), so the
  BQ25798 goes to **ship mode** instead; it wakes when the panel gives power. The gauge is saved first.
- **Short / over-current:** COMP0 turns the discharge FET off in its interrupt. With sun it tries again
  after 60 s; without sun it goes to ship mode.
- **Gauge:** counts mA·s every second, sets 100 % when the BQ25798 says "charge done", learns the
  capacity from full to the cut-off, counts cycles. Without a capacity the % comes from a voltage table
  (status bit 14). Saved every hour and before ship mode / reboot (hourly saves: one sector erase about
  every 2 days, far below the flash endurance).
- **LED:** a 60 ms flash every 2 s: red = fault (short, OV, over-current, BQ fault or no I2C),
  yellow = BMS active (FET off, balancing); off when all is well.
- **Power:** SYSOSC fixed at 4 MHz, STOP0 between interrupts (I2C target, timer and COMP keep running).
  Expected ~0.15-0.3 mA for the MCU (to be measured). `-DSMPPT_SLEEP_ONLY` uses SLEEP0 if STOP0 causes
  trouble with the I2C target.

## Known limits of the v0.3 board

- The shunt bias is only ~33 mV (R23/R24 1M with R21/R22 10k). SH_P reaches 0 V at about **1.6 A of
  discharge**: discharge readings and the short trip stop there. Lower R23/R24 (e.g. 220k) would move it.
- The ROM bootloader is reachable over J5 (PA0/PA1); SWD stays as the fallback.
- The OPAs run without chopping (COMP0 sees their output); the offset is taken out with ZERO.
- Not yet checked on hardware: COMP0 channel for the OPA0 output (`COMP_IMSEL_OPA0_OUT`), I2C target in
  STOP0, ADC timing with the 1M dividers.
