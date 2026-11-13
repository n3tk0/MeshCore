# solar-smart-mppt: MPPT charger + BMS 1S/2S + coulomb counter

A stand-alone power board for a solar repeater (any board: RAK, XIAO, ProMicro…), grown out
of [`../xiao-solar-lite`](../xiao-solar-lite): the same BQ25798 MPPT buck-boost charger, but the
XIAO stack is gone and a **TI MSPM0L1306** supervisor (VQFN-24, 64 KB flash) runs everything:

- drives the BQ25798 on its own I2C bus (hardware I2C1 master, PA15/PA16),
- shows itself to the repeater as an **INA260 on 0x41** (hardware I2C0 target, PA0/PA1 → J5; the ROM BSL I2C sits on the same pins),
  so MeshCore reads voltage, current and charge with `ENV_INCLUDE_INA260` and no firmware change,
- counts the charge through a 20 mΩ shunt: both shunt ends go through the zero-drift OPA0/OPA1
  (PGA up to 32×) into the 12-bit ADC,
- is the BMS: discharge FET (OD) and charge FET (OC) of an HJ8205 in the battery minus,
  short-circuit cut-off with COMP0 (32 ns) against its 8-bit DAC,
- balances a 2S LTO pack (~40 mA per cell),
- shows errors and BMS state on two LEDs on one pin.

30 × 77.5 mm (three across a 100 mm JLC panel), 2 layers, all SMD parts on the top side (JLC assembly), headers by hand.

Status v0.3 (2026-11-04): schematic, placement, routing, Gerber, JLC BOM/CPL, 3-up panel. ERC: no errors,
DRC: 0 unconnected, no clearance or short errors (only cosmetic items left, see Layout).
**Not built or tested.** Firmware for the MSPM0 (TI MSPM0 SDK, C) is a separate task.

v0.3 is the cost version (JLC quote for 15 boards was 213.63 €; v0.3 is about 150 €, see the cost
note `/mnt/project-files/notes/solar-smart-mppt-v03-cost-2026-11-04.md`):
LTC4412 → discrete ideal diode, ATtiny3226 (89 in stock at JLC) → MSPM0L1306, 2N7002DW → 2 × 2N7002,
MMDT3906 → 2 × MMBT3906, balance resistors 47 Ω → 100 Ω, C9 → 0603, RS1 → RLS12FTCR020: 7 extended
part types with a fee instead of 9.

Decisions: `/mnt/project-files/notes/solar-smart-mppt-phase1-2026-11-03.md` (phase 1) and the
answers in the project thread (phase 2).

## Connections
| Where | Pins | Notes |
|---|---|---|
| J1 SOLAR | + / − | 5–22 V panel (Voc up to 25 V), SS54 against reverse polarity, SMBJ24A TVS |
| J4 BATTERY | BATT+, BM, BATT− | 1S Li-ion / LiFePO4: BATT+ and BATT−, BM open. 2S LTO: BM = middle point |
| J3 NTC | TS / GND | optional 10k NTC (103AT); without it the MCU sets `TS_IGNORE` |
| J6 OUT | + / − | SYS through a discrete ideal diode (Q5 AO3401A + Q9/Q11 MMBT3906): battery or charger voltage, up to 5.6 V with LTO |
| J7 U | + / − | TPS63070 buck-boost, 3.3 V or 5.0 V (JP6), on/off by JP5 |
| J5 I2C host | SCL SDA 3V3 GND | to the repeater; **no pull-ups on this board** (the repeater has them); 3V3 is not used here; PA0/PA1 are 5 V tolerant open-drain pins, and the ROM bootloader (BSL, I2C) listens on them, so the host can also flash the MCU over J5: a blank chip starts the BSL by itself, a programmed one on `smppt cmd bsl`; a chip with broken firmware needs SWD (the BSL invoke pin PA18 is not on J5) |
| TP12, TP13, TP11, TP16, TP17 | 3V3M, GND, SWDIO, SWCLK, NRST | SWD on the **bottom side**, one row at 2.54 mm under the MCU band, left to right seen from the top (a 5-pin header or pogo pins from below) |

