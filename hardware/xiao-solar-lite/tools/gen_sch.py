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
del base.parts[:], base.texts[:]
base.ROOT = ROOT
base.PROJECT = "xiao-solar-lite"
base.ROOT_UUID = "6b1f3c1e-6a5e-4b7e-9d65-5f0b3a1d0002"
base.TITLE = "Solar charger layer for XIAO + Wio (BQ25798)"
base.REV = "0.3"
P, R, C, T = base.P, base.R, base.C, base.T
parts, ROOT_UUID = base.parts, base.ROOT_UUID
C0603, C1206, CONN2 = base.C0603, base.C1206, base.CONN2
C10U, C10U50, C100N, C47N, C4U7 = base.C10U, base.C10U50, base.C100N, base.C47N, base.C4U7

# ---- 1. Input ----------------------------------------------------------------
T(15, 15, "1. ВХОД: соларен панел 5-22 V (Voc до 25 V на студ)", 2.0)
P("J1", "Connector_Generic:Conn_01x02", "SOLAR", 20, 35, {"1": "SOLAR_IN", "2": "GND"}, CONN2)
P("D1", "Device:D_Schottky", "SS34", 45, 30, {"2": "SOLAR_IN", "1": "VBUS"}, "Diode_SMD:D_SMA", "C8678")
P("D3", "xiao_solar:SMBJ24A-C87268", "SMBJ24A", 65, 40, {"1": "VBUS", "2": "GND"}, "xiao_solar:SMB_L4.6-W3.6-LS5.3-RD", "C19077578")
C("C1", "10u/50V", 78, 40, "VBUS", "GND", C10U50, C1206)
C("C2", "10u/50V", 88, 40, "VBUS", "GND", C10U50, C1206)
C("C3", "100n", 98, 40, "VBUS", "GND", C100N, C0603)
T(15, 55, "SS34: защита от обратна полярност и от връщане към панела. VAC1=VAC2=VBUS, ACDRV към GND.", 1.27)

# ---- 2. Charger ----------------------------------------------------------------
T(15, 80, "2. MPPT ЗАРЯДНО BQ25798 (1S, 750 kHz, 2.2 uH), I2C 0x6B", 2.0)
bq = {"1": "STAT", "2": "VBUS", "3": "VBUS", "4": "BTST1", "5": "REGN", "6": None, "7": None,
      "8": "VBUS", "9": "VBUS", "10": "GND", "11": "GND", "12": "QON", "13": "CE", "14": "SCL",
      "15": "SDA", "16": "TS", "17": "REGN", "18": "BATP", "19": "BTST2", "20": "PROG",
      "21": None, "22": "BAT", "23": "BAT", "24": "SDRV", "25": "SYS", "26": "SW2", "27": "GND",
      "28": "SW1", "29": "PMID"}
P("U1", "xiao_solar:BQ25798RQMR", "BQ25798RQMR", 70, 125, bq,
  "xiao_solar:QFN-29_L4.0-W4.0-P0.40-TL-BQ25792RQMR", "C2876593")
P("L1", "xiao_solar:FNR4030S2R2MT", "2.2uH 3.2A", 125, 100, {"1": "SW1", "2": "SW2"},
  "xiao_solar:IND-SMD_L4.0-W4.0_FNR40XXS", "C167869")
C("C4", "47n", 125, 112, "BTST1", "SW1", C47N, C0603)
C("C5", "47n", 135, 112, "BTST2", "SW2", C47N, C0603)
C("C6", "10u/50V", 110, 140, "PMID", "GND", C10U50, C1206)
C("C7", "10u/50V", 118, 140, "PMID", "GND", C10U50, C1206)
C("C8", "10u/50V", 126, 140, "PMID", "GND", C10U50, C1206)
C("C9", "100n", 134, 140, "PMID", "GND", C100N, C0603)
C("C10", "4.7u/25V", 142, 140, "REGN", "GND", C4U7)
R("R1", "4.7k 1%", 150, 140, "PROG", "GND", "C23162")
C("C11", "10u/25V", 110, 160, "SYS", "GND", C10U)
C("C12", "10u/25V", 118, 160, "SYS", "GND", C10U)
C("C13", "10u/25V", 126, 160, "SYS", "GND", C10U)
C("C14", "10u/25V", 134, 160, "SYS", "GND", C10U)
C("C15", "10u/25V", 142, 160, "SYS", "GND", C10U)
C("C16", "100n", 150, 160, "SYS", "GND", C100N, C0603)
C("C17", "10u/25V", 158, 160, "BAT", "GND", C10U)
C("C18", "10u/25V", 166, 160, "BAT", "GND", C10U)
# No NTC: TS held at 59.5 % of REGN, the same as a 10k NTC at 25 C with the RT1/RT2 divider,
# so charging is never blocked by the TS window (firmware may also set TS_IGNORE).
R("R2", "5.1k 1%", 160, 100, "REGN", "TS", "C23186")
R("R3", "7.5k 1%", 160, 115, "TS", "GND", "C23234")
# CE (active low) on Xiao D0. JP2 picks what R4 does while nobody drives CE:
# 1-2 (default, Li-ion) pull-down = charges on its own with the BQ defaults (4.2 V / 1 A);
# 2-3 (LiFePO4) pull-up to the Xiao 3V3 = charges only after firmware set VREG and pulls CE low.
R("R4", "100k", 30, 160, "CE_PULL", "CE", "C25803")
P("JP2", "Jumper:SolderJumper_3_Bridged12", "CE: 1-2 Li-ion / 2-3 LiFePO4", 30, 175,
  {"1": "GND", "2": "CE_PULL", "3": "3V3"}, "Jumper:SolderJumper-3_P1.3mm_Bridged12_RoundedPad1.0x1.5mm")
