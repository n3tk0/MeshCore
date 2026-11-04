#!/usr/bin/env python3
"""Generates solar-smart-mppt.kicad_sch: universal solar MPPT charger + 1S/2S BMS + coulomb
counter. Based on xiao-solar-lite (same BQ25798 charger cluster); the XIAO stack is gone and
an ATtiny3226 supervisor runs the charger (internal I2C), the BMS FETs, 2S balancing and the
shunt current measurement, and shows itself to the host as an INA260 on 0x41 (J5).

Outputs: J6 (SYS through an LTC4412 ideal diode) and J7 (TPS63070 buck-boost, 3.3 / 5.0 V).
Decisions: /mnt/project-files/notes/solar-smart-mppt-phase1-2026-11-03.md + thread answers.

The writer and the part helpers come from ../xiao-solar/tools/gen_sch.py; this board keeps
its own copy of the symbol and footprint library in lib/.
Run: python3 tools/gen_sch.py  (from hardware/solar-smart-mppt)
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
base.LOCAL = os.path.join(ROOT, "lib", "xiao_solar.kicad_sym")
base._libcache.clear()
base.PROJECT = "solar-smart-mppt"
base.ROOT_UUID = "6b1f3c1e-6a5e-4b7e-9d65-5f0b3a1d0003"
base.TITLE = "solar-smart-mppt: MPPT charger + BMS 1S/2S + coulomb counter"
base.REV = "0.2"
base.POWER[:] = ["GND", "3V3M"]
P, R, C, T, TP, RAIL, BLOCK = base.P, base.R, base.C, base.T, base.TP, base.RAIL, base.BLOCK
parts, ROOT_UUID = base.parts, base.ROOT_UUID
C0402, C0603, C0805, C1206, SOT23 = base.C0402, base.C0603, base.C0805, base.C1206, base.SOT23
CONN2, CONN4 = base.CONN2, base.CONN4
CONN3 = "Connector_PinHeader_2.54mm:PinHeader_1x03_P2.54mm_Vertical"
R1206 = "Resistor_SMD:R_1206_3216Metric"
C10U, C10U50, C100N, C100N0402, C100N0402HV, C47N, C4U7 = (
    base.C10U, base.C10U50, base.C100N, base.C100N0402, base.C100N0402HV, base.C47N, base.C4U7)
C10U0603 = "C19702"      # 10uF 10V X5R 0603 (TPS63070 HF caps, max 5.6 V)
C22U = "C45783"          # 22uF 25V X5R 0805
R1M, R10K, R100K, R2K2 = "C22935", "C25804", "C25803", "C4190"
R0402 = "Resistor_SMD:R_0402_1005Metric"
# small signal parts of the new blocks are 0402 (basic parts), so the board stays 23 x 70
S0402 = {R1M: "C26083", R10K: "C25744", R100K: "C25741", R2K2: "C25879", "C23162": "C25900"}
def RS(ref, val, x, y, a, b, lcsc, rot=0):
    if lcsc in S0402:
        base.P(ref, "Device:R", val, x, y, {"1": a, "2": b}, R0402, S0402[lcsc], rot=rot)
    else:
        base.R(ref, val, x, y, a, b, lcsc, rot=rot)
def CS(ref, val, x, y, a, b, lcsc, fp=C0805, rot=0):
    if fp == C0603 and lcsc in (C100N, "C15849"):
        lcsc, fp = {C100N: "C1525", "C15849": "C52923"}[lcsc], C0402
    base.C(ref, val, x, y, a, b, lcsc, fp, rot=rot)
JP3P = "Jumper:SolderJumper-3_P1.3mm_Open_RoundedPad1.0x1.5mm"
JP2P = "Jumper:SolderJumper-2_P1.3mm_Open_RoundedPad1.0x1.5mm"

# ---- new symbols (pins grouped by function; footprints from easyeda2kicad, lib/) ------
box = base.custom_box
box("ATTINY3226_FUNC", "U",
    left=[("1", "VDD", "power_in"), ("16", "PA0/UPDI", "bidirectional"),
          ("2", "PA4/AIN4", "input"), ("3", "PA5/AIN5", "input"), ("17", "PA1/AIN1", "input"),
          ("5", "PA7/AINP0", "input"), ("4", "PA6/AINN0", "input"),
          ("18", "PA2", "input"), ("19", "PA3", "output")],
    right=[("10", "PB1/SDA", "bidirectional"), ("11", "PB0/SCL", "bidirectional"),
           ("13", "PC1", "bidirectional"), ("14", "PC2", "bidirectional"), ("12", "PC0", "output"),
           ("9", "PB2", "output"), ("8", "PB3", "output"), ("6", "PB5", "output"), ("7", "PB4", "output"),
           ("15", "PC3", "bidirectional")],
    bottom=[("20", "GND", "power_in", 0.0)],
    width=20.32, fp="xiao_solar:SOIC-20_L12.8-W7.5-P1.27-LS10.3-BL", pad_bot=0.0)
box("TPS63070_FUNC", "U",
    left=[("12", "VIN", "power_in"), ("14", "EN", "input"), ("1", "PS/SYNC", "input"),
          ("15", "VSEL", "input"), ("3", "VAUX", "passive")],
    right=[("7", "VOUT", "power_out"), ("5", "FB", "input"), ("6", "FB2", "passive"),
           ("2", "PG", "open_collector")],
    top=[("11", "L1", "passive", -2.54), ("9", "L2", "passive", 2.54)],
    bottom=[("4", "GND", "power_in", -1.27), ("10", "PGND", "power_in", 1.27)],
    width=15.24, fp="xiao_solar:VQFN-15_L3.0-W2.5-P0.50-BL", pad_top=0.0, pad_bot=0.0)
box("LTC4412_FUNC", "U",
    left=[("1", "VIN", "power_in"), ("3", "CTL", "input")],
    right=[("6", "SENSE", "input"), ("5", "GATE", "output"), ("4", "STAT", "open_collector")],
    bottom=[("2", "GND", "power_in", 0.0)],
    width=12.7, fp="xiao_solar:TSOT-23-6_L2.9-W1.6-P0.95-LS2.8-BR")
box("HT7533_FUNC", "U",
    left=[("2", "VIN", "power_in")], right=[("3", "VOUT", "power_out")],
    bottom=[("1", "GND", "power_in", 0.0)],
    width=10.16, fp="xiao_solar:SOT-89-3_L4.5-W2.5-P1.50-LS4.2-BR")
box("2N7002DW_FUNC", "Q",
    left=[("5", "G1", "input"), ("2", "G2", "input")],
    right=[("3", "D1", "passive"), ("4", "S1", "passive"), ("6", "D2", "passive"), ("1", "S2", "passive")],
    width=10.16, fp="xiao_solar:SC-70-6_L2.2-W1.3-P0.65-LS2.1-BR")

# ---- 1. Solar Input -> VBUS Rail -> BQ25798 Charger (as on xiao-solar-lite) ------------
BLOCK(20, 22, "1. ВХОД (5-22 V соларен панел) -> MPPT ЗАРЯДНО BQ25798 (PROG 1S, 750 kHz, 2.2 uH)")
P("J1", "Connector_Generic:Conn_01x02", "SOLAR", 28, 78, {"1": "SOLAR_IN", "2": "GND"}, CONN2, mirror=True)
P("D1", "Device:D_Schottky", "SS54", 46, 78, {"2": "SOLAR_IN", "1": "VBUS"}, "Diode_SMD:D_SMA", "C22452", mirror=True)
RAIL("SOLAR_IN", [(33.08, 78.0), (42.19, 78.0)])
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

RAIL("PMID", [(60.0, 120.08), (124.76, 120.08)])
C("C6", "10u/50V", 60, 128.5, "PMID", "GND", C10U50, C1206)
C("C7", "10u/50V", 72, 128.5, "PMID", "GND", C10U50, C1206)
C("C8", "10u/50V", 84, 128.5, "PMID", "GND", C10U50, C1206)
C("C9", "100n/50V", 96, 128.5, "PMID", "GND", C100N0402HV, C0402)
TP("TP4", "PMID", 104, 120.08, rot=0)

bq = {"1": "STAT", "2": "VBUS", "3": "VBUS", "4": "BTST1", "5": "REGN", "6": None, "7": None,
      "8": "VBUS", "9": "VBUS", "10": "GND", "11": "GND", "12": "QON", "13": "CE", "14": "SCL_I",
      "15": "SDA_I", "16": "TS", "17": "REGN", "18": "BATP", "19": "BTST2", "20": "PROG",
      "21": None, "22": "BAT", "23": "BAT", "24": "SDRV", "25": "SYS", "26": "SW2", "27": "GND",
      "28": "SW1", "29": "PMID"}
P("U1", "custom:BQ25798RQMR_FUNC", "BQ25798RQMR", 140, 125, bq,
  "xiao_solar:QFN-29_L4.0-W4.0-P0.40-TL-BQ25792RQMR", "C2876593")
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

TP("TP2", "QON", 112.0, 122.62, rot=90, value="QON (to GND = wake)")
RAIL("QON", [(112.0, 122.62), (124.76, 122.62)])
# ~CE: pulled up to 3V3M, ATtiny PC0 pulls it low = charge. No MCU = no charging.
R("R4", "100k", 118, 148, "3V3M", "CE", R100K, rot=180)
RAIL("CE", [(118.0, 125.16), (124.76, 125.16)])
RAIL("CE", [(118.0, 125.16), (118.0, 144.19)])

P("D4", "Device:LED", "green", 168, 127.70, {"1": "STAT", "2": "LED_STAT"},
  "LED_SMD:LED_0805_2012Metric", "C2297")
RAIL("STAT", [(155.24, 127.70), (164.19, 127.70)])
R("R6", "2.2k", 188, 127.70, "REGN", "LED_STAT", R2K2, rot=270)
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
# BQ I2C (internal bus, ATtiny is the master) + INT, pull-ups to 3V3M
TP("TP9", "SCL_I", 168, 112.46, rot=0)
TP("TP10", "SDA_I", 176, 115.00, rot=0)

T(20, 174, "SS54: защита от обратна полярност и връщане към панела. D3 (SMBJ24A) пази VBUS до 25 V.", 1.27)
T(20, 177.5, "PROG = 1S винаги (R1 4.7k): с 2S PROG BQ тръгва с VSYSMIN ~7 V. За LTO 2S ATtiny записва VREG 5.6 V и чак тогава пуска CE.", 1.27)
T(20, 181, "~CE: R4 към 3V3M = без заряд, докато ATtiny не го свали (PC0). TP2 (QON) към GND = излизане от ship режим.", 1.27)

# ---- 2. SYS -> 3V3M (HT7533) + LTC4412 ideal diode -> J6 -------------------------------
BLOCK(225, 22, "2. SYS -> 3V3M (HT7533, 2.5 uA) за ATtiny + J6 OUT (SYS през идеален диод LTC4412)")
RAIL("SYS", [(155.24, 109.92), (168.0, 109.92), (168.0, 55.0), (262.0, 55.0)])
for i, (ref, val, lcsc, fp) in enumerate([
        ("C16", "100n", C100N0402, C0402), ("C11", "10u/25V", C10U, C0805),
        ("C12", "10u/25V", C10U, C0805), ("C13", "10u/25V", C10U, C0805),
        ("C14", "10u/25V", C10U, C0805), ("C15", "10u/25V", C10U, C0805)]):
    C(ref, val, 180 + 12 * i, 64, "SYS", "GND", lcsc, fp)
TP("TP5", "SYS", 252, 55.0, rot=0)

P("U7", "custom:LTC4412_FUNC", "LTC4412ES6", 300, 72, {"1": "SYS", "2": "GND", "3": "GND", "4": None,
  "5": "OR_G", "6": "VOUT"}, "xiao_solar:TSOT-23-6_L2.9-W1.6-P0.95-LS2.8-BR", "C514442")
P("Q5", "Transistor_FET:BSS84", "AO3401A", 300, 52, {"1": "OR_G", "2": "VOUT", "3": "SYS"}, SOT23, "C15127", rot=90)
CS("C20", "10u/25V", 330, 64, "VOUT", "GND", C10U)
P("J6", "Connector_Generic:Conn_01x02", "OUT (SYS)", 350, 55, {"1": "VOUT", "2": "GND"}, CONN2)

P("U5", "custom:HT7533_FUNC", "HT7533-1", 300, 110, {"1": "GND", "2": "SYS", "3": "3V3M"},
  "xiao_solar:SOT-89-3_L4.5-W2.5-P1.50-LS4.2-BR", "C14289")
CS("C21", "10u/25V", 280, 118, "SYS", "GND", C10U)
CS("C22", "10u/25V", 320, 118, "3V3M", "GND", C10U)
TP("TP8", "3V3M", 335, 107.46, rot=0)
T(225, 140, "Q5 (AO3401A): D към SYS, S към J6, тялото му е диодът SYS -> OUT; LTC4412 го отваря (~20 mV пад, 11 uA, 2.5-28 V).", 1.27)
T(225, 143.5, "Товар със свое зарядно не може да върне ток в батерията. HT7533 храни само ATtiny (SYS до 5.6 V при LTO).", 1.27)

# ---- 3. Battery path: BAT -> Ship FET Q2 -> BATT+ -> J4 (1S or 2S) ----------------------
BLOCK(20, 186, "3. БАТЕРИЯ 1S Li-ion / LiFePO4 или 2S LTO: Ship FET (Q2 + JP3) -> J4 (BATT+, BM, BATT-)")
RAIL("BAT", [(155.24, 120.08), (158.0, 120.08), (158.0, 122.62), (155.24, 122.62)])
RAIL("BAT", [(158.0, 121.35), (232.0, 121.35), (232.0, 210.0), (259.92, 210.0)])
CS("C17", "10u/25V", 238, 219, "BAT", "GND", C10U)
CS("C18", "10u/25V", 248, 219, "BAT", "GND", C10U)
TP("TP6", "BAT", 232, 210.0, rot=0)
P("Q2", "Transistor_FET:BSS138", "AO3400A (ship FET)", 265, 207.46,
  {"1": "SDRV", "2": "BAT", "3": "BATT+"}, SOT23, "C20917", rot=270)
RAIL("SDRV", [(155.24, 117.54), (236.0, 117.54), (236.0, 198.0), (265.0, 198.0), (265.0, 202.38)])
P("JP3", "Jumper:SolderJumper_2_Open", "no ship FET", 265, 191.0,
  {"1": "BAT", "2": "BATT+"}, JP2P)
RAIL("BAT", [(255.0, 210.0), (255.0, 191.0), (259.92, 191.0)])
RAIL("BATT+", [(270.08, 191.0), (275.0, 191.0), (275.0, 210.0)])
RS("R8", "100", 265, 226.0, "BATT+", "BATP", "C22775", rot=270)
RAIL("BATP", [(155.24, 125.16), (228.0, 125.16), (228.0, 226.0), (261.19, 226.0)])
RAIL("BATT+", [(268.81, 226.0), (275.0, 226.0), (275.0, 210.0)])
RAIL("BATT+", [(270.08, 210.0), (300.0, 210.0)])
P("J4", "Connector_Generic:Conn_01x03", "BATTERY", 310, 212.54,
  {"1": "BATT_J", "2": "BM", "3": "BATT-"}, CONN3)
# battery fuse next to J4: opens if Q3 fails short (3 A, 1206)
P("F1", "Device:Fuse", "3A 1206 (0468003)", 300, 200, {"1": "BATT+", "2": "BATT_J"},
  "Fuse:Fuse_1206_3216Metric", "C41342", rot=90)
TP("TP7", "GND", 202, 210.0, rot=0)
RAIL("GND", [(202.0, 210.0), (202.0, 215.08)])
T(20, 252, "Q2 (AO3400A): S към BAT, D към батерията; в ship режим спира разреждането (UVLO, SDRV). JP3 затворен = без ship FET.", 1.27)
T(20, 255.5, "J4: 1S = BATT+ / BATT-, BM свободен. 2S LTO = BM е средната точка (балансиране).", 1.27)

# ---- 4. BMS: Q3 (HJ8205) in BATT-, 20 mOhm shunt, balancing -----------------------------
BLOCK(330, 186, "4. BMS: Q3 HJ8205 (OD разряд / OC заряд) + шунт 20 mOhm + балансиране 2S")
P("Q3", "custom:HJ8205_FUNC", "HJ8205", 360, 228.0,
  {"1": "BATT-", "2": "FS_D", "3": "BATT_S", "4": "OC_G", "5": "FS_D", "6": "OD_G"},
  "xiao_solar:SOT-23-6_L2.9-W1.6-P0.95-LS2.8-BL", "C20069150")
P("RS1", "Device:R", "20m 1W", 395, 236, {"1": "BATT_S", "2": "GND"}, R1206, "C154638")
# OD (discharge FET, source = BATT-): on by default through R18 to BATT+, ATtiny turns it
# off through Q6 (no MCU pin sees BATT+)
RS("R18", "1M", 340, 200, "BATT+", "OD_G", R1M)
P("Q6", "Transistor_FET:BSS138", "2N7002", 340, 250, {"1": "OD_CTL", "2": "GND", "3": "OD_G"}, SOT23, "C8545")
RS("R19", "100k", 326, 262, "OD_CTL", "GND", R100K)
# OC (charge FET, source = shunt): on as soon as 3V3M is up (R20), so discharge never runs
# through its body diode; the ATtiny PB2 pulls it low to stop charging. No ATtiny, no
# charging anyway: ~CE of the BQ25798 has its pull-up to 3V3M (R4)
RS("R20", "1M", 380, 262, "OC_G", "3V3M", R1M)
# shunt sense: 10k + 1M bias to 3V3M keeps both inputs above 0 V at -3.2 A
RS("R21", "10k", 420, 226, "BATT_S", "SH_P", R10K, rot=270)
RS("R22", "10k", 420, 246, "GND", "SH_N", R10K, rot=270)
RS("R23", "1M", 440, 212, "3V3M", "SH_P", R1M)
RS("R24", "1M", 452, 212, "3V3M", "SH_N", R1M)
# one differential filter cap across the PGA inputs: no CMRR loss from two caps to GND
# that do not match (fast LoRa TX current steps)
CS("C24", "100n", 440, 236, "SH_P", "SH_N", C100N, C0603)
# balancing: cell 1 (BATT- .. BM) by Q7 FET1, cell 2 (BM .. BATT+) by P-FET Q8 driven by Q7 FET2
P("Q7", "custom:2N7002DW_FUNC", "2N7002DW", 500, 236,
  {"5": "BAL1", "2": "BAL2", "3": "BAL1_D", "4": "BATT-", "6": "BAL2_G", "1": "GND"},
  "xiao_solar:SC-70-6_L2.2-W1.3-P0.65-LS2.1-BR", "C61840")
P("R37", "Device:R", "47 1206", 530, 226, {"1": "BM", "2": "BAL1_D"}, R1206, "C17969")
RS("R38", "100k", 478, 250, "BAL1", "GND", R100K)
RS("R39", "100k", 488, 260, "BAL2", "GND", R100K)
P("Q8", "Transistor_FET:BSS84", "AO3401A", 540, 205, {"1": "BAL2_G", "2": "BATT+", "3": "BAL2_D"}, SOT23, "C15127")
RS("R40", "100k", 520, 200, "BATT+", "BAL2_G", R100K)
P("R41", "Device:R", "47 1206", 555, 222, {"1": "BAL2_D", "2": "BM"}, R1206, "C17969")
# voltage dividers to the ATtiny ADC
RS("R25", "1M", 470, 280, "BATT+", "ADC_VBAT", R1M)
RS("R26", "470k", 470, 296, "ADC_VBAT", "GND", "C23178")
CS("C26", "100n", 482, 296, "ADC_VBAT", "GND", C100N, C0603)
RS("R27", "1M", 500, 280, "BM", "ADC_VBM", R1M)
RS("R28", "1M", 500, 296, "ADC_VBM", "GND", R1M)
CS("C27", "100n", 512, 296, "ADC_VBM", "GND", C100N, C0603)
T(330, 272, "Q3 е в минуса: S1 = BATT- (клетката), S2 = BATT_S -> RS1 -> GND. OD (G1) вкл. по подразбиране (R18), OC (G2) вкл. щом има 3V3M (R20), ATtiny го изключва.", 1.27)
T(330, 275.5, "Без DW01A: ток/късо пази ATtiny (ADC + компаратор AC0 на PA6/PA7) и BATFET OCP на BQ25798.", 1.27)
T(330, 279, "Шунт: + заряд, - разряд; R21-R24 изместват входовете с ~33 mV. Балансиране ~60 mA (47 Ohm).", 1.27)

# ---- 5. ATtiny3226 supervisor + INA260 emulator on J5 -----------------------------------
BLOCK(370, 22, "5. ATtiny3226: BQ25798 master (PC1/PC2), INA260 емулатор 0x41 (TWI PB1/PB0 -> J5), BMS, LED")
P("U4", "custom:ATTINY3226_FUNC", "ATtiny3226-SU", 470, 100,
  {"1": "3V3M", "16": "UPDI", "2": "ADC_VBAT", "3": "ADC_VBM", "17": "ADC_CHEM", "5": "SH_P", "4": "SH_N",
   "18": "PA2", "19": "REG_CTL", "10": "SDA_E", "11": "SCL_E", "13": "SDA_I", "14": "SCL_I",
   "12": "CE", "9": "OC_G", "8": "OD_CTL", "6": "BAL1", "7": "BAL2", "15": "LED", "20": "GND"},
  "xiao_solar:SOIC-20_L12.8-W7.5-P1.27-LS10.3-BL", "C3234898")
CS("C28", "100n", 420, 70, "3V3M", "GND", C100N, C0603)
CS("C29", "1u", 430, 70, "3V3M", "GND", "C15849", C0603)
RS("R13", "4.7k", 520, 60, "3V3M", "SDA_I", "C23162")
RS("R14", "4.7k", 530, 60, "3V3M", "SCL_I", "C23162")
TP("TP15", "PA2", 410, 130, rot=180, value="PA2 (spare)")
# chemistry jumper: open = Li-ion (1/2 3V3M), 1-2 = LiFePO4 (0 V), 2-3 = LTO 2S (3V3M)
RS("R29", "1M", 400, 150, "3V3M", "ADC_CHEM", R1M)
RS("R36", "1M", 400, 166, "ADC_CHEM", "GND", R1M)
P("JP4", "Jumper:SolderJumper_3_Open", "CHEM: - Li-ion / 1-2 LiFePO4 / 2-3 LTO", 420, 158,
  {"1": "GND", "2": "ADC_CHEM", "3": "3V3M"}, JP3P, rot=90)
CS("C30", "100n", 432, 166, "ADC_CHEM", "GND", C100N, C0603)
# two LEDs on one pin: PC3 high = red (error), low = yellow (BMS/balance), Hi-Z = both off
P("D6", "Device:LED", "red ERR", 540, 130, {"1": "LED_R", "2": "LED"}, "LED_SMD:LED_0805_2012Metric", "C84256", rot=90)
RS("R16", "2.2k", 540, 150, "LED_R", "GND", R2K2)
P("D7", "Device:LED", "yellow BMS", 555, 110, {"1": "LED", "2": "LED_Y"}, "LED_SMD:LED_0805_2012Metric", "C2296", rot=90)
RS("R17", "2.2k", 555, 90, "3V3M", "LED_Y", R2K2)
# host I2C: the repeater is the master and brings 3V3 + pull-ups (none on this board)
P("J5", "Connector_Generic:Conn_01x04", "I2C host (INA260 0x41)", 560, 60,
  {"1": "SCL_E", "2": "SDA_E", "3": "3V3E", "4": "GND"}, CONN4)
TP("TP11", "UPDI", 400, 95, rot=180)
TP("TP12", "3V3M", 400, 100, rot=180)
TP("TP13", "GND", 400, 105, rot=180)
T(370, 176, "J5 pin 3 (3V3E) е захранването на репийтъра: тук не се ползва, pull-up резисторите са на репийтъра.", 1.27)
T(370, 179.5, "TP11-TP13: UPDI програмиране (UPDI, 3V3M, GND). PA2 свободен (TP15; INT на BQ25798 е NC: пин 21 е между PROG и BAT), PA3 = управление на J7 (JP5 2-3).", 1.27)

# ---- 6. TPS63070 buck-boost -> J7 (3.3 / 5.0 V) -----------------------------------------
BLOCK(20, 300, "6. J7 U: TPS63070 buck-boost 3.3 V / 5.0 V (JP6), вкл. с JP5: - изкл. / 1-2 винаги / 2-3 от ATtiny")
P("U6", "custom:TPS63070_FUNC", "TPS63070RNMR", 120, 350,
  {"12": "SYS", "14": "REG_EN", "1": "LOGIC_HI", "15": "VSEL", "3": "VAUX", "7": "VREG", "5": "FB",
   "6": "FB2", "2": None, "11": "SWA", "9": "SWB", "4": "GND", "10": "GND"},
  "xiao_solar:VQFN-15_L3.0-W2.5-P0.50-BL", "C109322")
P("L2", "xiao_solar:FNR4030S2R2MT", "1.5uH 5A molded (MWSA0402S)", 120, 320, {"1": "SWA", "2": "SWB"},
  "xiao_solar:IND-SMD_L4.4-W4.2_MWSA0402S", "C408334")
CS("C31", "10u/25V", 60, 360, "SYS", "GND", C10U)
CS("C32", "10u/25V", 70, 360, "SYS", "GND", C10U)
CS("C33", "10u/25V", 80, 360, "SYS", "GND", C10U)
CS("C34", "100n", 92, 380, "VAUX", "GND", C100N, C0603)
RS("R30", "10k", 60, 330, "SYS", "LOGIC_HI", R10K)
RS("R31", "1M", 85, 395, "REG_EN", "GND", R1M)
RS("R32", "1M", 100, 395, "VSEL", "GND", R1M)
P("JP5", "Jumper:SolderJumper_3_Open", "EN: - off / 1-2 on / 2-3 MCU", 40, 380,
  {"1": "LOGIC_HI", "2": "REG_EN", "3": "REG_CTL"}, JP3P, rot=90)
P("JP6", "Jumper:SolderJumper_2_Open", "VSEL: open 3.3V / closed 5V", 60, 400, {"1": "LOGIC_HI", "2": "VSEL"}, JP2P)
RS("R33", "470k 1%", 160, 340, "VREG", "FB", "C23178")
RS("R34", "150k 1%", 160, 365, "FB", "GND", "C22807")
RS("R35", "220k 1%", 175, 365, "FB", "FB2", "C22961")
CS("C35", "10u/25V", 190, 360, "VREG", "GND", C10U)
CS("C36", "22u/25V", 200, 360, "VREG", "GND", C22U)
CS("C37", "22u/25V", 210, 360, "VREG", "GND", C22U)
CS("C38", "22u/25V", 220, 360, "VREG", "GND", C22U)
P("J7", "Connector_Generic:Conn_01x02", "U (3.3/5 V)", 245, 345, {"1": "VREG", "2": "GND"}, CONN2)
TP("TP14", "VREG", 232, 342.46, rot=0)
T(20, 402, "FB: R33/R34 = 3.3 V; VSEL високо свързва R35 паралелно на R34 = 5.0 V. EN, VSEL, PS/SYNC към VIN само през R30 10k (TI 11.1).", 1.27)
T(20, 405.5, "L2 1.5 uH + 3x22 uF (TI табл. 3). PS/SYNC високо = PFM при малък товар (~50 uA).", 1.27)

# ERC power flags on the source nets
for i, (net, fx, fy) in enumerate([
        ("VBUS", 55.0, 78.0), ("BATT+", 300.0, 210.0), ("BATT-", 345.0, 222.92), ("BM", 525.0, 226.0),
        ("GND", 202.0, 212.54), ("3V3E", 575.0, 65.08)]):
    P("#FLG%02d" % (i + 1), "power:PWR_FLAG", "PWR_FLAG", fx, fy, {"1": net})

for i in range(2):
    P("H%d" % (i + 1), "Mechanical:MountingHole", "M2", 575 + i * 15, 90, {},
      "MountingHole:MountingHole_2.2mm_M2", blk=-1)

write_bom = base.write_bom

def emit():
    return base.emit().replace('(paper "A3")', '(paper "A2")', 1)

if __name__ == "__main__":
    path = os.path.join(ROOT, base.PROJECT + ".kicad_sch")
    open(path, "w", encoding="utf-8").write(emit())
    g, h = write_bom()
    print("wrote", path, len(parts), "parts;", len(g), "JLC lines,", len(h), "hand parts")
