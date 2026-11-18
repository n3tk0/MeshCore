#!/usr/bin/env python3
"""Generates xiao-solar-lite.kicad_sch: the power part of the xiao-solar board on its own
(solar input, BQ25798 MPPT charger, optional NTC, ship FET, DW01A/HJ8205 BMS), made as the
third layer under the XIAO nRF52840 + Wio-SX1262 stack: SYS feeds the Xiao 5V pin, CE/I2C
use D0/D6/D7.

The writer and the part helpers come from ../xiao-solar/tools/gen_sch.py, so both boards
share one symbol library and one connected left-to-right schematic flow.
Run: python3 tools/gen_sch.py  (from hardware/xiao-solar-lite)
"""
import importlib.util, os

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
_spec = importlib.util.spec_from_file_location(
    "xs_gen_sch", os.path.join(ROOT, "..", "xiao-solar", "tools", "gen_sch.py"))
base = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(base)
del base.parts[:], base.texts[:], base.blocks[:], base.rails[:]
base.OFF[:] = [0.0, 0.0]
base.ROOT = ROOT
base.PROJECT = "xiao-solar-lite"
base.ROOT_UUID = "6b1f3c1e-6a5e-4b7e-9d65-5f0b3a1d0002"
base.TITLE = "Solar charger layer for XIAO + Wio (BQ25798 + BMS)"
base.REV = "0.5"
P, R, C, T, TP, RAIL, BLOCK, OFF = base.P, base.R, base.C, base.T, base.TP, base.RAIL, base.BLOCK, base.OFF
parts, ROOT_UUID = base.parts, base.ROOT_UUID
C0402, C0603, C0805, C1206, SOT23, CONN2, CONN4 = (
    base.C0402, base.C0603, base.C0805, base.C1206, base.SOT23, base.CONN2, base.CONN4)
C10U, C10U50, C100N, C100N0402, C100N0402HV, C47N, C4U7 = (
    base.C10U, base.C10U50, base.C100N, base.C100N0402, base.C100N0402HV, base.C47N, base.C4U7)

# ---- 1. Solar Input -> VBUS Rail -> BQ25798 Charger --------------------------
BLOCK(20, 22, "1. ВХОД (5-22 V соларен панел) -> MPPT ЗАРЯДНО BQ25798 (1S, 750 kHz, 2.2 uH)")
P("J1", "Connector_Generic:Conn_01x02", "SOLAR", 28, 78, {"1": "SOLAR_IN", "2": "GND"}, CONN2, mirror=True)
P("D1", "Device:D_Schottky", "SS54", 46, 78, {"2": "SOLAR_IN", "1": "VBUS"}, "Diode_SMD:D_SMA", "C22452", mirror=True)
RAIL("SOLAR_IN", [(33.08, 78.0), (42.19, 78.0)])

# VBUS rail runs horizontally at y = 78.0 from D1 cathode (49.81) to x = 116.0, then down into U1 VBUS/VAC1/VAC2
RAIL("VBUS", [(49.81, 78.0), (116.0, 78.0), (116.0, 117.54), (124.76, 117.54)])
RAIL("VBUS", [(116.0, 109.92), (124.76, 109.92)])
RAIL("VBUS", [(116.0, 112.46), (124.76, 112.46)])
RAIL("VBUS", [(116.0, 115.00), (124.76, 115.00)])

P("D3", "xiao_solar:SMBJ24A-C87268", "SMBJ24A", 64, 87.58, {"1": "VBUS", "2": "GND"},
  "xiao_solar:SMB_L4.6-W3.6-LS5.3-RD", "C19077578", rot=270)
C("C1", "10u/50V", 76, 86.5, "VBUS", "GND", C10U50, C1206)
C("C2", "10u/50V", 88, 86.5, "VBUS", "GND", C10U50, C1206)
C("C3", "100n", 100, 86.5, "VBUS", "GND", C100N, C0603)
TP("TP3", "VBUS", 105, 78.0, rot=0)

# PMID rail at y = 120.08 out of U1.PMID (124.76, 120.08) left across C9..C6
RAIL("PMID", [(60.0, 120.08), (124.76, 120.08)])
C("C6", "10u/50V", 60, 128.5, "PMID", "GND", C10U50, C1206)
C("C7", "10u/50V", 72, 128.5, "PMID", "GND", C10U50, C1206)
C("C8", "10u/50V", 84, 128.5, "PMID", "GND", C10U50, C1206)
C("C9", "100n/50V", 96, 128.5, "PMID", "GND", C100N0402HV, C0402)
TP("TP4", "PMID", 104, 120.08, rot=0)

