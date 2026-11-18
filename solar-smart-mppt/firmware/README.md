# solar-smart-mppt firmware (MSPM0L1306)

Status: **v0.2, not tested on hardware** (the v0.4 board is not built yet). Host tests pass; it compiles to ~15 KB.

What the MCU does:
- sets up the BQ25798 charger (charge voltage and current, input limit, MPPT, NTC) and keeps it set
  (checks every second, writes again after a chip or watchdog reset); `~CE` goes low only when our
  values are in the chip,
- BMS: under-voltage, over-voltage, software over-current, short circuit (COMP0), J7 switch, LTO 2S balancing,
- charge counter through the 20 mΩ shunt (learns the capacity from a full charge down to the cut-off),
- shows itself to the repeater as an **INA260 on 0x41** (MeshCore reads it with no change),
- has its own register map on **0x42** for status and settings; settings are kept in flash,
- v0.4 boards: battery **heater** (PA11, only with sun, from the battery NTC). The BQ25798 INT pin is not
  wired (dropped on 2026-11-18: no room to route it); the charger is read every second.

Board versions: the default build is for **v0.4** (J5 on PA0/PA1, heater). `make J5=pa10` is for the
**v0.3** boards made before 2026-11-13: J5 on PA10/PA11, OC_G on PA0, **no heater** (PA11 is
J5 SCL there and is never driven; the heater registers exist but have no effect, status2 bit 1 is set).

## Build

```
make SDK_ROOT=/path/to/mspm0-sdk      # build/smppt.hex, .bin, .elf
make test                             # host unit tests (gcc, ASan/UBSan)
```

Needs `gcc-arm-none-eabi` + `libnewlib-arm-none-eabi` and the TI MSPM0 SDK (only `source/ti/driverlib`,
`source/ti/devices/msp`, `source/third_party/CMSIS/Core/Include`; CI pins commit e6793f4).
CI: `.github/workflows/build-solar-smart-mppt-fw.yml` (tag `solar-smart-mppt-fw-v*` makes a release).

Flashing: [`../programmer`](../programmer) (ESP32-C3 SuperMini, web page, both images built in), or SWD on the bottom pads (TP11 SWDIO, TP16 SWCLK, TP17 NRST, TP12 3V3M, TP13 GND) with an
XDS110 or a J-Link / CMSIS-DAP probe that knows the MSPM0. `smppt cmd bsl` (or command 8) starts the
ROM bootloader. Its I2C is on PA0/PA1, which are the J5 pins, so the host can flash the MSPM0 over J5
(ROM BSL I2C address 0x48, TI BSL protocol). NONMAIN flash is left at the factory defaults.
A blank chip starts the BSL by itself; one with broken firmware needs SWD (the BSL invoke pin PA18 is
not on J5).

