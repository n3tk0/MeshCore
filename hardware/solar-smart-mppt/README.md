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
- shows errors and BMS state on two LEDs on one pin,
- switches a battery heater (J8, low-side AO3400A, PWM from PA11),
- has the BQ25798 interrupt (INT) on PA22 with a 10k pull-up; **the copper path from U1.21 to PA22 is a wire on the v0.4 board** (see Open items: TP18 to R47.2).

30 × 79.35 mm (three across a 100 mm JLC panel), 2 layers, all SMD parts on the top side (JLC assembly), headers by hand.

Status v0.4 (2026-11-16, v0.3 is ordered, v0.4 is the DFM and silkscreen revision of it, same circuit): schematic, placement, routing, Gerber, JLC BOM/CPL, 3-up panel. ERC: no errors,
DRC: no clearance or short errors, 0 unconnected. HEAT is routed. **One connection is a wire on this revision: BQ25798 INT (U1.21) ends at the test pad TP18, the MCU side (U4.18 / R47.2) is a separate net named INT, see "Open items".**
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
| J8 HEAT | + / − | battery heater, resistive, up to ~2.5 A: **+ = SYS**, **− = drain of Q12** (AO3400A, low side, source on GND); PA11 drives the gate through R45 100 Ω, R46 100k to GND keeps the heater off while the MCU is in reset or unpowered. No flyback diode (resistive load; xiao-solar v0.5 has none either). |
| J5 I2C host | SCL SDA 3V3 GND | to the repeater; **no pull-ups on this board** (the repeater has them); 3V3 is not used here; PA0/PA1 are 5 V tolerant open-drain pins, and the ROM bootloader (BSL, I2C) listens on them, so the host can also flash the MCU over J5: a blank chip starts the BSL by itself, a programmed one on `smppt cmd bsl`; a chip with broken firmware needs SWD (the BSL invoke pin PA18 is not on J5) |
| TP12, TP13, TP11, TP16, TP17 | 3V3M, GND, SWDIO, SWCLK, NRST | SWD on the **bottom side**, one row at 2.54 mm under the MCU band, left to right seen from the top (a 5-pin header or pogo pins from below) |

## Jumpers
| Jumper | Open | 1-2 | 2-3 |
|---|---|---|---|
| JP4 CHEM | Li-ion (PA23 follows the internal pull) | LiFePO4 (always 0) | LTO 2S (always 1) |
| JP5 EN of J7 | off | always on | MCU PA4 switches it |
| JP6 VSEL of J7 | 3.3 V | closed = 5.0 V | |
| JP3 | ship FET Q2 in use (Li-ion, LTO) | closed = no ship FET: **use it for LiFePO4** (with a ship FET the BQ25798 starts from the battery only above ~3.40 V and stops below ~3.20 V, SLUSDV2C 6.5; without it 2.6 / 2.4 V) | |

**Ordered v0.3 boards: the jumper silkscreen is misleading (fixed in v0.4).** "ON" sits next to JP5 pads 2-3 (that is MCU) and
"MCU" is pushed to the left; "LFP/LTO" sit next to Q7, ~15 mm from JP4. Go by the pads: JP5 pad 1 is on the left,
JP4 pad 1 (GND, LiFePO4 side) is on the left. v0.4 prints the setting of every jumper next to its pads (`tools/labels.py`; the positions are checked against the pad coordinates by script).

