# solar-smart-mppt: MPPT charger + BMS 1S/2S + coulomb counter

A stand-alone power board for a solar repeater (any board: RAK, XIAO, ProMicro…), grown out
of [`../xiao-solar-lite`](../xiao-solar-lite): the same BQ25798 MPPT buck-boost charger, but the
XIAO stack is gone and an **ATtiny3226** supervisor runs everything:

- drives the BQ25798 on its own I2C bus (software master on PC1/PC2),
- shows itself to the repeater as an **INA260 on 0x41** (hardware TWI slave on PB1/PB0 → J5),
  so MeshCore reads voltage, current and charge with `ENV_INCLUDE_INA260` and no firmware change,
- counts the charge through a 20 mΩ shunt with its 12-bit differential ADC (PGA 16×),
- is the BMS: discharge FET (OD) and charge FET (OC) of an HJ8205 in the battery minus,
- balances a 2S LTO pack (~60 mA per cell),
- shows errors and BMS state on two LEDs on one pin.

33 × 83 mm (three across a 100 mm JLC panel), 2 layers, all SMD parts on the top side (JLC assembly), headers by hand.

Status v0.1 (2026-11-04): schematic, placement, routing, Gerber, JLC BOM/CPL, 3-up panel. ERC: no errors.
DRC: 0 unconnected, no clearance errors (only cosmetic items, see below). **Not built or tested.**
Firmware for the ATtiny is a separate task.

Decisions: `/mnt/project-files/notes/solar-smart-mppt-phase1-2026-11-03.md` (phase 1) and the
answers in the project thread (phase 2).

## Connections
| Where | Pins | Notes |
|---|---|---|
| J1 SOLAR | + / − | 5–22 V panel (Voc up to 25 V), SS54 against reverse polarity, SMBJ24A TVS |
| J4 BATTERY | BATT+, BM, BATT− | 1S Li-ion / LiFePO4: BATT+ and BATT−, BM open. 2S LTO: BM = middle point |
| J3 NTC | TS / GND | optional 10k NTC (103AT); without it the ATtiny sets `TS_IGNORE` |
| J6 OUT | + / − | SYS through an LTC4412 ideal diode (Q5 AO3401A): battery or charger voltage, up to 5.6 V with LTO |
| J7 U | + / − | TPS63070 buck-boost, 3.3 V or 5.0 V (JP6), on/off by JP5 |
| J5 I2C host | SCL SDA 3V3 GND | to the repeater; **no pull-ups on this board** (the repeater has them); 3V3 is not used here |
| TP11–TP13 | UPDI, 3V3M, GND | ATtiny programming (UPDI) |
| TP15 | PA2 | spare ATtiny pin |

## Jumpers
| Jumper | Open | 1-2 | 2-3 |
|---|---|---|---|
| JP4 CHEM | Li-ion (ADC reads ½ 3V3M) | LiFePO4 (0 V) | LTO 2S (3V3M) |
| JP5 EN of J7 | off | always on | ATtiny PA3 switches it |
| JP6 VSEL of J7 | 3.3 V | closed = 5.0 V | |
| JP3 | ship FET Q2 in use | closed = no ship FET | |

## How it works
- **Charger:** BQ25798, PROG always 1S (R1 4.7k, 750 kHz). With a 2S PROG the chip would start
  with VSYSMIN ≈ 7 V, too much for the 5.5 V parts on SYS. For LTO the ATtiny writes VREG 5.6 V
  (2.8 V/cell) and only then pulls ~CE low. ~CE has a pull-up to 3V3M (R4): no ATtiny, no charging.
  INT is not connected (pin 21 sits between PROG and BAT at 0.4 mm pitch); the ATtiny polls.
- **3V3M:** HT7533 (2.5 µA) from SYS, only for the ATtiny and its dividers.
- **BMS (Q3 HJ8205 in BATT−):** S1 = cell minus, S2 → shunt RS1 → GND.
  OD (discharge, G1) is **on by default** through R18 to BATT+, so SYS starts from the battery alone;
  the ATtiny turns it off through Q6 (no MCU pin sees BATT+). OC (charge, G2) is off by default
  (R20) and the ATtiny turns it on (PB2).
  Under-voltage uses the BQ25798 ship mode (Q2, SDRV): sun on VBUS wakes the charger, SYS comes back
  and the ATtiny starts again. There is **no DW01A**: over-current and short are handled by the ATtiny
  (ADC + analog comparator AC0, whose inputs are the shunt pins PA6/PA7) and by the BQ25798 BATFET OCP.
