# xiao-solar-lite: solar charger layer for XIAO + Wio-SX1262

The power part of [`../xiao-solar`](../xiao-solar) as the third layer of the standard
XIAO nRF52840 + Wio-SX1262 sandwich: this board at the bottom, Wio in the middle, XIAO on top
(USB and buttons stay reachable). Solar input, BQ25798 MPPT buck-boost charger, ship FET.
23 × 53 mm, 2 layers, all SMD parts on the top side (JLC assembly), no mounting holes.

The stack sits over the 14 XIAO holes at the USB end; only flat parts (two 0603 pull-ups) are
under it, since the header plastic leaves ~2.5 mm and the inductor is 3 mm tall. The charger
is below the stack. Long pin headers go through all three boards.

Status v0.2: schematic, routed PCB, Gerber, JLC BOM/CPL. Not built or tested.

## Connections
| Where | Pins | Notes |
|---|---|---|
| XIAO stack | D0 = CE, D6 = SCL, D7 = SDA, 3V3, GND, 5V | 5V gets SYS (3.5–4.4 V) through B5819W, so the XIAO USB cannot feed SYS. Other pins pass through. **No battery on the XIAO itself.** |
| J1 SOL | + / − | 5–22 V panel (Voc up to 25 V), SS34 against reverse polarity, SMBJ24A TVS |
| J4 BAT | + / − | 1S cell **with its own protection** (no DW01A on this board) |
| J5 | SCL SDA 3V3 GND | external I2C sensors (4.7k pull-ups on board, 3V3 from the XIAO) |

## Charger settings
- **CE / JP2:** CE is XIAO D0. 1-2 (default) = Li-ion, charges on its own with the BQ defaults
  (4.2 V, 1 A) even while the XIAO is off; 2-3 = LiFePO4, the pull-up to 3V3 holds charging off until firmware has
  set VREG 3.6 V and pulls CE low.
- **No NTC:** TS is held at 59.5 % of REGN (5.1k/7.5k), the value of a 10k NTC at 25 °C, so the
  TS window never stops charging. Firmware may also set TS_IGNORE.
- **Charge current:** up to 2 A (firmware sets ICHG; 2.2 µH 3.2 A inductor). PROG 4.7k = 750 kHz, 1S.
- **STAT LED** (green) lights while charging; QON test pad TP2 to GND wakes the charger from ship mode.
- **INT** is not brought out: firmware polls the status registers.

## JLC
19 BOM lines, 2 extended parts with a setup fee (BQ25798, FNR4030S2R2MT inductor); SMBJ24A is
a preferred extended part (no fee), everything else is basic. Headers are hand-soldered
(`production/bom-hand.csv`).

## Files and tools
- `tools/gen_sch.py`: schematic + BOM; reuses the writer and the symbol/footprint library of `../xiao-solar`.
- `tools/gen_pcb.py`: placement. The charger cluster keeps the relative placement and hand routes
  of the xiao-solar board (`preroute_charger`); keepouts hold signal tracks off the L1 switch node
  and 0.4 mm off the board edge.
- `tools/run.py <tool>` runs a shared tool from `../xiao-solar/tools` on this board:
  `FREEROUTING_JAR=… python3 tools/run.py route`, then `python3 tools/run.py export`.
- `production/`: Gerber zip, `bom-jlc.csv`, `cpl-jlc.csv`, schematic PDF, board image, DRC report.

DRC: 0 unconnected items. What remains is the JP2 bridge polygon touching the GND pour (expected:
it ties JP2 pad 1 to GND) and cosmetic silk, courtyard and library items.