## How it works
- **Charger:** BQ25798, PROG always 1S (R1 4.7k, 750 kHz). With a 2S PROG the chip would start
  with VSYSMIN ≈ 7 V, too much for the 5.5 V parts on SYS. For LTO the MCU writes VREG 5.6 V
  (2.8 V/cell) and only then pulls ~CE low (PA17). ~CE has a pull-up to 3V3M (R4): no MCU, no charging.
  INT (open drain, low = event) goes to PA22 with a 10k pull-up to 3V3M (R47); the firmware can wake on it or poll. On the v0.4 board the BQ side (net INT_BQ, TP18) is joined to R47.2 / U4.18 by a wire; without the wire PA22 just reads high and the firmware polls the BQ25798 over I2C.
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
- **Shunt:** 20 mΩ 1 %, PGA ×16. R21/R22 (10k) take Kelvin taps from both shunt ends, R23/R24 (470k to 3V3M)
  lift both inputs by 3.3 V × 10k / 480k ≈ **69 mV** (v0.3: 1M, 33 mV) so they stay above 0 V while discharging, C24 100 nF across them.
  Discharge is visible up to 69 mV / 20 mΩ ≈ **3.4 A** (v0.3: 1.6 A), charge up to ~4.3 A (PGA output at the 2.5 V reference).
  The GND-end tap is a real Kelvin connection: R22 sits on its own net SH_K, joined to the RS1 GND
  pad by the net tie NT1, so load current in the GND pour does not shift the reading.
  SH_P runs straight down from the MCU on F.Cu with SH_N under it on B.Cu (a pair).
  SH_P → PA25 (OPA0 IN+), SH_N → PA18 (OPA1 IN+). Positive = charging.
  The offset is calibrated at zero current. Firmware: keep the OPA1 output pin off (PA16 is I2C1 SDA)
  and the OPA0 output-pin switch off (PA22 is a GPIO input for INT); PA18 is also the BSL pin and sits at ~69 mV at reset (no BSL).
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
| 10 | PA11 | HEAT | heater gate (Q12) through R45 100 Ω, R46 100k pull-down; GPIO or TIMG4_CCP1 for PWM |
| 18 | PA22 | INT | BQ25798 ~INT, open drain, R47 10k to 3V3M (GPIO input; it is the OPA0 output pin, keep that output switch off) |
| 15 / 16 / 2 | PA19 / PA20 / NRST | SWD | TP11 / TP16 / TP17 |
| 3 / 23 / 4, EP | VDD / VCORE / VSS | | 3V3M, 470 nF, GND (4 vias in the EP, tented from below) |

The pins are grouped by direction so every pin leaves the package without crossing: up (OC_G, HEAT, LED, J7 EN),
left (CE, SWD, SH_N, VBM, INT), down (CHEM, VBAT, SH_P, OD, VCORE, J5 SDA), right (balancing, NRST, J5 SCL).

## Layout
- Charger part = the xiao-solar-lite layout (shared cluster `../xiao-solar/tools/charger.py`).
- TPS63070 after the TI layout example: inductor above the IC, SW nodes as short copper, VIN caps left,
  VOUT caps right, PGND and GND joined under the package and taken to two vias.
- Top row: J6 OUT | J5 I2C | J7 U with JST XH housings (J6 3.33, J5 11.19, J7 24.13 along y = 0.5; the board edge is at y = -1.85, so the housings end exactly at the V-cut, no overhang; 0.5 mm between the housings), ideal diode under J6, jumpers JP5/JP6 under J7. H1 (M2) does not fit in the row (J5 + J7 housings leave 0.5 mm only between J6 and J5 / J5 and J7 for the housing, the hole needs a 4 mm screw head keepout): it sits just under the row between J5 and J7 at (21.45, 6.4), 0.51 mm from the J5 and J7 housings. TP14 (VREG) at (25.0, 6.0).
- Heater: J8 sits on the right edge next to the mounting hole H3 (pins (28.5, 34.53) and (28.5, 37.07), housing toward the board with 0.85 mm overhang over the V-cut like J1), Q12 (27.2, 42.7, rot 90), R45 (26.25, 45.8, 0402, C25076) and R46 (27.55, 45.8) below it, R37 (23.8, 40.2) moved 0.4 mm left. The SYS feeder to J8.1 is 0.8 mm F.Cu from the B.Cu SYS lane (0.8 mm, x = 22.0) through two 0.8 / 0.4 vias at (22.0, 33.85) and (22.0, 34.65); narrowest copper on the whole SYS path to J8.1 is 0.8 mm (no track or lane narrower, no pour neck). The drain run Q12.3 to J8.2 is 0.8 mm F.Cu (5.2 mm); the Q12 source (pad 2, solid pour connection) sits in the F.Cu GND pour with GND vias at (29.0, 43.0), (29.0, 44.6) and (29.0, 46.0). The REG_CTL lane that used to run there is re-laid (0.2 mm, F.Cu / B.Cu / F.Cu, two vias (23.45, 36.3) and (23.45, 32.7)). The bottom edge is free again (H2 (2.8, 74.5), FID3, TP7 stay where they were; the space J8 used is not needed by anything). HEAT (U4.10 to R45.1) is 56.8 mm, 4 vias, 0.2 mm, see Open items.