- **Shunt:** 20 mΩ (±3.2 A). R21/R22 (10k) take Kelvin taps from both shunt ends, R23/R24 (1M to 3V3M)
  lift both ADC inputs by ~33 mV so they stay above 0 V while discharging. Positive = charging.
  The offset is calibrated at zero current (see the phase 1 note, section 8).
- **Dividers:** VBAT 1M/470k (PA4), BM 1M/1M (PA5), CHEM jumper 1M/1M (PA1), all with 100 nF.
- **Balancing:** cell 1 (BATT−..BM) through Q7 FET1 + R37 47 Ω; cell 2 (BM..BATT+) through the P-FET Q8
  + R41 47 Ω, its gate pulled down by Q7 FET2. Only for 2S.
- **LEDs:** PC3 high = red D6 (error), low = yellow D7 (BMS / balancing), Hi-Z = both off
  (1.8 + 1.9 V > 3.3 V, no current).
- **J7:** TPS63070 (2–16 V in), L2 1.5 µH + 3 × 22 µF (TI table 3). FB R33/R34 = 3.3 V, VSEL high adds
  R35 in parallel to R34 = 5.0 V. EN, VSEL and PS/SYNC go to VIN only through R30 10k (TI 11.1).
  PS/SYNC high = power save at light load.

## Layout
- Charger part = the xiao-solar-lite layout (shared cluster `../xiao-solar/tools/charger.py`), 14 mm lower.
- TPS63070 after the TI layout example: inductor above the IC, SW nodes as short copper, VIN caps left,
  VOUT caps right, PGND and GND joined under the package and taken to two vias.
- The ATtiny sits upright under the outputs: pins 1–10 (VDD, ADC, BMS) to the left, 11–20 to the right.
- BMS, shunt and balancing at the bottom between the SYS caps and J4.

## Files and tools
- `tools/gen_sch.py`: schematic + BOM, writer from `../xiao-solar/tools/gen_sch.py`.
  This board has its own copy of the symbol/footprint library in `lib/` (new: TPS63070, LTC4412,
  ATtiny3226, HT7533, 2N7002DW from easyeda2kicad).
- `tools/gen_pcb.py`: placement, TPS63070 copper, outline, pours, and the locked pre-routes:
  SYS (0.8 mm), the long ATtiny lines (`PRE_ROUTES`) and a GND tie (short track + via into the
  B.Cu fill) for every small GND pad that the F.Cu pour cannot reach. `B_CLEAR` boxes keep
  Freerouting off B.Cu where long B lines used to cut the ground fill into islands.
- `tools/patch2.py`: own two-layer grid A* (0.1 mm grid). Used for the pre-routes and for the few
  connections Freerouting leaves open: `python3 tools/patch2.py NET X1 Y1 X2 Y2 [WIDTH]`.
- `tools/gndfix.py`, `tools/bfrag.py`: check for GND copper without a connection and tie it with a via.
- `tools/run.py <tool>` runs a shared tool from `../xiao-solar/tools` on this board:
  `FREEROUTING_JAR=… python3 tools/run.py route`, `… finish`, `python3 tools/run.py export`.
- `panel/`: 3-up panel (`tools/run.py panel`, KiKit 1.6), 99.2 × 93.2 mm, V-cuts, 5 mm rails.
- `production/`: Gerber zip, `bom-jlc.csv`, `cpl-jlc.csv`, `bom-hand.csv`, schematic PDF, board image, DRC report.

KiCad 10 (kicad-cli + pcbnew Python).

Routing order: `python3 tools/gen_pcb.py` (placement + pre-routes), `tools/run.py route`,
`tools/run.py finish`, `tools/patch2.py` for what is still open, `tools/run.py refill`.
Freerouting is not deterministic, so the committed `.kicad_pcb` is the reference; the last six
patches (SYS, BATT+ sense, REGN, SDA_I, 3V3M to R23, ADC_VBAT) were made with `patch2.py` on it.

Known layout compromises (v0.1):
- 3V3M to R23 (shunt bias, 1 MΩ) and ADC_VBAT (divider to PA4, 100 nF at the divider) take
  long detours (~27 mm, 4 vias each). Both are DC and high impedance, so it does not matter for
  the readings, but a v0.2 should move R23–R26 next to the ATtiny pins.
- Some tented GND vias sit in the courtyard margin of 0402 parts (never closer than 0.15 mm to a pad).
- DRC leftovers are cosmetic: teardrop zone overlaps, silk text height, library footprint
  differences (easyeda2kicad parts), header courtyards.

## Version history
- v0.1 (2026-11-04): first version, 33 × 83 mm.