Boards made from the files before 2026-11-13 (PR #35 d6a898d, v0.3) have J5 on PA10/PA11 and OC_G on PA0:
build them with `make J5=pa10` (CI: `smppt-j5-pa10.hex`). There the ROM BSL is not on J5, so they are
flashed over SWD only and `smppt cmd bsl` answers "not now". The two images are not interchangeable:
the wrong one drives the charge FET gate from an I2C pin (and the v0.4 image would drive the heater pin PA11, J5 SCL there).

## Files

| File | What |
|---|---|
| `src/main.c` | main loop: LED every 10 ms; every second ADC, BQ, BMS, gauge, heater, live registers; commands |
| `src/settings.c` | defaults per chemistry, limits, the flash store (two 1 KB sectors at 0xF000, A/B, CRC) |
| `src/regs.c` | register maps 0x41 / 0x42 and the I2C framing |
| `src/bq25798.c` | BQ25798 driver (through a bus interface, so it is host tested) |
| `src/bms.c` | BMS decisions (pure logic) |
| `src/heater.c` | heater decision and TS to temperature table (pure logic) |
| `src/gauge.c` | charge counter and voltage table |
| `src/hal.c`, `analog.c`, `i2c_target.c` | MSPM0 glue: GPIO, I2C1 controller, timer, flash, OPA/ADC/COMP, I2C0 target |
| `test/test_main.c` | host tests (store with power cuts at every write, framing with prefetch, BMS, gauge, heater, BQ fake chip) |

## Pins added on v0.4

| Pin | MCU | Function |
|---|---|---|
| 18 | PA22 | not connected (OPA0 output pin, its output switch stays disabled) |
| 10 | PA11 (PINCM12, GPIO function 1, TIMG4_CCP1 = function 5) | heater MOSFET gate, high = on, 100k pull-down on the board; GPIO for 0 / 100 %, TIMG4 PWM (1 kHz) in between. Not touched with `J5=pa10` (J5 SCL there) |

All other pins are as in `src/board.h`.

## Registers on 0x42

16-bit registers, MSB first. Write: `<reg> <msb> <lsb> [<msb> <lsb>]...`; read: write `<reg>`, then read
2 bytes per register. The pointer moves on by one register after each one (read or write).

| Reg | Name | | Reg | Name (settings, R/W) | Default Li-ion / LiFePO4 / LTO 2S |
|---|---|---|---|---|---|
| 0x00 | ID 0x534D | | 0x20 | chem: 0 Li-ion, 1 LiFePO4, 2 LTO 2S, 0xFF = by JP4 | 0xFF |
| 0x01 | version major<<8 \| minor (0.3) | | 0x21 | capacity mAh (0 = learn) | 0 |
| 0x02 | status (below) | | 0x22 | vchg mV | 4200 / 3600 / 5600 |
| 0x03 | SOC % × 100 | | 0x23 | ichg mA | 1000 |
| 0x04 | remaining mAh | | 0x24 | iin mA (panel) | 3000 |
| 0x05 | pack mV | | 0x25 | mppt VOC_PCT code 0..7 (56.25..100 %) | 4 (81.25 %) |
| 0x06/07 | cell 1 / cell 2 mV (2S) | | 0x26 | vindpm mV, 0 = MPPT | 0 |
| 0x08 | battery mA, signed, + = charging | | 0x27 | ntc: 0 auto, 1 off, 2 on | 0 |
| 0x09 | panel mV | | 0x28/29 | od_off / od_on mV (pack) | 3000/3300, 2700/3000, 3600/4000 |
| 0x0A | panel mA | | 0x2A | ship mV | 2900 / 2600 / 3400 |
| 0x0B | temperature 0.1 °C (BQ die) | | 0x2B/2C | j7_off / j7_on mV | 3300/3500, 3000/3150, 4000/4400 |
| 0x0C | active chem << 8 \| JP4 | | 0x2D | idsg_max mA (0 = off) | 0 |
| 0x0D | BQ CHG_STAT | | 0x2E | isc mA (short trip) | 1500 |
| 0x0E | BQ REG20 << 8 \| REG21 | | 0x2F/30 | bal_delta / bal_start mV (LTO, per cell) | 30 / 2500 |
| 0x0F | learnt capacity mAh | | 0x31 | zero_uv (signed, from ZERO) | 0 |
| 0x10 | cycles × 10 | | 0x32 | shunt µΩ (gain trim) | 20000 |
| 0x11 | battery temperature 0.1 °C, signed (NTC through the BQ TS ADC), 0x8000 = no reading | | 0x33 | INA260 current sign: 0 = discharge + | 0 |
| 0x12 | heater PWM % now (0 = off) | | 0x34 | expert_n (0..8) | 0 |
| 0x13 | status 2 (below) | | 0x35..3C | expert: BQ reg << 8 \| value | – |
| 0x60 | UNLOCK: write 0xA55A | | 0x3D | heat_on, 0.1 °C signed (-300..400): heater on below | 20 (+2 °C) |
| 0x61 | CMD | | 0x3E | heat_off, 0.1 °C signed (heat_on + 10 .. 500): heater off above | 50 (+5 °C) |
| 0x62 | RESULT of the last write/command | | 0x3F | heat_duty, PWM % (0..100), 0 = heater disabled | 100 |
| 0x80+n | BQ25798 register n (read every second) | | | | |

A settings write is checked at once (limits from the chemistry and the board: 3 A fuse, 3.2 A shunt
range, 5.5 V parts on SYS) and works at once; RESULT says why it was refused. It stays only after SAVE.
Changing `chem` loads that chemistry's voltage defaults (currents, capacity and calibration stay).

Expert writes (BQ25798 registers 0x03..0x18 except 0x0A cell count and 0x11) are applied after our own; the bits we manage
(VSYSMIN, VREG, ICHG, IINDPM, VINDPM, VOC_PCT/EN_MPPT, TS_IGNORE, WATCHDOG, ship, AUTO_INDET_EN,
SFET_PRESENT, ADC_EN, TS_ADC_DIS, EN_ICO, EN_HIZ, EN_CHG) always win.

Commands (write to 0x61; `*` = write 0xA55A to 0x60 first, valid 2 s, one command):
1 SAVE\*, 2 LOAD, 3 DEFAULTS\* (also saved), 4 COUNTER_RESET, 5 SET_FULL, 6 ZERO\* (0 A now; needs SAVE),
7 SHIP\*, 8 BSL\*, 9 REBOOT\*. RESULT: 0 OK, 1 locked, 2 range, 3 flash, 4 bad register, 5 busy,
6 unknown command, 7 not now, 0xFFFF running.

Status bits: 0 charging, 1 full, 2 sun, 3 no NTC, 4 discharge FET off, 5 charge FET off, 6 short,
7 under-voltage, 8 over-voltage, 9 over-current, 10 balancing, 11 J7 on, 12 BQ fault, 13 BQ I2C error,
14 % from voltage, 15 settings not saved.

Status 2 (0x13): bit 0 heating (gate driven), bit 1 v0.3 build (no heater). The first status word is full,
so the heater has its own word.
Firmware 0.3 adds, with VBUS present: status 2 bit 2 `hiz` (the chip set EN_HIZ), bit 3 `no PG`, bit 4 poor
source seen in the last 30 min; 0x14 poor source detections since start (REG22 POORSRC_FLAG, read to clear,
so counted every second), 0x15 minutes since the last (0xFFFF = none). Older firmware reads 0 there. The input
limit is REG05 in the mirror (0x85).

**Weak panel at dawn (MPPT mode, `vindpm` 0).** The chip sets VINDPM = VBUS0 - 0.7 V (3.6 V at the clamp), which
pulls a 5 V / 5 W panel under VPOORSRC: poor source, EN_HIZ = 1, PG = 0. EN_MPPT is now written only with PG = 1
and VBUS > VBUS_PRESENT and VSYS_STAT = 0 (as the chip needs it). While MPPT is not running and the converter
does not load the panel (EN_HIZ / no PG / poor source) the VBUS ADC is the open-circuit voltage Voc, if it was
read with the converter off for a whole second (not right after we cleared EN_HIZ). VINDPM is then set to 80 % of
Voc (down to 100 mV, 3.6..22 V) before EN_HIZ is cleared, at every drop-out, until PG = 1 and the chip's MPPT
runs. EN_HIZ is kept for 2 seconds of every 6 to get that reading. A fixed `vindpm` is unchanged. Not tested on
hardware.

## INA260 on 0x41

CONFIG (0x00, reset bit works), CURRENT (0x01, 1.25 mA/bit, signed, + = discharge by default as MeshCore's
battery gauge expects), VOLTAGE (0x02, 1.25 mV/bit, pack), POWER (0x03, 10 mW/bit), MASK/ALERT
(stored only), 0xFE = 0x5449, 0xFF = 0x2270. The pointer stays where it was written, like the real chip.

## MeshCore CLI

`smppt` on the repeater (built with `ENV_INCLUDE_INA260`, the default): `smppt` (status), `smppt get <name>`,
`smppt set <name> <value>`, `smppt save`, `smppt cmd load|defaults|reset|full|zero|ship|bsl|reboot`,
`smppt bq <reg>`, `smppt names`. Heater: `smppt set heat_on 20` (0.1 °C), `heat_off`, `heat_duty`;
`smppt get bat_temp` (0.1 °C), `heat_pct`; `smppt` (status) shows `bat:3.4C` and `heat` while the heater is on.

## Behaviour

- **Chemistry:** JP4 is read at start. If it differs from the one saved with the settings, the jumper
  wins and the voltages go back to its defaults (a LiFePO4 never gets 4.2 V by mistake).
- **NTC (auto):** TS above 84 % of REGN means no NTC (R2/R3 give ~85.5 % open, ~59.5 % with 10k at
  25 °C): TS is ignored and status bit 3 is set. With an NTC the BQ25798 JEITA defaults apply.
- **Heater (v0.4):** the MOSFET gate on PA11 (100k pull-down, high = on, heater fed from SYS) is driven only
  while the panel gives power (the `sun` flag: VBUS above 3.5 V). If the panel is too weak and the BQ25798 tops SYS up
  from the battery (battery discharging by more than 100 mA), the heater goes off for 60 s. The battery temperature is the NTC on J3 read through the BQ25798 TS ADC (10k B3435 with R2/R3
  5.1k/30k, table -35..+60 °C, interpolated, within about 0.5 °C of the formula plus the NTC and ADC
  tolerances). Below `heat_on` (+2 °C) the heater goes on, above `heat_off` (+5 °C) it goes off, in between
  it keeps its state. `heat_duty` is the PWM percent while on (1 kHz, TIMG4_CCP1; 100 = plain GPIO high,
  0 = heater disabled). **Fail safe = off:** no sun, no NTC (TS open, or `ntc` = off), no I2C to the BQ25798,
  any BQ fault flag, TS outside the table (shorted NTC, below -35 °C), ship mode, reboot, BSL. The gate is
  also low while the MCU is reset (pull-down). There is no MCU watchdog yet: if the main loop hangs with the
  heater on, the last output stays.
  The first pass after a start is also a decision pass, so a cold battery in the sun is heated within a second.
- **Under-voltage:** with sun the discharge FET goes off and charging goes on. Without sun the MCU would
  lose its own supply (it sits behind the FET, R19 turns it back on, and it would start over), so the
  BQ25798 goes to **ship mode** instead, in the same step the under-voltage is confirmed (3 s), while the
  discharge FET stays on and J7 goes off; it wakes when the panel gives power. The gauge is saved first.
  Until the sun is back the firmware never writes SDRV_CTRL back to 0 and keeps ~CE high, in case its
  own supply holds for a while after the command. LiFePO4: SFET_PRESENT stays 0 (with it the BQ25798
  would stop on battery at 3.2 V, mid-plateau; without it at 2.4 V), so there is no ship mode: the board
  stays on with J7 off until the BQ25798 cuts the pack at 2.4 V.
- **BQ25798 extras:** SFET_PRESENT for Li-ion and LTO, AUTO_INDET_EN and EN_ICO off, the TS ADC channel
  only with VBUS (on battery alone it keeps REGN on and stops the ADC under 3.2 V; the NTC is judged only
  while TS converts), sun = VBUS_PRESENT_STAT and VBUS over 3.5 V.
- **Short / over-current:** COMP0 turns the discharge FET off in its interrupt. With sun it tries again
  after 60 s; without sun ship mode is asked for in the same step, before the FET goes off in software.
  A COMP0 pass between the 1 s steps counts nothing (no debounce or pause step).
- **Gauge:** counts mA·s every second, sets 100 % when the BQ25798 says "charge done", learns the
  capacity from full to the cut-off, counts cycles. Without a capacity the % comes from a voltage table
  (status bit 14). Saved every hour and before ship mode / reboot (hourly saves: one sector erase about
  every 2 days, far below the flash endurance).
- **LED:** a 60 ms flash every 2 s: red = fault (short, OV, over-current, BQ fault or no I2C),
  yellow = BMS active (FET off, balancing); off when all is well.
- **Power:** SYSOSC fixed at 4 MHz, STOP0 between interrupts (I2C target, timer and COMP keep running).
  Expected ~0.15-0.3 mA for the MCU (to be measured). `-DSMPPT_SLEEP_ONLY` uses SLEEP0 if STOP0 causes
  trouble with the I2C target.

## Shunt range (bias)

The OPAs read SH_P and SH_N x16 against the 2.5 V VREF; the shunt (20 mΩ) sits on a bias made by R21/R22 10k
and R23/R24 to 3V3M.
- **v0.4: R23/R24 470k, bias ~69 mV.** SH_P reaches 0 V at about **3.4 A of discharge**; on charge the x16
  output reaches VREF (2.5 V) at about **4.3 A**. So the charge reading covers up to ~4.3 A and the discharge
  reading and the short trip up to ~3.4 A. The settings limits (3 A fuse, `isc` up to 3.2 A) stay inside this.
  Keep the x16 output below 2.5 V if the bias or the shunt is changed.
- v0.3: R23/R24 1M, bias ~33 mV. SH_P reaches 0 V at about **1.6 A of discharge**: discharge readings and
  the short trip stop there (the default `isc` of 1.5 A is just inside); charge reads to ~6 A.

## Known limits

- Heater PWM and the TS temperature table are not tried on hardware. If the PWM duty comes out
  inverted (100 - duty), swap the compare value in `hal_heater` (`src/hal.c`).
- The ROM bootloader is reachable over J5 (PA0/PA1); SWD stays as the fallback.
- The OPAs run without chopping (COMP0 sees their output); the offset is taken out with ZERO.
- Not yet checked on hardware: COMP0 channel for the OPA0 output (`COMP_IMSEL_OPA0_OUT`), I2C target in
  STOP0, ADC timing with the 1M dividers.