R("R6", "2.2k", 55, 160, "REGN", "LED_STAT", "C4190")
P("D4", "Device:LED", "green", 55, 175, {"1": "STAT", "2": "LED_STAT"}, "LED_SMD:LED_0805_2012Metric", "C2297")
P("TP2", "Connector:TestPoint", "QON (to GND = wake)", 90, 165, {"1": "QON"}, "TestPoint:TestPoint_Pad_D1.0mm")
T(15, 192, "INT не е изведен (firmware чете регистрите). Watchdog off. Без NTC и без DW01A: клетката трябва да е със собствена защита.", 1.27)

# ---- 3. Battery ----------------------------------------------------------------------
T(15, 205, "3. БАТЕРИЯ 1S (Li-ion по подразбиране, LiFePO4 с настройка), защитена клетка; ship FET", 2.0)
P("J4", "Connector_Generic:Conn_01x02", "BATTERY", 20, 230, {"1": "BATT+", "2": "GND"}, CONN2)
P("Q2", "Transistor_FET:BSS138", "AO3400A (ship FET)", 50, 225, {"1": "SDRV", "2": "BAT", "3": "BATT+"}, base.SOT23, "C20917")
R("R8", "100", 70, 225, "BATT+", "BATP", "C22775")

# ---- 4. XIAO stack + I2C -------------------------------------------------------------------
T(205, 15, "4. ТРЕТИ СЛОЙ ПОД XIAO + Wio-SX1262 (общи 14 пина, пинове като variants/xiao_nrf52)", 2.0)
P("U2", "custom:XIAO_nRF52840", "XIAO + Wio stack", 235, 45,
  {"1": "CE", "2": None, "3": None, "4": None, "5": None, "6": None, "7": "SCL",
   "14": "VOUT", "13": "GND", "12": "3V3", "11": None, "10": None, "9": None, "8": "SDA"},
  "xiao_solar:XIAO_14P_2.54_21x17.8mm")
P("D5", "Device:D_Schottky", "B5819W", 275, 40, {"2": "SYS", "1": "VOUT"}, "Diode_SMD:D_SOD-123", "C8598")
C("C20", "10u/25V", 290, 45, "VOUT", "GND", C10U)
T(205, 70, "D5: SYS към пина 5V на Xiao; USB на Xiao не може да захрани SYS обратно. На Xiao не се слага батерия.", 1.27)
T(205, 74, "CE = D0, SCL = D6, SDA = D7; 3V3 идва от регулатора на Xiao (само за pull-up и JP2).", 1.27)
R("R11", "4.7k", 215, 90, "3V3", "SCL", "C23162")
R("R12", "4.7k", 225, 90, "3V3", "SDA", "C23162")
P("J5", "Connector_Generic:Conn_01x04", "I2C sensors", 245, 110,
  {"1": "SCL", "2": "SDA", "3": "3V3", "4": "GND"}, base.CONN4)

for i, net in enumerate(["GND", "VBUS", "SYS", "BAT", "BATT+", "REGN", "PMID", "VOUT", "3V3"]):
    P("#FLG%02d" % (i + 1), "power:PWR_FLAG", "PWR_FLAG", 300 + (i % 5) * 15, 160 + (i // 5) * 15, {"1": net})

write_bom = base.write_bom

if __name__ == "__main__":
    path = os.path.join(ROOT, base.PROJECT + ".kicad_sch")
    open(path, "w").write(base.emit())
    g, h = write_bom()
    print("wrote", path, len(parts), "parts;", len(g), "JLC lines,", len(h), "hand parts")
