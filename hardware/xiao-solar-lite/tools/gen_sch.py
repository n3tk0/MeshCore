#!/usr/bin/env python3
"""Generates xiao-solar-lite.kicad_sch: the power part of the xiao-solar board on its own
(solar input, BQ25798 MPPT charger, ship FET), no BMS/NTC, made as the third layer under the
XIAO nRF52840 + Wio-SX1262 stack: SYS feeds the Xiao 5V pin, CE/I2C use D0/D6/D7.

The writer and the part helpers come from ../xiao-solar/tools/gen_sch.py, so both boards
share one symbol library and one way of drawing the schematic.
Run: python3 tools/gen_sch.py  (from hardware/xiao-solar-lite)
"""
import importlib.util, os

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
_spec = importlib.util.spec_from_file_location(
    "xs_gen_sch", os.path.join(ROOT, "..", "xiao-solar", "tools", "gen_sch.py"))
base = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(base)
del base.parts[:], base.texts[:], base.blocks[:]
base.OFF[:] = [0.0, 0.0]
base.ROOT = ROOT
base.PROJECT = "xiao-solar-lite"
base.ROOT_UUID = "6b1f3c1e-6a5e-4b7e-9d65-5f0b3a1d0002"
base.TITLE = "Solar charger layer for XIAO + Wio (BQ25798)"
base.REV = "0.4"
P, R, C, T, BLOCK, OFF = base.P, base.R, base.C, base.T, base.BLOCK, base.OFF
parts, ROOT_UUID = base.parts, base.ROOT_UUID
C0603, C1206, CONN2 = base.C0603, base.C1206, base.CONN2
C10U, C10U50, C100N, C47N, C4U7 = base.C10U, base.C10U50, base.C100N, base.C47N, base.C4U7

# ---- 1. Input ----------------------------------------------------------------
BLOCK(15, 15, "1. ВХОД: соларен панел 5-22 V (Voc до 25 V на студ)")
P("J1", "Connector_Generic:Conn_01x02", "SOLAR", 25, 35, {"1": "SOLAR_IN", "2": "GND"}, CONN2, mirror=True)
P("D1", "Device:D_Schottky", "SS54", 45, 35, {"2": "SOLAR_IN", "1": "VBUS"}, "Diode_SMD:D_SMA", "C22452", mirror=True)
P("D3", "xiao_solar:SMBJ24A-C87268", "SMBJ24A", 62, 49.08, {"1": "VBUS", "2": "GND"}, "xiao_solar:SMB_L4.6-W3.6-LS5.3-RD", "C19077578", rot=270)
C("C1", "10u/50V", 75, 48, "VBUS", "GND", C10U50, C1206)
C("C2", "10u/50V", 87, 48, "VBUS", "GND", C10U50, C1206)
C("C3", "100n", 99, 48, "VBUS", "GND", C100N, C0603)
T(15, 62, "SS54: защита от обратна полярност и от връщане към панела. VAC1=VAC2=VBUS, ACDRV към GND.", 1.27)

# ---- 2. Charger (same drawing as the main board, without the NTC) -------------------
BLOCK(15, 72, "2. MPPT ЗАРЯДНО BQ25798 (1S, 750 kHz, 2.2 uH), I2C 0x6B")
OFF[1] = -20.0   # blocks 2 and 3 drawn higher than their coordinates
bq = {"1": "STAT", "2": "VBUS", "3": "VBUS", "4": "BTST1", "5": "REGN", "6": None, "7": None,
      "8": "VBUS", "9": "VBUS", "10": "GND", "11": "GND", "12": "QON", "13": "CE", "14": "SCL",
      "15": "SDA", "16": "TS", "17": "REGN", "18": "BATP", "19": "BTST2", "20": "PROG",
      "21": None, "22": "BAT", "23": "BAT", "24": "SDRV", "25": "SYS", "26": "SW2", "27": "GND",
      "28": "SW1", "29": "PMID"}
P("U1", "xiao_solar:BQ25798RQMR", "BQ25798RQMR", 80, 140, bq,
  "xiao_solar:QFN-29_L4.0-W4.0-P0.40-TL-BQ25792RQMR", "C2876593")