# ---- 2. Charger U1 (BQ25798) + Switching Stage (Top) + NTC/PROG (Bottom) -----
bq = {"1": "STAT", "2": "VBUS", "3": "VBUS", "4": "BTST1", "5": "REGN", "6": None, "7": None,
      "8": "VBUS", "9": "VBUS", "10": "GND", "11": "GND", "12": "QON", "13": "CE", "14": "SCL",
      "15": "SDA", "16": "TS", "17": "REGN", "18": "BATP", "19": "BTST2", "20": "PROG",
      "21": None, "22": "BAT", "23": "BAT", "24": "SDRV", "25": "SYS", "26": "SW2", "27": "GND",
      "28": "SW1", "29": "PMID"}
P("U1", "custom:BQ25798RQMR_FUNC", "BQ25798RQMR", 140, 125, bq,
  "xiao_solar:QFN-29_L4.0-W4.0-P0.40-TL-BQ25792RQMR", "C2876593")

# Switching inductor L1 & bootstrap caps C4, C5 directly above U1
P("L1", "xiao_solar:FNR4030S2R2MT", "2.2uH 4A molded (MWSA0402S)", 140, 72, {"1": "SW1", "2": "SW2"},
  "xiao_solar:IND-SMD_L4.4-W4.2_MWSA0402S", "C408335")
RAIL("SW1", [(134.92, 102.14), (134.92, 72.0)])
RAIL("SW2", [(145.08, 102.14), (145.08, 72.0)])
C("C4", "47n", 125.0, 88, "BTST1", "SW1", C47N, C0603)
RAIL("BTST1", [(129.84, 102.14), (129.84, 81.0), (125.0, 81.0), (125.0, 84.19)])
RAIL("SW1", [(125.0, 91.81), (125.0, 95.0), (134.92, 95.0)])
C("C5", "47n", 155.0, 88, "BTST2", "SW2", C47N, C0603)
RAIL("BTST2", [(150.16, 102.14), (150.16, 81.0), (155.0, 81.0), (155.0, 84.19)])
RAIL("SW2", [(155.0, 91.81), (155.0, 95.0), (145.08, 95.0)])

# QON & CE (JP2 Li-ion/LiFePO4) on the lower-left of U1
TP("TP2", "QON", 112.0, 122.62, rot=90)
RAIL("QON", [(112.0, 122.62), (124.76, 122.62)])

R("R4", "100k", 118, 148, "CE_PULL", "CE", "C25803", rot=180)
RAIL("CE", [(118.0, 125.16), (124.76, 125.16)])
RAIL("CE", [(118.0, 125.16), (118.0, 144.19)])
P("JP2", "Jumper:SolderJumper_3_Bridged12", "CE: 1-2 Li-ion / 2-3 LiFePO4", 118, 162,
  {"1": "GND", "2": "CE_PULL", "3": "3V3"}, "Jumper:SolderJumper-3_P1.3mm_Bridged12_RoundedPad1.0x1.5mm", rot=180)
RAIL("CE_PULL", [(118.0, 151.81), (118.0, 158.19)])

# STAT LED (D4 + R6), REGN (C10), NTC divider (R2/R3/J3), PROG (R1) on the bottom-right of U1
P("D4", "Device:LED", "green", 168, 127.70, {"1": "STAT", "2": "LED_STAT"},
  "LED_SMD:LED_0805_2012Metric", "C2297")
RAIL("STAT", [(155.24, 127.70), (164.19, 127.70)])
R("R6", "2.2k", 188, 127.70, "REGN", "LED_STAT", "C4190", rot=270)
RAIL("LED_STAT", [(171.81, 127.70), (184.19, 127.70)])

RAIL("REGN", [(155.24, 130.24), (196.0, 130.24)])
RAIL("REGN", [(155.24, 132.78), (160.0, 132.78), (160.0, 130.24)])
RAIL("REGN", [(191.81, 127.70), (194.0, 127.70), (194.0, 130.24)])
C("C10", "4.7u/25V", 182, 142, "REGN", "GND", C4U7)
RAIL("REGN", [(182.0, 130.24), (182.0, 138.19)])