## Open items (v0.4)
- **INT is not routed on the board (bodge wire).** BQ25798 pin 21 sits between the PROG lane and the 0.6 mm BAT lane with C17 / C18 / R1 beside it, and the MCU pin 18 (left, below the SWD / sense / CE lanes) is boxed in by the SWD / sense / I2C lanes; both ends are in closed pockets. Tried: MCU pin swaps (every pin that can reach the BQ pocket needs its signal moved to pin 18, which only a bottom-right signal could use and cannot reach), the A* router with the blocking nets released (INT alone finds a 22 mm zig-zag, but then CE / SH_N / SDA_I find no path), partial and full Freerouting runs (33 unconnected after a full re-route), a longer board (the walls are lanes, not board size). U1.21 therefore ends at TP18 (0.6 mm pad, F.Cu, (11.33, 40.1), net INT_BQ); the MCU side keeps the net INT with R47. A 0.2 mm wire from TP18 to R47.2 (or U4.18) on the first sample joins them; PA22 works as polled input without it. Round 3 (owner request: INT on B.Cu vias from TP18 / U1.21 to U4.18 / R47.2, 0.2 mm, no GND island, crossing rather than running under the charger / TPS63070 switch node, nets merged): not possible on this layout. The A* router (0.05 mm grid, 0.15 mm clearance, B.Cu preferred) finds no path out of the TP18 pocket (x 9.2-12.2, y 33.7-43.5): walled by the BAT B.Cu lanes (x 10.1-11.9, y 43-44.4), the SDA_I F.Cu / B.Cu lanes, the SCL_I B.Cu diagonal, the F.Cu BAT / SYS zones and the charger SW1 / SW2 B.Cu zones. Smallest fix that would open it: re-lay the 0.6 mm BAT B.Cu lane and the SCL_I diagonal (about 15 mm of copper) or move SDA_I / SCL_I to other MCU pins, then INT can leave TP18 downwards (x 11.3) to R47.2 with two vias. INT_BQ and INT therefore stay two nets (TP18 and a wire). Round 4 (owner decision: smallest fix, INT on B.Cu with two vias, nets merged): the smallest fix named above is not enough. Re-laying the BAT lane and the SCL_I diagonal opens the TP18 pocket, but the B.Cu column below it (x 10.9-11.3, y 44-55) is also used by the SDA_I / SCL_I B.Cu hops, the SH_N and 3V3M B.Cu lanes and the R13 / R14 pull-up cluster. A full rip-up (BAT, SH_N, 3V3M, SDA_I, SCL_I, R13 / R14 swapped) routes INT in 15.3 mm with 2 vias, but then the I2C1 north lanes and the TP9 / TP10 links find no clean path (SCL_I R14.2 to the test pad TP9 has no path; SDA_I to TP10 needs 21 mm and 2 vias) and the BAT lane would have to be re-laid at 0.6 mm with extra layer changes. Not done: INT_BQ and INT stay two nets (TP18 and a wire, as in round 3). Options: pin swap SCL_I / SDA_I with another I2C-capable pair (firmware change), move test points TP9 / TP10, or keep the wire.
- **HEAT lane (round 4):** with J8 at the right edge the gate signal (U4.10, PA11) to R45.1 is 56.8 mm long with 4 vias (0.2 mm, B.Cu / F.Cu hops); a short lane is not possible: the region north-east of the MCU is enclosed by the 0.6 mm BAT lane, the REG_CTL F.Cu lane and the SDA_E / SCL_E B.Cu bundle (A* with the 0.05 mm grid finds nothing shorter). Resistive load, DC / slow PWM only, so it works; to get it short either swap PA11 with a pin on the right side of the MCU (firmware change, pin map above) or re-lay the BAT / REG_CTL lanes (owner decision).
- **J1 housing overhang:** J1 pins are 1.5 mm from the left V-cut (pad edge 0.65), the XH housing overhangs the edge by 0.85 mm (same for J8 on the right edge); J3 housing overhangs by 0.05 mm (pins 2.3 mm from the cut). The SMD >= 3.0 mm rule is met by shifting the VBUS cluster right: D3 +0.3, R6 +0.25, C2 +0.3, D4 +0.25, C1 +0.35 (courtyards stay clear), TP4 1.85 mm down; smallest SMD pad to THT pad distance is now 3.02 mm (D3.1 to J1.1).
- **JST XH housings** (F.Fab outlines on J1, J3, J4, J5, J6, J7, J8; drawn by `xh_housings()` in `tools/gen_pcb.py`): width (pins - 1) * 2.5 + 4.9 mm, 5.75 mm deep, 2.45 mm beyond the end pins, 2.35 mm from the pin line towards the nearer board edge and 3.4 mm to the inside. Assumption: the 2.35 mm side faces the board edge (check against the JST XH datasheet and the chosen part before ordering). Clearances (mm, outline to outline): J5-J6 0.50, J5-J7 0.50, all other housing pairs > 3 (J8 to the next housing J4 > 30); housing to screw-head keepout (r = 2.0 around the hole): J5-H1 0.51, J7-H1 0.51, J8-H3 0.10, others > 4; housing to SMD pad (no pad under any housing): J8-R37.2 0.43, J1-D3 0.50, J3-R3.2 0.64, J3-JP3 0.65, J4-Q3 0.71, all others > 1. Overhang: J1 0.85 left, J8 0.85 right, J3 0.05 left, none elsewhere. J8 pins are 1.5 mm from the right V-cut; the + / - labels sit 1.9 mm left of the pads and HEAT 2.9 mm left (rotated), the version text moved to the top row (14.5, 4.3).
- Silkscreen: every text is >= 0.8 mm high and every silk line >= 0.15 mm wide. Reference designators of R14, R20, R28, R42, R45, R46, C9, C29, C33, C36, C37, C39, D6, Q12, RS1, TP14, TP18, J7, J8, H1 do not fit at 0.8 mm next to their parts and are hidden (J7 and J8 have the +/- and name labels).
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
- `tools/gen_pcb.py`: placement, TPS63070 copper, outline, pours, and the locked pre-routes: `tools/v04_delta.json`: the round 3 copper (tracks and vias added / removed against the round 2 board), applied by `add_v04_delta()`; the committed `.kicad_pcb` stays the reference (see below).
  SYS (0.8 mm), the long MCU lines (`PRE_ROUTES`), fixed TPS63070/BQ25798 hand routes
  (`tools/hand_routes.json`, kept from v0.2) and a GND tie (short track + via into the
  B.Cu fill) for every small GND pad that the F.Cu pour cannot reach. `B_CLEAR` boxes keep
  Freerouting off B.Cu where long B lines used to cut the ground fill into islands.