C("C4", "47n", 80, 112, "BTST1", "SW1", C47N, C0603, rot=90)
P("L1", "xiao_solar:FNR4030S2R2MT", "2.2uH 4A molded (MWSA0402S)", 112, 112, {"1": "SW1", "2": "SW2"},
  "xiao_solar:IND-SMD_L4.4-W4.2_MWSA0402S", "C408335")
C("C5", "47n", 112, 147.62, "BTST2", "SW2", C47N, C0603, rot=90)
R("R1", "4.7k 1%", 104, 165, "PROG", "GND", "C23162")
for i, (ref, val, lcsc, fp) in enumerate([
        ("C6", "10u/50V", C10U50, C1206), ("C7", "10u/50V", C10U50, C1206), ("C8", "10u/50V", C10U50, C1206),
        # 100 nF right at the PMID/SYS and GND (27) pins, 0402 as in the TI layout guide
        ("C9", "100n/50V", base.C100N0402HV, base.C0402)]):
    C(ref, val, 30 + 11 * i, 185, "PMID", "GND", lcsc, fp)
C("C10", "4.7u/25V", 78, 185, "REGN", "GND", C4U7)
for i, (ref, val, lcsc, fp) in enumerate([("C11", "10u/25V", C10U, base.C0805), ("C12", "10u/25V", C10U, base.C0805),
                                          ("C13", "10u/25V", C10U, base.C0805), ("C14", "10u/25V", C10U, base.C0805),
                                          ("C15", "10u/25V", C10U, base.C0805), ("C16", "100n", base.C100N0402, base.C0402)]):
    C(ref, val, 92 + 11 * i, 185, "SYS", "GND", lcsc, fp)
C("C17", "10u/25V", 162, 185, "BAT", "GND", C10U)
C("C18", "10u/25V", 173, 185, "BAT", "GND", C10U)
# No NTC: TS held at 59.5 % of REGN, the same as a 10k NTC at 25 C with the RT1/RT2 divider,
# so charging is never blocked by the TS window (firmware may also set TS_IGNORE).
R("R2", "5.1k 1%", 135, 150, "REGN", "TS", "C23186")
R("R3", "7.5k 1%", 135, 162, "TS", "GND", "C23234")
# CE (active low) on Xiao D0. JP2 picks what R4 does while nobody drives CE:
# 1-2 (default, Li-ion) pull-down = charges on its own with the BQ defaults (4.2 V / 1 A);
# 2-3 (LiFePO4) pull-up to the Xiao 3V3 = charges only after firmware set VREG and pulls CE low.
R("R4", "100k", 45, 155.24, "CE_PULL", "CE", "C25803", rot=90)
P("JP2", "Jumper:SolderJumper_3_Bridged12", "CE: 1-2 Li-ion / 2-3 LiFePO4", 41.19, 168,
  {"1": "GND", "2": "CE_PULL", "3": "3V3"}, "Jumper:SolderJumper-3_P1.3mm_Bridged12_RoundedPad1.0x1.5mm", rot=180)
R("R6", "2.2k", 40, 124.76, "REGN", "LED_STAT", "C4190", rot=90)
P("D4", "Device:LED", "green", 52, 124.76, {"1": "STAT", "2": "LED_STAT"}, "LED_SMD:LED_0805_2012Metric", "C2297", mirror=True)
P("TP2", "Connector:TestPoint", "QON", 40, 147, {"1": "QON"}, "TestPoint:TestPoint_Pad_D1.0mm")
T(18, 199, "INT не е изведен (firmware чете регистрите). Watchdog off. TP2 (QON) към GND за 1 s = събуждане от ship mode.", 1.27)
T(18, 202.5, "Без NTC и без DW01A: клетката трябва да е със собствена защита.", 1.27)