R("R2", "5.1k 1%", 196, 142, "REGN", "TS", "C23186")
RAIL("REGN", [(196.0, 130.24), (196.0, 138.19)])
RAIL("TS", [(155.24, 135.32), (172.0, 135.32), (172.0, 150.0), (204.0, 150.0)])
RAIL("TS", [(196.0, 145.81), (196.0, 150.0)])
R("R3", "30k 1%", 196, 160, "TS", "GND", "C22984")
RAIL("TS", [(196.0, 150.0), (196.0, 156.19)])
P("J3", "Connector_Generic:Conn_01x02", "NTC 10k (103AT)", 209.08, 160, {"1": "TS", "2": "GND"}, CONN2)
RAIL("TS", [(204.0, 150.0), (204.0, 160.0)])

R("R1", "4.7k 1%", 164, 152, "PROG", "GND", "C23162")
RAIL("PROG", [(155.24, 137.86), (164.0, 137.86), (164.0, 148.19)])

T(20, 174, "SS54: защита от обратна полярност и връщане към панела. D3 (SMBJ24A) пази VBUS до 25 V.", 1.27)
T(20, 177.5, "CE (D0): JP2 1-2 (Li-ion) зарежда автоматично; 2-3 (LiFePO4) изчаква фърмуера (VREG=3.6V, D0=0). TP2 (QON) към GND = wake.", 1.27)

# ---- 3. SYS Rail -> Ideal Diode Q5/Q6/Q7 -> XIAO + Wio Stack & I2C -------
BLOCK(225, 22, "2. СИСТЕМНО ЗАХРАНВАНЕ (SYS -> ideal diode Q5-Q7 -> VOUT 5V) + XIAO & Wio-SX1262 СТЕК + I2C")
# Continuous SYS wire from U1.SYS (155.24, 109.92) up to y = 55 and right towards Q5 (266.11, 55)
RAIL("SYS", [(155.24, 109.92), (168.0, 109.92), (168.0, 55.0), (266.11, 55.0)])
for i, (ref, val, lcsc, fp) in enumerate([
        ("C16", "100n", C100N0402, C0402), ("C11", "10u/25V", C10U, C0805),
        ("C12", "10u/25V", C10U, C0805), ("C13", "10u/25V", C10U, C0805),
        ("C14", "10u/25V", C10U, C0805), ("C15", "10u/25V", C10U, C0805)]):
    C(ref, val, 180 + 12 * i, 64, "SYS", "GND", lcsc, fp)
TP("TP5", "SYS", 252, 55.0, rot=0)

# Discrete ideal diode SYS -> VOUT (same circuit as the xiao-solar main board, no extended-part
# fee): Q5 AO3401A (D at SYS, S at VOUT, its body diode points to the Xiao), PNP mirror Q6/Q7
# (MMBT3906). Q6 (diode connected, E at SYS) sets the base; Q7 (E at VOUT) pulls the gate up
# to VOUT as soon as VOUT > SYS (USB on the Xiao), so no current flows back into SYS.
P("Q5", "Transistor_FET:BSS84", "AO3401A (ideal diode)", 275, 50, {"1": "OR_G", "2": "VOUT", "3": "SYS"},
  SOT23, "C15127", rot=90)
P("Q6", "Transistor_BJT:MMBT3906", "MMBT3906", 266, 72, {"1": "OR_B", "2": "SYS", "3": "OR_B"}, SOT23, "C7420354")
P("Q7", "Transistor_BJT:MMBT3906", "MMBT3906", 284, 72, {"1": "OR_B", "2": "VOUT", "3": "OR_G"}, SOT23, "C7420354")
R("R30", "1M", 262, 86, "OR_B", "GND", "C22935", rot=90)
R("R31", "470k", 288, 86, "OR_G", "GND", "C23178", rot=90)
RAIL("VOUT", [(283.89, 55.0), (305.0, 55.0)])
C("C20", "10u/25V", 296, 64, "VOUT", "GND", C10U)

# U3 (XIAO + Wio stack) in Top-View orientation (D0..D6 left, 5V..D7 right)
P("U3", "custom:XIAO_nRF52840", "XIAO + Wio stack", 342, 62.62,
  {"1": "CE", "2": None, "3": None, "4": None, "5": None, "6": None, "7": "SCL",
   "14": "VOUT", "13": "GND", "12": "3V3", "11": None, "10": None, "9": None, "8": "SDA"},
  "xiao_solar:XIAO_14P_2.54_21x17.8mm")
