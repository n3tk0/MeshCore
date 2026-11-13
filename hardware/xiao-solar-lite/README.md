# xiao-solar-lite: solar charger layer for XIAO + Wio-SX1262

The power part of [`../xiao-solar`](../xiao-solar) as the third layer of the standard
XIAO nRF52840 + Wio-SX1262 sandwich: this board at the bottom, Wio in the middle, XIAO on top
(USB and buttons stay reachable). Solar input, BQ25798 MPPT buck-boost charger, optional NTC,
ship FET, and DW01A + HJ8205 battery protection (BMS).
23 × 64.1 mm, 2 layers, all SMD parts on the top side (JLC assembly), no mounting holes.

The stack sits over the 14 XIAO holes at the USB end; only flat parts (two 0603 pull-ups, in v0.5 also the SOT-23 ideal diode, R30/R31 and C20) are
under it, since the header plastic leaves ~2.5 mm and the inductor is 3 mm tall. The charger
and BMS sit below the stack. Long pin headers go through all three boards.

Status v0.5: schematic, routed PCB, Gerber, JLC BOM/CPL. Not built or tested.

v0.3 (review 2026-10-29): charger zone redone after the BQ25798 layout guide (SLUSDV2C 8.4.1):
PMID, SYS, VBUS and BAT are copper areas, SW1/SW2 run short and wide to the inductor, the 100 nF
PMID/SYS caps sit at the pins, BTST/REGN/GND leave through vias under the package. Cap counts as
TI recommends (VBUS 2, PMID 3, SYS 5, BAT 2 × 10 µF), so the board grew by 4 mm. Small parts get
thermal spokes to GND (easier hand rework), the charger power caps stay solid.
Every layer change on a power net has two vias (no single-via hot spot), and no via sits in a pad.

v0.4 (2026-10-30, Argon review + v0.4 proposals, shared with the main board):
- Charger per TI 8.4.1: 0402 100 nF right over the PMID–GND(27) and SYS–GND(27) pins on top,
  SW1/SW2 drop through 3 vias each under the IC to short B.Cu copper to L1, pin 27 to several vias.
  L1 is a molded **MWSA0402S-2R2MT** (Isat 4 A, DCR ≤ 58 mΩ, less stray field near the radio).
- **JP3** solder jumper across the ship FET Q2: closed = no ship FET, for LiFePO4 (with the ship
  FET the BQ25798 starts above 3.40 V and stops below 3.20 V; without it 2.6 / 2.4 V).
- **DW01A + HJ8205 BMS** (`U2`, `Q3`, `R9`, `R10`, `C19`) on the low side of the battery, matching
  the main board (`2.4 V / 4.3 V` backup protection).
- **Optional NTC** (`J3` + `R2/R3 = 5.1k/30k`), matching the main board: firmware sets `TS_IGNORE`
  by default when no NTC is fitted; CLI command enables the 0–60 °C NTC window.
- D5 was an **LM66100** ideal diode (~30 mV drop instead of ~0.3 V), replaced in v0.5 (see below); D1 is **SS54**.
- DFM: no vias in SMD courtyards except the tented ones under U1 (v0.5: and the two OR_B vias at Q7 / Q6), 4 mm GND via grid + 3 mm edge
  fence, GND vias at caps and signal vias, B.Cu kept whole under the charger, teardrops,
  3 fiducials, test points VBUS/PMID/SYS/BAT/GND/3V3/SCL/SDA, parts ≥ 1 mm from the edge,
  silk ≥ 0.8 mm without overlaps (some refs in tight spots are hidden, they stay on F.Fab).
- Rotations in the CPL: U1, U2, Q2, D1, D3, D4 agree with JLC's footprints. Q3 (+270, the HJ8205 has its
  pins in two columns in JLC's footprint) and the SOT-23 parts (+180) are confirmed by JLC's DFM view
  (2026-11-13); D5 is gone.

v0.5 (2026-11-13):
- **Discrete ideal diode instead of the LM66100** (same circuit as the xiao-solar main board, no extended-part
  fee): `Q5` AO3401A (C15127) in the SYS to VOUT path, mirror `Q6` / `Q7` MMBT3906 (C7420354),
  `R30` 1M and `R31` 470k (0603). The power path is 0.8 mm copper (SYS) and 0.6 mm (VOUT); the mirror
  lines are 0.25 mm tracks, OR_B changes to B.Cu once (two vias beside Q7 / Q6, not in pads).
- **All SMD pads are at least 3.0 mm (copper to copper) from every pin of the headers** (U3, J1, J3, J4, J5),
  as JLC's DFM asked on the main board. The charger and everything below it moved down 0.4 mm, the
  connector row 2.1 mm, R2/R3 0.9 mm to the right, so the board is now **23 × 64.1 mm** (was 62).