## Jumpers
| Jumper | Open | 1-2 | 2-3 |
|---|---|---|---|
| JP4 CHEM | Li-ion (PA23 follows the internal pull) | LiFePO4 (always 0) | LTO 2S (always 1) |
| JP5 EN of J7 | off | always on | MCU PA4 switches it |
| JP6 VSEL of J7 | 3.3 V | closed = 5.0 V | |
| JP3 | ship FET Q2 in use | closed = no ship FET | |

## How it works
- **Charger:** BQ25798, PROG always 1S (R1 4.7k, 750 kHz). With a 2S PROG the chip would start
  with VSYSMIN ≈ 7 V, too much for the 5.5 V parts on SYS. For LTO the MCU writes VREG 5.6 V
  (2.8 V/cell) and only then pulls ~CE low (PA17). ~CE has a pull-up to 3V3M (R4): no MCU, no charging.
  INT is not connected; the MCU polls.
- **3V3M:** HT7533 (2.5 µA) from SYS, only for the MCU and its dividers. VDD 10 µF + 100 nF,
  VCORE 470 nF, NRST 47k + 10 nF (TI minimum circuit).
- **Ideal diode (J6):** Q5 (P-FET, D at SYS, S at J6) conducts SYS → OUT through its body diode
  and is switched fully on by the PNP mirror: Q9 (diode connected, E at SYS) sets the base, Q11
  (E at OUT) pulls the Q5 gate to OUT as soon as OUT rises above SYS, so a load with its own supply
  (USB on a XIAO) cannot feed back into the battery. R43 1 MΩ / R44 470 kΩ: ~4 µA; Q5 is fully on already at 0 V drop (gate ~1.5 V at 3.7 V), and turns off when OUT is ~25 mV above SYS (up to ~0.4 A reverse for that moment).
- **BMS (Q3 HJ8205 in BATT−):** S1 = cell minus, S2 → shunt RS1 → GND.
  OD (discharge, G1) is **on by default** through R18 to BATT+, so SYS starts from the battery alone;
  the MCU turns it off through Q6 (PA26, no MCU pin sees BATT+). OC (charge, G2) is on as soon as
  3V3M is up (R20) and the MCU turns it off (PA10).
  Under-voltage uses the BQ25798 ship mode (Q2, SDRV): sun on VBUS wakes the charger, SYS comes back
  and the MCU starts again. There is **no DW01A**: over-current and short are handled by the MCU
  (OPA + COMP0 with the DAC threshold) and by the BQ25798 BATFET OCP.
- **Shunt:** 20 mΩ 1 % (±3.2 A). R21/R22 (10k) take Kelvin taps from both shunt ends, R23/R24 (1M to 3V3M)
  lift both inputs by ~33 mV so they stay above 0 V while discharging, C24 100 nF across them.
  The GND-end tap is a real Kelvin connection: R22 sits on its own net SH_K, joined to the RS1 GND
  pad by the net tie NT1, so load current in the GND pour does not shift the reading.
  SH_P runs straight down from the MCU on F.Cu with SH_N under it on B.Cu (a pair).
  SH_P → PA25 (OPA0 IN+), SH_N → PA18 (OPA1 IN+). Positive = charging.
  The offset is calibrated at zero current. Firmware: keep the OPA1 output pin off (PA16 is I2C1 SDA)
  and the OPA0 output pin PA22 unused; PA18 is also the BSL pin and sits at ~33 mV at reset (no BSL).
- **Dividers:** VBAT 1M/470k (PA24, A3), BM 1M/1M (PA21, A5), both with 100 nF. Highest ADC input is ~1.79 V
  (VBAT at 5.6 V, 2S LTO) and ~1.4 V (BM), so the firmware uses the internal 2.5 V VREF (1.4 V would clip).
- **CHEM jumper (JP4 → PA23):** read twice, once with the internal pull-up and once with the pull-down.
- **Balancing:** cell 1 (BATT−..BM) through Q7 (PA2) + R37 100 Ω; cell 2 (BM..BATT+) through the P-FET Q8
  + R41 100 Ω, its gate pulled down by Q10 (PA3). Both need push-pull pins. ~40 mA at 4.2 V, 0.18 W per 1206. Only for 2S.