# Connect VOUT directly from Q5/C20 over the top of U3 into U3 pin 14 (5V at 353.43, 55.0)
RAIL("VOUT", [(305.0, 55.0), (305.0, 42.0), (358.0, 42.0), (358.0, 55.0), (353.43, 55.0)])
# Connect CE directly from U1.~CE (118.0, 125.16) over the top lane (y=36.0) into U3.D0 (330.57, 55.0)
RAIL("CE", [(118.0, 125.16), (118.0, 36.0), (326.0, 36.0), (326.0, 55.0), (330.57, 55.0)])
RAIL("GND", [(353.43, 57.54), (363.0, 57.54), (363.0, 48.0), (368.0, 48.0)])
RAIL("3V3", [(353.43, 60.08), (386.0, 60.08)])
TP("TP8", "3V3", 386, 60.08, rot=0)

# I2C bus (SCL, SDA, 3V3) + Pull-ups R11/R12 + J5 — wired directly to U1 and U3!
P("J5", "Connector_Generic:Conn_01x04", "I2C sensors", 252, 146.0,
  {"1": "SCL", "2": "SDA", "3": "3V3", "4": "GND"}, CONN4, mirror=True)
R("R11", "4.7k", 272, 134, "3V3", "SCL", "C23162")
R("R12", "4.7k", 282, 134, "3V3", "SDA", "C23162")
TP("TP9", "SCL", 292, 143.46, rot=0)
TP("TP10", "SDA", 300, 146.00, rot=0)

RAIL("SCL", [(257.08, 143.46), (310.0, 143.46)])
RAIL("SDA", [(257.08, 146.00), (364.0, 146.00)])
RAIL("SCL", [(155.24, 112.46), (262.0, 112.46), (262.0, 143.46)])
RAIL("SDA", [(155.24, 115.00), (260.0, 115.00), (260.0, 146.00)])
RAIL("SCL", [(272.0, 137.81), (272.0, 143.46)])
RAIL("SDA", [(282.0, 137.81), (282.0, 146.00)])
RAIL("3V3", [(257.08, 148.54), (370.0, 148.54), (370.0, 60.08)])
RAIL("3V3", [(272.0, 130.19), (282.0, 130.19), (282.0, 126.0), (264.0, 126.0), (264.0, 148.54)])
RAIL("SCL", [(330.57, 70.24), (310.0, 70.24), (310.0, 143.46)])
RAIL("SDA", [(353.43, 70.24), (364.0, 70.24), (364.0, 146.00)])

# ---- 4. Battery Path: BAT -> Ship FET (Q2 + JP3) -> BMS (DW01A + HJ8205) -> J4
BLOCK(20, 186, "3. БАТЕРИЯ 1S (Li-ion / LiFePO4): Ship FET (Q2 + JP3) -> BMS (DW01A + HJ8205) -> J4")
# Continuous BAT wire from U1.BAT (155.24, 120.08 & 122.62) right to x = 232, down to y = 210, and right into Q2 Source (259.92, 210)
RAIL("BAT", [(155.24, 120.08), (158.0, 120.08), (158.0, 122.62), (155.24, 122.62)])
RAIL("BAT", [(158.0, 121.35), (232.0, 121.35), (232.0, 210.0), (259.92, 210.0)])
C("C17", "10u/25V", 238, 219, "BAT", "GND", C10U)
C("C18", "10u/25V", 248, 219, "BAT", "GND", C10U)
TP("TP6", "BAT", 232, 210.0, rot=0)

# Ship FET Q2 (rot=270 at (265, 207.46): Pin 1 Gate=(265, 202.38), Pin 2 Source=(259.92, 210.0), Pin 3 Drain=(270.08, 210.0))
P("Q2", "Transistor_FET:BSS138", "AO3400A (ship FET)", 265, 207.46,
  {"1": "SDRV", "2": "BAT", "3": "BATT+"}, SOT23, "C20917", rot=270)
RAIL("SDRV", [(155.24, 117.54), (236.0, 117.54), (236.0, 198.0), (265.0, 198.0), (265.0, 202.38)])
P("JP3", "Jumper:SolderJumper_2_Open", "no ship FET (LiFePO4)", 265, 191.0,
  {"1": "BAT", "2": "BATT+"}, "Jumper:SolderJumper-2_P1.3mm_Open_RoundedPad1.0x1.5mm")
RAIL("BAT", [(255.0, 210.0), (255.0, 191.0), (259.92, 191.0)])
RAIL("BATT+", [(270.08, 191.0), (275.0, 191.0), (275.0, 210.0)])