- The ideal diode (Q5, Q6, Q7, R30, R31) and C20 sit in the free window between the two XIAO header rows, with
  R11 / R12 (flat SOT-23 and 0603 parts, 1.2 mm high at most, under the 2.5 mm header plastic). SYS reaches
  Q5 on a 0.8 mm track up the right edge.
- C9 is the basic C307331 (100n/50V 0402).
- Hand patches on the routed board that `gen_pcb.py` does not reproduce: the 3V3 link to U3 pin 12
  on B.Cu (with one via at 8.0, 16.9), two GND vias (3.3, 43.4 and 2.1, 31.0) and two shortened stubs
  (REGN, SCL).

## Connections
| Where | Pins | Notes |
|---|---|---|
| XIAO stack (`U3`) | D0 = CE, D6 = SCL, D7 = SDA, 3V3, GND, 5V | 5V gets SYS (3.5–4.4 V) through the Q5/Q6/Q7 ideal diode (AO3401A + PNP mirror), so the XIAO USB cannot feed SYS. Other pins pass through. **No battery on the XIAO itself.** |
| J1 SOLAR | + / − | 5–22 V panel (Voc up to 25 V), SS54 against reverse polarity, SMBJ24A TVS |
| J3 NTC | TS / GND | Optional 10k NTC (103AT, B = 3435); leave open if unused (`TS_IGNORE` on by default) |
| J4 BAT | + / − | 1S cell (protected by onboard DW01A + HJ8205 BMS and ship FET Q2) |
| J5 I2C | SCL SDA 3V3 GND | external I2C sensors (4.7k pull-ups on board, 3V3 from the XIAO) |

## Charger settings
- **CE / JP2:** CE is XIAO D0. 1-2 (default) = Li-ion, charges on its own with the BQ defaults
  (4.2 V, 1 A) even while the XIAO is off; 2-3 = LiFePO4, the pull-up to 3V3 holds charging off until firmware has
  set VREG 3.6 V and pulls CE low.
- **TS / NTC (J3):** R2/R3 (5.1k/30k) for a 10k NTC (103AT, B = 3435) on J3 (~0–60 °C window).
  Without NTC, TS sits at ~85 % of REGN ("cold"), so firmware enables `TS_IGNORE` by default.
- **Charge current:** up to 2 A (firmware sets ICHG; 2.2 µH 4 A molded inductor). PROG 4.7k = 750 kHz, 1S.
- **STAT LED** (green) lights while charging; QON test pad TP2 to GND wakes the charger from ship mode.
- **INT** is not brought out: firmware polls the status registers.
- **Ship FET & BMS:** Q2 (AO3400A, S to BAT, D to the battery) is driven by SDRV only after firmware
  sets SFET_PRESENT (REG0x14 bit 7); ship and shutdown modes need it. For LiFePO4 leave Q2 off and
  close JP3; firmware then keeps SFET_PRESENT at 0. DW01A (`U2`) + HJ8205 (`Q3`) sits in the negative
  battery lead (`BATT-` to `GND`).
- **After a charger POR** (registers back at defaults) firmware writes EN_MPPT=1, WATCHDOG=0,
  SFET_PRESENT (only with Q2) and VREG again.

## JLC
26 BOM lines, 3 extended parts with a $3 fee (BQ25798, MWSA0402S inductor, DW01A);
SMBJ24A and HJ8205 are preferred extended parts (no fee), everything else is basic (since 2026-11-13
C9 100n/50V 0402 is the basic C307331). Parts cost about $4.5 per board (not recalculated for v0.5). Headers are
hand-soldered (`production/bom-hand.csv`).

Panel: `panel/` holds 4 boards on 92×74.1 mm (V-cut, 5 mm rails with tooling holes and fiducials),
made by `python3 tools/run.py panel` (KiKit; Gerber/BOM/CPL in `panel/production/`). Order it as
"Panel by Customer" with V-cut. Copper keeps 0.4 mm from every edge, as JLC wants next to a V-cut.

## Files and tools
- `tools/gen_sch.py`: schematic + BOM; reuses the writer and the symbol/footprint library of `../xiao-solar`.
- `tools/gen_pcb.py`: placement. The charger cluster (placement, power zones, hand routes) comes
  from `../xiao-solar/tools/charger.py`, shared with the main board and rotated 270° here;
  keepouts hold signal tracks off the L1 switch node and 0.4 mm off the board edge.
- `tools/run.py <tool>` runs a shared tool from `../xiao-solar/tools` on this board:
  `FREEROUTING_JAR=… python3 tools/run.py route`, then `python3 tools/run.py export`.
- `production/`: Gerber zip, `bom-jlc.csv`, `cpl-jlc.csv`, schematic PDF, board image, DRC report.

DRC: 0 unconnected items, 0 track/pad clearance errors. What remains is cosmetic silk, courtyard and library items.