# ---- 3. Battery ----------------------------------------------------------------------
BLOCK(15, 211, "3. БАТЕРИЯ 1S (Li-ion по подразбиране, LiFePO4 с настройка), защитена клетка; ship FET")
OFF[1] = -10.0
P("J4", "Connector_Generic:Conn_01x02", "BATTERY", 35, 225, {"1": "BATT+", "2": "GND"}, CONN2, mirror=True)
P("Q2", "Transistor_FET:BSS138", "AO3400A (ship FET)", 82, 227.54, {"1": "SDRV", "2": "BAT", "3": "BATT+"}, base.SOT23, "C20917", rot=90)
# JP3 closed = no ship FET: BQ25798 then runs down to 2.4 V instead of 3.2 V (needed for
# LiFePO4); leave SFET_PRESENT=0 in firmware. Q2 may stay fitted or be left off.
P("JP3", "Jumper:SolderJumper_2_Open", "no ship FET (LiFePO4)", 82, 213, {"1": "BAT", "2": "BATT+"},
  "Jumper:SolderJumper-2_P1.3mm_Open_RoundedPad1.0x1.5mm", mirror=True)
R("R8", "100", 70, 240, "BATT+", "BATP", "C22775")
T(100, 220, "Q2: D към батерията, S към BAT; в shutdown (SDRV_CTRL=01, 0.5 uA)", 1.27)
T(100, 223.5, "диодът на Q2 спира разреждането; събужда се, когато панелът даде напрежение.", 1.27)

# ---- 4. XIAO stack -------------------------------------------------------------------
OFF[1] = 0.0
BLOCK(192, 15, "4. ТРЕТИ СЛОЙ ПОД XIAO + Wio-SX1262 (общи 14 пина, пинове като variants/xiao_nrf52)")
P("U2", "custom:XIAO_nRF52840", "XIAO + Wio stack", 235, 45,
  {"1": "CE", "2": None, "3": None, "4": None, "5": None, "6": None, "7": "SCL",
   "14": "VOUT", "13": "GND", "12": "3V3", "11": None, "10": None, "9": None, "8": "SDA"},
  "xiao_solar:XIAO_14P_2.54_21x17.8mm")
# ideal diode: SYS to the Xiao 5V pin with ~30 mV drop; CE tied to VOUT blocks the Xiao USB
# from feeding SYS back
P("D5", "custom:LM66100", "LM66100", 275, 80, {"1": "SYS", "2": "GND", "3": "VOUT", "4": "GND", "5": "GND", "6": "VOUT"},
  "Package_TO_SOT_SMD:SOT-363_SC-70-6", "C2869734")
C("C20", "10u/25V", 295, 82, "VOUT", "GND", C10U)
T(195, 97, "D5: SYS към пина 5V на Xiao; USB на Xiao не може да захрани SYS обратно. На Xiao не се слага батерия.", 1.27)
T(195, 100.5, "3V3 идва от регулатора на Xiao (само за pull-up и JP2).", 1.27)

# ---- 5. I2C ---------------------------------------------------------------------------
BLOCK(192, 112, "5. I2C (CE=D0, SCL=D6, SDA=D7): BQ25798 0x6B, външни сензори")
R("R11", "4.7k", 222, 130, "3V3", "SCL", "C23162")
R("R12", "4.7k", 232, 130, "3V3", "SDA", "C23162")
P("J5", "Connector_Generic:Conn_01x04", "I2C sensors", 205, 150,
  {"1": "SCL", "2": "SDA", "3": "3V3", "4": "GND"}, base.CONN4, mirror=True)

BLOCK(192, 180, "6. Тест точки за първото пускане (падове 1 mm отгоре)")
base.test_points(["VBUS", "PMID", "SYS", "BAT", "GND", "3V3", "SCL", "SDA"], 200, 197, step=13)

BLOCK(352, 15, "PWR_FLAG")
for i, net in enumerate(["GND", "VBUS", "SYS", "BAT", "BATT+", "REGN", "PMID", "VOUT", "3V3"]):
    P("#FLG%02d" % (i + 1), "power:PWR_FLAG", "PWR_FLAG", 360 + (i % 3) * 17, 32 + (i // 3) * 18, {"1": net})

write_bom = base.write_bom

if __name__ == "__main__":
    path = os.path.join(ROOT, base.PROJECT + ".kicad_sch")
    open(path, "w").write(base.emit())
    g, h = write_bom()
    print("wrote", path, len(parts), "parts;", len(g), "JLC lines,", len(h), "hand parts")