- **LEDs (PA9):** high = red D6 (error), low = yellow D7 (BMS / balancing), Hi-Z = both off
  (1.8 + 1.9 V > 3.3 V, no current).
- **J7:** TPS63070 (2–16 V in), L2 2.2 µH (same part as L1) + 10 µF + 3 × 22 µF (TI table 3: Cout,eff ≥ 10 × L,eff). FB R33/R34 = 3.3 V, VSEL high adds
  R35 in parallel to R34 = 5.0 V. EN, VSEL and PS/SYNC go to VIN only through R30 10k (TI 11.1).
  PS/SYNC high = power save at light load.

## MCU pins (MSPM0L1306SRGER, VQFN-24)
| Pin | Port | Net | Use |
|---|---|---|---|
| 24 / 1 | PA0 / PA1 | SDA_E / SCL_E | I2C0 target (INA260 0x41) → J5, open-drain 5 V tolerant, also the ROM BSL I2C (SDA / SCL) |
| 12 / 11 | PA16 / PA15 | SDA_I / SCL_I | I2C1 controller → BQ25798 (R13/R14 4.7k) |
| 21 / 14 | PA25 / PA18 | SH_P / SH_N | OPA0 / OPA1 IN+ (shunt) |
| 20 / 17 | PA24 / PA21 | ADC_VBAT / ADC_VBM | ADC A3 / A5 |
| 19 | PA23 | ADC_CHEM | JP4 (digital) |
| 13 | PA17 | CE | ~CE low = charge (R4 pull-up) |
| 9 | PA10 | OC_G | R20 pull-up to 3V3M: low = charge FET off (push-pull; driving it high gives the same 3.3 V as R20) |
| 22 | PA26 | OD_CTL | discharge FET off (Q6) |
| 5 / 6 | PA2 / PA3 | BAL1 / BAL2 | balancing |
| 8 | PA9 | LED | D6 / D7 |
| 7 | PA4 | REG_CTL | J7 EN through JP5 2-3 |
| 10 | PA11 | – | not connected |
| 18 | PA22 | – | OPA0 output pin, not connected (keep the OPA0 output off the pin) |
| 15 / 16 / 2 | PA19 / PA20 / NRST | SWD | TP11 / TP16 / TP17 |
| 3 / 23 / 4, EP | VDD / VCORE / VSS | | 3V3M, 470 nF, GND (4 vias in the EP, tented from below) |

The pins are grouped by direction so every pin leaves the package without crossing: up (OC_G, LED, J7 EN),
left (CE, SWD, SH_N, VBM), down (CHEM, VBAT, SH_P, OD, VCORE, J5 SDA), right (balancing, NRST, J5 SCL).

## Layout
- Charger part = the xiao-solar-lite layout (shared cluster `../xiao-solar/tools/charger.py`).
- TPS63070 after the TI layout example: inductor above the IC, SW nodes as short copper, VIN caps left,
  VOUT caps right, PGND and GND joined under the package and taken to two vias.
- Top row: J6 OUT | J5 I2C | J7 U, ideal diode under J6, jumpers JP5/JP6 under J7.
- MCU band under the charger: MSPM0 turned 180°, its fan-out drawn by hand (`MCU_STUBS` in
  `tools/gen_pcb.py`): SWD row and HT7533 on the left, BM / VBAT dividers and JP4 under the MCU,
  balancing FETs Q8/Q10 on the right next to R37/R41.
- Shunt filter (C24, R21–R24) and net tie NT1 at the shunt RS1; SH_P on F.Cu, SH_N under it on B.Cu.
- MLCCs keep ≥ 0.9 mm from the V-cut edges (C21 and C20 turned parallel to the cut); U5, Q5, Q9, D1 pads ≥ 1.5 mm from the left edge.
- BMS, shunt, fuse at the bottom next to J4.

## Files and tools
- `tools/gen_sch.py`: schematic + BOM, writer from `../xiao-solar/tools/gen_sch.py`.
  This board has its own copy of the symbol/footprint library in `lib/` (TPS63070, HT7533 from
  easyeda2kicad); MSPM0, MMBT3906 and 2N7002 use the KiCad standard footprints.