- `tools/patch2.py`: own two-layer grid A* (0.1 mm grid). Used for the pre-routes and for the few
  connections Freerouting leaves open: `python3 tools/patch2.py NET X1 Y1 X2 Y2 [WIDTH]`.
- `tools/gndfix.py`, `tools/bfrag.py`: check for GND copper without a connection and tie it with a via.
- `tools/run.py <tool>` runs a shared tool from `../xiao-solar/tools` on this board:
  `FREEROUTING_JAR=… python3 tools/run.py route`, `… finish`, `python3 tools/run.py export`.
- `panel/`: 3-up panel (`KIKIT=… tools/run.py panel 3`, KiKit 1.8.1 for KiCad 10), 90.2 × 89.6 mm, V-cuts, 5 mm rails.
- `production/`: Gerber zip, `bom-jlc.csv`, `cpl-jlc.csv`, `bom-hand.csv`, schematic PDF, board image (`solar-smart-mppt-v0.4.png`), DRC report.

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
- v0.4 (2026-11-16): DFM and silkscreen revision of the ordered v0.3, same 30 × 77.5 mm, same 3-up panel; circuit changes (second round, owner decisions): R23 / R24 1M -> 470k (C25790, 0402, preferred; shunt bias 33 -> 69 mV, discharge visible to 3.4 A instead of 1.6 A, charge to ~4.3 A), BQ25798 INT on PA22 (R47 10k pull-up to 3V3M, C25744 basic), battery heater output J8 HEAT (SYS +, Q12 AO3400A C20917 low side, R45 100 Ω, R46 100k, PA11), all silkscreen text >= 0.8 mm / lines >= 0.15 mm. HEAT is routed; INT ends at the test pad TP18 (U1.21) and the MCU side is joined by a wire (see Open items). H1 stays at (27.6, 6.4): between J5 and J7 the J8 header (6.17 mm) and the hole courtyard (5 mm) do not fit in the 5.95 mm gap.
  Round 3 (2026-11-17, owner review): JST XH housings on all THT connectors (see Open items); board 30 × 79.35 mm (top edge at y = -1.85, so that the top-row housings end at the edge; width unchanged, panel 3-up still fits the 100 mm JLC rule); top row J6 | J5 | J7 with H1 under the row; heater J8 + Q12 / R45 (0402, C25076) / R46 at the bottom next to J4, H2 (2.8, 74.5), FID3, TP7 moved; J1 (1.5, 22.5), J3 (2.3, 43.5); HEAT re-routed (98 mm); labels rebuilt (+ / - and names inside the housings, J8 at the bottom). INT stays a wire (not routable, see Open items). DRC: 0 unconnected, no clearance / short / hole / courtyard error; ERC: no errors.
  Round 4 (2026-11-18, owner review): heater J8 / Q12 / R45 / R46 moved from the bottom edge to the right edge next to H3 (HEAT gate 56.8 mm, 4 vias instead of 98 mm / 8 vias), SYS feed 0.8 mm with two vias, VBUS cluster (D3, R6, C2, D4, C1, TP4) shifted so every SMD pad is >= 3.0 mm from J1 (min 3.02), R37 0.4 mm left, version text to the top row. INT stays a wire (the owner's smallest fix does not close, see Open items). DRC: 0 unconnected, no clearance / short / hole / courtyard error, 51 baseline items (9 lib_footprint_mismatch, 4 silk_overlap, 1 track_dangling, 37 zones_intersect); ERC: no errors.
  DFM: every SMD pad >= 3.0 mm from the THT pads of J1, J3, J4, J5, J6, J7 (was 1.52 mm at F1-J4); every SMD
  pad >= 1.5 mm from all four edges (R35, C38, C21 and the JP5 pads were 1.2-1.4 mm); MLCC long sides parallel
  to the edge. Moves: J6, J5, J7 to y = 1.5 (J5 now left of the hole), J1, D3, R6, J3, C21, F1, R35, R33, C36-C38,
  JP5, JP6; the touched nets were re-routed.
  Mounting holes: H1 moved between J5 and J7 (20.5, 3.0); new third M2 hole H3 under JP5 / JP6 (27.0, 30.0).
  Silkscreen: + / - at every pad of J1, J4, J6, J7; JP4 "CHEM" with LFP / LTO, JP5 "J7 EN" with ON (pads 1-2) and
  MCU (pads 2-3), JP6 "J7 5V", JP3 "NO SHIP FET" / "LFP: close", each next to its pads (jumper reference
  designators hidden); "v0.4" on the board and in the schematic title block.