# BATP Kelvin sense through R8 (100R) below Q2 to BATT+
R("R8", "100", 265, 226.0, "BATT+", "BATP", "C22775", rot=270)
RAIL("BATP", [(155.24, 125.16), (228.0, 125.16), (228.0, 226.0), (261.19, 226.0)])
RAIL("BATT+", [(268.81, 226.0), (275.0, 226.0), (275.0, 210.0)])

# BATT+ rail continues straight from Q2 Drain (270.08, 210.0) to J4 (BATTERY) pin 1 (374.92, 210.0)
RAIL("BATT+", [(270.08, 210.0), (374.92, 210.0)])
P("J4", "Connector_Generic:Conn_01x02", "BATTERY", 380, 210.0,
  {"1": "BATT+", "2": "BATT-"}, CONN2)

# BMS: R9 (100R) from BATT+ down to U2 (DW01A) VCC; C19 (100n) to BATT-; U2 + Q3 (HJ8205) + R10 (1k) in BATT- lead
R("R9", "100", 322, 220, "BATT+", "DW_VCC", "C22775")
RAIL("BATT+", [(322.0, 210.0), (322.0, 216.19)])
C("C19", "100n", 322, 236, "DW_VCC", "BATT-", C100N, C0603)
RAIL("DW_VCC", [(322.0, 223.81), (322.0, 232.19)])
RAIL("DW_VCC", [(322.0, 228.0), (331.11, 228.0)])

P("U2", "custom:DW01A_FUNC", "DW01A", 340, 230.54,
  {"1": "DW_OD", "2": "DW_CS", "3": "DW_OC", "4": None, "5": "DW_VCC", "6": "BATT-"},
  "xiao_solar:SOT-23-6_L2.9-W1.6-P0.95-LS2.8-BL", "C351410")
P("Q3", "custom:HJ8205_FUNC", "HJ8205", 368, 228.0,
  {"1": "BATT-", "2": "FS_D", "3": "GND", "4": "DW_OC", "5": "FS_D", "6": "DW_OD"},
  "xiao_solar:SOT-23-6_L2.9-W1.6-P0.95-LS2.8-BL", "C20069150")

RAIL("DW_OD", [(348.89, 228.0), (359.11, 228.0)])
RAIL("DW_OC", [(348.89, 230.54), (359.11, 230.54)])
RAIL("BATT-", [(374.92, 212.54), (355.0, 212.54), (355.0, 225.46), (359.11, 225.46)])
RAIL("BATT-", [(322.0, 239.81), (322.0, 242.0), (328.0, 242.0), (328.0, 230.54), (331.11, 230.54)])
RAIL("BATT-", [(328.0, 242.0), (355.0, 242.0), (355.0, 225.46)])
RAIL("FS_D", [(376.89, 225.46), (380.0, 225.46), (380.0, 228.0), (376.89, 228.0)])
R("R10", "1k", 354, 250, "DW_CS", "GND", "C21190")
RAIL("DW_CS", [(348.89, 233.08), (354.0, 233.08), (354.0, 246.19)])

TP("TP7", "GND", 202, 210.0, rot=0)
RAIL("GND", [(202.0, 210.0), (202.0, 215.08)])
T(20, 252, "Q2 (AO3400A): S към BAT, D към батерията; в shutdown спира разреждането. За LiFePO4: затвори JP3 (без Q2).", 1.27)
T(20, 255.5, "BMS (DW01A + HJ8205) в минуса на батерията (BATT- към GND): защита от презареждане (4.3V), преразреждане (2.4V) и късо.", 1.27)
T(20, 259, "LiFePO4: прагът 4.3V на DW01A е над 3.65V, т.е. от презареждане пази само VREG=3.6V на BQ25798; DW01A остава за 2.4V и късо/свръхток (~3A).", 1.27)

# ERC power flags placed directly on their rail wires
for i, (net, fx, fy) in enumerate([
        ("VBUS", 55.0, 78.0), ("BATT+", 300.0, 210.0), ("BATT-", 355.0, 212.54),
        ("DW_VCC", 326.0, 228.0), ("GND", 202.0, 212.54), ("VOUT", 300.0, 55.0)]):
    P("#FLG%02d" % (i + 1), "power:PWR_FLAG", "PWR_FLAG", fx, fy, {"1": net})

write_bom = base.write_bom

if __name__ == "__main__":
    path = os.path.join(ROOT, base.PROJECT + ".kicad_sch")
    open(path, "w", encoding="utf-8").write(base.emit())
    g, h = write_bom()
    print("wrote", path, len(parts), "parts;", len(g), "JLC lines,", len(h), "hand parts")