- `tools/gen_pcb.py`: placement, TPS63070 copper, outline, pours, and the locked pre-routes:
  SYS (0.8 mm), the long MCU lines (`PRE_ROUTES`), fixed TPS63070/BQ25798 hand routes
  (`tools/hand_routes.json`, kept from v0.2) and a GND tie (short track + via into the
  B.Cu fill) for every small GND pad that the F.Cu pour cannot reach. `B_CLEAR` boxes keep
  Freerouting off B.Cu where long B lines used to cut the ground fill into islands.
- `tools/patch2.py`: own two-layer grid A* (0.1 mm grid). Used for the pre-routes and for the few
  connections Freerouting leaves open: `python3 tools/patch2.py NET X1 Y1 X2 Y2 [WIDTH]`.
- `tools/gndfix.py`, `tools/bfrag.py`: check for GND copper without a connection and tie it with a via.
- `tools/run.py <tool>` runs a shared tool from `../xiao-solar/tools` on this board:
  `FREEROUTING_JAR=… python3 tools/run.py route`, `… finish`, `python3 tools/run.py export`.
- `panel/`: 3-up panel (`KIKIT=… tools/run.py panel 3`, KiKit 1.8.1 for KiCad 10), 90.2 × 87.7 mm, V-cuts, 5 mm rails.
- `production/`: Gerber zip, `bom-jlc.csv`, `cpl-jlc.csv`, `bom-hand.csv`, schematic PDF, board image, DRC report.

KiCad 10 (kicad-cli + pcbnew Python).

Routing order: `python3 tools/gen_pcb.py` (placement + pre-routes), `tools/run.py route`,
`tools/run.py finish`, `tools/patch2.py` for what is still open, `tools/run.py refill`.
Freerouting is not deterministic, so the committed `.kicad_pcb` is the reference. After the v0.3
route and the finish pass, TS, SYS / VOUT at the ideal diode, SDA_I and SCL_I were closed with
`patch2.py` on it (CE, REGN to C10 and VREG to TP14 are pre-routes in `gen_pcb.py`).

Known layout compromises / DRC leftovers:
- Some tented GND vias sit in the courtyard margin of 0402 parts (never closer than 0.15 mm to a pad).
- Cosmetic DRC items only: teardrop zone overlaps, silk text height and overlap, library footprint
  differences (easyeda2kicad parts), header courtyards, one thermal relief with one spoke.

## Version history
- v0.1 (2026-11-04): first version, 33 × 83 mm, ATtiny3226.
- v0.2 (2026-11-04, not finished, not ordered): top row J6 | J5 | J7, 30 mm wide, fuse, 2 holes.
- v0.3 (2026-11-04): cost version (~150 € for 15 boards): MSPM0L1306, discrete ideal diode,
  basic parts where possible, 30 × 77.5 mm, MCU pins regrouped by direction.
  Review fixes: SWD + NRST row on B.Cu, Kelvin GND for the shunt (NT1), SH_P / SH_N as a pair,
  EP vias tented, MLCCs away from the V-cut. Argon review: R44 470 kΩ (Q5 fully on at 0 V drop),
  L2 2.2 µH = same part as L1 (one extended type less, L2 had only 944 in stock), R4 / R6 in 0402
  (BOM 43 → 40 lines).
  CPL 2026-11-11: LEDs D4 / D6 / D7 turned 180° (JLC preview showed them reversed; no LED
  offset for this board in tools/run.py).
  2026-11-13: J5 host I2C moved from PA10/PA11 to PA0/PA1 (5 V tolerant, ROM BSL I2C: flash over J5), OC_G on PA10;
  Q3 (HJ8205) CPL rotation +270 (`tools/run.py`, JLC model has its pins in two columns; checked against the
  EasyEDA footprint of C20069150); `run.py export` now really applies the run.py overrides (LED fix was lost on export).
  2026-11-13: U5 (HT7533, turned 270°), Q5, Q9, Q11, C20 (turned parallel to the cut) and D1 moved so that pads and body of U5, Q5, Q9, C20 and D1 stay ≥ 1.5 mm from the left V-cut edge (pads now ≥ 1.53 mm; C21 1.38 mm, right edge R35 / C38 1.28 mm unchanged).
