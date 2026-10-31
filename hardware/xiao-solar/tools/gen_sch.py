#!/usr/bin/env python3
"""Generates xiao-solar.kicad_sch (KiCad 7+) from the part list below.

Parts are arranged in a continuous left-to-right functional flow (Solar/VIN2 input ->
BQ25798 MPPT buck-boost charger with L1/bootstrap above and NTC/PROG below ->
SYS ideal-diode feed to XIAO+Wio & I2C on top-right, and BAT Ship-FET + INA219 +
DW01A/HJ8205 BMS + Battery on bottom-right) and wired with direct orthogonal nets.
Run: python3 tools/gen_sch.py  (from hardware/xiao-solar)
"""
import os, re, uuid

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SYSLIB = "/usr/share/kicad/symbols"
if not os.path.isdir(SYSLIB):
    SYSLIB = "C:/Program Files/KiCad/10.0/share/kicad/symbols"
LOCAL = os.path.join(ROOT, "lib", "xiao_solar.kicad_sym")
PROJECT = "xiao-solar"
ROOT_UUID = "6b1f3c1e-6a5e-4b7e-9d65-5f0b3a1d0001"
TITLE = "Xiao nRF52840 + Wio-SX1262 solar board"
REV = "0.4"

def uid():
    return str(uuid.uuid4())

# ---------------------------------------------------------------- symbols

def sexpr_block(text, start):
    depth = 0
    i = start
    in_str = False
    while i < len(text):
        c = text[i]
        if in_str:
            if c == '\\':
                i += 1
            elif c == '"':
                in_str = False
        elif c == '"':
            in_str = True
        elif c == '(':
            depth += 1
        elif c == ')':
            depth -= 1
            if depth == 0:
                return text[start:i + 1]
        i += 1
    raise ValueError("unbalanced")

_libcache = {}
def lib_text(lib):
    if lib not in _libcache:
        path = LOCAL if lib == "xiao_solar" else os.path.join(SYSLIB, lib + ".kicad_sym")
        _libcache[lib] = open(path, encoding="utf-8").read()
    return _libcache[lib]

def find_symbol(lib, name):
    t = lib_text(lib)
    m = re.search(r'\(symbol "%s"' % re.escape(name), t)
    if not m:
        raise KeyError(lib + ":" + name)
    return sexpr_block(t, m.start())

CUSTOM = {}

def custom_box(name, ref, left, right, top=(), bottom=(), width=15.24, fp=""):
    """left/right: lists of (number, name, type).
    top/bottom: lists of (number, name, type[, x_offset])."""
    rows = max(len(left), len(right), 1)
    if rows % 2 == 0:
        rows += 1
    h = rows * 2.54 + 2.54
    w = width
    top_y = h / 2
    out = ['(symbol "%s" (in_bom yes) (on_board yes)' % name,
           '  (property "Reference" "%s" (at 0 %.2f 0) (effects (font (size 1.27 1.27))))' % (ref, top_y + 5.08),
           '  (property "Value" "%s" (at 0 %.2f 0) (effects (font (size 1.27 1.27))))' % (name, top_y + 2.54),
           '  (property "Footprint" "%s" (at 0 0 0) (effects (font (size 1.27 1.27)) hide))' % fp,
           '  (property "Datasheet" "" (at 0 0 0) (effects (font (size 1.27 1.27)) hide))',
           '  (symbol "%s_0_1" (rectangle (start %.2f %.2f) (end %.2f %.2f) (stroke (width 0.254) (type default)) (fill (type background))))' % (name, -w / 2, top_y, w / 2, -top_y),
           '  (symbol "%s_1_1"' % name]
    def pin(num, pname, ptype, x, y, ang):
        out.append('    (pin %s line (at %.2f %.2f %d) (length 2.54) (name "%s" (effects (font (size 1.05 1.05)))) (number "%s" (effects (font (size 0.95 0.95)))))' % (ptype, x, y, ang, pname, num))
    for i, (n, pn, t) in enumerate(left):
        pin(n, pn, t, -w / 2 - 2.54, top_y - 2.54 * (i + 1), 0)
    for i, (n, pn, t) in enumerate(right):
        pin(n, pn, t, w / 2 + 2.54, top_y - 2.54 * (i + 1), 180)
    for i, item in enumerate(top):
        n, pn, t = item[:3]
        x = item[3] if len(item) > 3 else (-w / 2 + 2.54 * (i + 1))
        pin(n, pn, t, x, top_y + 2.54, 270)
    for i, item in enumerate(bottom):
        n, pn, t = item[:3]
        x = item[3] if len(item) > 3 else (-w / 2 + 2.54 * (i + 1))
        pin(n, pn, t, x, -top_y - 2.54, 90)
    out.append('  )')
    out.append(')')
    CUSTOM[name] = "\n".join(out)

# Functional schematic symbol for BQ25798RQMR (all 29 pins grouped by signal flow)
BQ_L = [("2", "VBUS", "power_in"), ("3", "VBUS", "power_in"),
        ("9", "VAC1", "input"), ("8", "VAC2", "input"),
        ("29", "PMID", "power_out"),
        ("12", "~{QON}", "input"), ("13", "~{CE}", "input"),
        ("6", "D+", "bidirectional"), ("7", "D-", "bidirectional"), ("21", "~{INT}", "open_collector")]
BQ_R = [("25", "SYS", "power_out"),
        ("14", "SCL", "input"), ("15", "SDA", "bidirectional"),
        ("24", "SDRV", "output"),
        ("22", "BAT", "power_out"), ("23", "BAT", "power_out"),
        ("18", "BATP", "input"),
        ("1", "STAT", "open_collector"),
        ("5", "REGN", "power_out"), ("17", "ILIM_HIZ", "input"),
        ("16", "TS", "input"), ("20", "PROG", "passive")]
BQ_T = [("4", "BTST1", "passive", -10.16), ("28", "SW1", "power_out", -5.08),
        ("26", "SW2", "power_out", 5.08), ("19", "BTST2", "passive", 10.16)]
BQ_B = [("10", "ACDRV2", "output", -2.54), ("11", "ACDRV1", "output", 0.0), ("27", "GND", "power_in", 2.54)]
custom_box("BQ25798RQMR_FUNC", "U", BQ_L, BQ_R, top=BQ_T, bottom=BQ_B, width=25.4,
           fp="xiao_solar:QFN-29_L4.0-W4.0-P0.40-TL-BQ25792RQMR")

XIAO_PINS_L = [("1", "D0/A0", "bidirectional"), ("2", "D1", "bidirectional"), ("3", "D2", "bidirectional"),
               ("4", "D3", "bidirectional"), ("5", "D4", "bidirectional"), ("6", "D5", "bidirectional"),
               ("7", "D6/SCL", "bidirectional")]
XIAO_PINS_R = [("14", "5V", "power_in"), ("13", "GND", "power_in"), ("12", "3V3", "power_out"),
               ("11", "D10/MOSI", "bidirectional"), ("10", "D9/MISO", "bidirectional"),
               ("9", "D8/SCK", "bidirectional"), ("8", "D7/SDA", "bidirectional")]
custom_box("XIAO_nRF52840", "U", XIAO_PINS_L, XIAO_PINS_R, width=17.78,
           fp="xiao_solar:XIAO_14P_2.54_21x17.8mm")

WIO_L = [("1", "D0", "passive"), ("2", "DIO1(D1)", "output"), ("3", "RST(D2)", "input"),
         ("4", "BUSY(D3)", "output"), ("5", "NSS(D4)", "input"), ("6", "RF_SW(D5)", "input"),
         ("7", "D6", "passive")]
WIO_R = [("14", "5V", "passive"), ("13", "GND", "power_in"), ("12", "3V3", "power_in"),
         ("11", "MOSI(D10)", "input"), ("10", "MISO(D9)", "output"), ("9", "SCK(D8)", "input"),
         ("8", "D7", "passive")]
custom_box("Wio-SX1262_for_XIAO", "U", WIO_L, WIO_R, width=22.86,
           fp="xiao_solar:XIAO_14P_2.54_21x17.8mm")

custom_box("LM66100", "U",
           left=[("1", "VIN", "power_in")],
           right=[("6", "VOUT", "power_out"), ("3", "~{CE}", "input")],
           bottom=[("4", "NC", "passive", -2.54), ("2", "GND", "power_in", 0.0), ("5", "~{ST}", "open_collector", 2.54)],
           width=12.7, fp="Package_TO_SOT_SMD:SOT-363_SC-70-6")

custom_box("DW01A_FUNC", "U",
           left=[("5", "VCC", "power_in"), ("6", "GND", "power_in"), ("4", "TD", "passive")],
           right=[("1", "OD", "output"), ("3", "OC", "output"), ("2", "CS", "input")],
           width=12.7, fp="xiao_solar:SOT-23-6_L2.9-W1.6-P0.95-LS2.8-BL")

custom_box("HJ8205_FUNC", "Q",
           left=[("1", "S1", "passive"), ("6", "G1", "input"), ("4", "G2", "input")],
           right=[("2", "D1/D2", "passive"), ("5", "D1/D2", "passive"), ("3", "S2", "passive")],
           width=12.7, fp="xiao_solar:SOT-23-6_L2.9-W1.6-P0.95-LS2.8-BL")

custom_box("INA219_FUNC", "U",
           left=[("4", "VS", "power_in")],
           right=[("5", "SCL", "input"), ("6", "SDA", "bidirectional")],
           top=[("2", "IN-", "input", -2.54), ("1", "IN+", "input", 2.54)],
           bottom=[("7", "A0", "input", -2.54), ("3", "GND", "power_in", 0.0), ("8", "A1", "input", 2.54)],
           width=12.7, fp="Package_TO_SOT_SMD:SOT-23-8")

custom_box("SHT40_FUNC", "U",
           left=[("2", "SCL", "input"), ("1", "SDA", "bidirectional"), ("3", "VDD", "power_in")],
           right=[],
           bottom=[("4", "VSS", "power_in", -1.27), ("5", "EP", "power_in", 1.27)],
           width=12.7, fp="xiao_solar:DFN-4_L1.5-W1.5-P0.8-TL-EP")

def install_custom():
    """Keeps the hand-made symbols in lib/xiao_solar.kicad_sym too."""
    t = open(LOCAL, encoding="utf-8").read()
    for name, body in CUSTOM.items():
        m = re.search(r'\n  \(symbol "%s"' % re.escape(name), t)
        if m:
            old = sexpr_block(t, m.start() + 3)
            t = t.replace(old, body)
        else:
            t = t.rstrip()[:-1].rstrip() + "\n  " + body.replace("\n", "\n  ") + "\n)\n"
    open(LOCAL, "w", encoding="utf-8").write(t)
    _libcache.pop("xiao_solar", None)

def symbol_def(lib_id):
    lib, name = lib_id.split(":")
    if lib == "custom":
        body = CUSTOM[name]
        lib_id = "xiao_solar:" + name
    else:
        body = find_symbol(lib, name)
        m = re.search(r'\(extends "([^"]+)"\)', body)
        if m:
            parent = m.group(1)
            body = find_symbol(lib, parent).replace(parent, name)
    return body.replace('(symbol "%s"' % name, '(symbol "%s"' % lib_id, 1)

def symbol_pins(lib_id):
    body = symbol_def(lib_id)
    pins = []
    for m in re.finditer(r'\(pin (\w+) \w+\s*\(at ([-\d.]+) ([-\d.]+) (\d+)\)\s*\(length ([-\d.]+)\)(.*?)\(number "([^"]*)"', body, re.S):
        pins.append(dict(type=m.group(1), x=float(m.group(2)), y=float(m.group(3)),
                         ang=int(m.group(4)), num=m.group(7), length=float(m.group(5))))
    return pins

# ---------------------------------------------------------------- design

parts = []      # dict(ref, lib, value, x, y, nets, fp, lcsc, dnp, note)
texts = []
blocks = []     # visual section titles
rails = []      # (net, [(x1, y1), (x2, y2), ...]) explicit backbone wires
OFF = [0.0, 0.0]

def BLOCK(x, y, title):
    blocks.append(title)
    T(x, y, title, 1.8)

def RAIL(net, pts):
    rails.append((net, [(x + OFF[0], y + OFF[1]) for (x, y) in pts]))

def P(ref, lib, value, x, y, nets, fp="", lcsc="", dnp=False, mpn="", rot=0, mirror=False, blk=None):
    """rot: 0/90/180/270 (counter-clockwise), mirror: flipped left-right (not with rot)."""
    assert not (rot and mirror)
    x, y = x + OFF[0], y + OFF[1]
    b_idx = 0 if blk is None else blk
    parts.append(dict(ref=ref, lib=lib, value=value, x=x, y=y, nets=nets, fp=fp, lcsc=lcsc, dnp=dnp, mpn=mpn,
                      block=b_idx, section=max(len(blocks) - 1, 0), rot=rot, mirror=mirror))

TP_FP = "TestPoint:TestPoint_Pad_D1.0mm"
def TP(ref, net, x, y, rot=0):
    P(ref, "Connector:TestPoint", net, x, y, {"1": net}, TP_FP, rot=rot)

def T(x, y, s, size=1.8):
    texts.append((x + OFF[0], y + OFF[1], s, size, max(len(blocks) - 1, 0)))

R0603 = "Resistor_SMD:R_0603_1608Metric"
C0603 = "Capacitor_SMD:C_0603_1608Metric"
C0805 = "Capacitor_SMD:C_0805_2012Metric"
SOT23 = "Package_TO_SOT_SMD:SOT-23"
CONN2 = "Connector_PinHeader_2.54mm:PinHeader_1x02_P2.54mm_Vertical"
CONN4 = "Connector_PinHeader_2.54mm:PinHeader_1x04_P2.54mm_Vertical"

def R(ref, val, x, y, a, b, lcsc, dnp=False, rot=0):
    P(ref, "Device:R", val, x, y, {"1": a, "2": b}, R0603, lcsc, dnp, rot=rot)

def C(ref, val, x, y, a, b, lcsc, fp=C0805, rot=0):
    P(ref, "Device:C", val, x, y, {"1": a, "2": b}, fp, lcsc, rot=rot)

C10U = "C15850"          # 10uF 25V X5R 0805
C10U50 = "C13585"        # 10uF 50V X5R 1206 (VBUS/PMID: derating at 25 V)
C1206 = "Capacitor_SMD:C_1206_3216Metric"
C100N = "C14663"         # 100nF 50V X7R 0603
C47N = "C1622"           # 47nF 50V 0603
C4U7 = "C1779"           # 4.7uF 25V 0805
C0402 = "Capacitor_SMD:C_0402_1005Metric"
C100N0402 = "C1525"      # 100nF 16V X7R 0402 (SYS)
C100N0402HV = "C131394"  # 100nF 50V X7R 0402 (PMID: up to 25 V)

# ==============================================================================
# CONNECTED SCHEMATIC LAYOUT (A3: 420 x 297 mm)
# U1 (BQ25798, rows=13 -> top_y=17.78) sits at (140, 125):
#   Left pins (10):  x = 124.76, y = 109.92 + 2.54*i (i=0..9)
#   Right pins (12): x = 155.24, y = 109.92 + 2.54*i (i=0..11)
#   Top pins (4):    y = 104.68, x = 129.84, 134.92, 145.08, 150.16
#   Bottom pins (3): y = 145.32, x = 137.46, 140.00, 142.54
# ==============================================================================

# ---- 1. Solar & Aux Inputs -> VBUS & PMID rails (Left of U1) -----------------
BLOCK(20, 22, "1. ВХОДОВЕ (5-22 V соларен панел + втори вход) -> MPPT ЗАРЯДНО BQ25798 (1S, 750 kHz, 2.2 uH)")
P("J1", "Connector_Generic:Conn_01x02", "SOLAR 5-22V", 28, 78.0, {"1": "SOLAR_IN", "2": "GND"}, CONN2, mirror=True)
P("D1", "Device:D_Schottky", "SS54", 46, 78.0, {"2": "SOLAR_IN", "1": "VBUS"}, "Diode_SMD:D_SMA", "C22452", mirror=True)
P("J2", "Connector_Generic:Conn_01x02", "VIN2 5-20V", 28, 95.0, {"1": "VIN2_IN", "2": "GND"}, CONN2, mirror=True)
P("D2", "Device:D_Schottky", "SS54", 46, 95.0, {"2": "VIN2_IN", "1": "VBUS"}, "Diode_SMD:D_SMA", "C22452", mirror=True)

# VBUS rail at y = 78.0 from D1 (x=49.81) across D3, C1..C3, TP3 and down into U1.VBUS/VAC1/VAC2
RAIL("VBUS", [(49.81, 78.0), (116.0, 78.0), (116.0, 109.92), (124.76, 109.92)])
RAIL("VBUS", [(49.81, 95.0), (55.0, 95.0), (55.0, 78.0)])
RAIL("VBUS", [(116.0, 109.92), (116.0, 117.54), (124.76, 117.54)])
RAIL("VBUS", [(116.0, 112.46), (124.76, 112.46)])
RAIL("VBUS", [(116.0, 115.00), (124.76, 115.00)])

P("D3", "xiao_solar:SMBJ24A-C87268", "SMBJ24A", 64, 86.5, {"1": "VBUS", "2": "GND"}, "xiao_solar:SMB_L4.6-W3.6-LS5.3-RD", "C19077578", rot=270)
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
P("L1", "xiao_solar:FNR4030S2R2MT", "2.2uH 4A (MWSA0402S)", 140, 72, {"1": "SW1", "2": "SW2"},
  "xiao_solar:IND-SMD_L4.4-W4.2_MWSA0402S", "C408335")
RAIL("SW1", [(134.92, 104.68), (134.92, 72.0)])
RAIL("SW2", [(145.08, 104.68), (145.08, 72.0)])
C("C4", "47n", 125.0, 88, "BTST1", "SW1", C47N, C0603)
RAIL("BTST1", [(129.84, 104.68), (129.84, 81.0), (125.0, 81.0), (125.0, 84.19)])
RAIL("SW1", [(125.0, 91.81), (125.0, 95.0), (134.92, 95.0)])
C("C5", "47n", 155.0, 88, "BTST2", "SW2", C47N, C0603)
RAIL("BTST2", [(150.16, 104.68), (150.16, 81.0), (155.0, 81.0), (155.0, 84.19)])
RAIL("SW2", [(155.0, 91.81), (155.0, 95.0), (145.08, 95.0)])

# QON & CE (JP2 Li-ion/LiFePO4) on the lower-left of U1
TP("TP2", "QON", 114.54, 122.62, rot=90)
RAIL("QON", [(114.54, 122.62), (124.76, 122.62)])

R("R4", "100k", 108, 148, "CE", "CE_PULL", "C25803")
RAIL("CE", [(108.0, 125.16), (124.76, 125.16)])
RAIL("CE", [(108.0, 125.16), (108.0, 144.19)])
P("JP2", "Jumper:SolderJumper_3_Bridged12", "1-2 Li-ion / 2-3 LiFePO4", 108, 162,
  {"1": "GND", "2": "CE_PULL", "3": "3V3"}, "Jumper:SolderJumper-3_P1.3mm_Bridged12_RoundedPad1.0x1.5mm", rot=180)
RAIL("CE_PULL", [(108.0, 151.81), (108.0, 158.19)])

# STAT LED (D4 + R6), REGN (C10), NTC divider (R2/R3/J3), PROG (R1) on the bottom-right of U1
# U1 right pins: STAT=127.70, REGN=130.24, ILIM_HIZ=132.78, TS=135.32, PROG=137.86
P("D4", "Device:LED", "green", 172, 127.70, {"1": "STAT", "2": "LED_STAT"},
  "LED_SMD:LED_0805_2012Metric", "C2297")
RAIL("STAT", [(155.24, 127.70), (168.19, 127.70)])
R("R6", "2.2k", 184, 127.70, "LED_STAT", "REGN", "C4190", rot=90)
RAIL("LED_STAT", [(175.81, 127.70), (180.19, 127.70)])

RAIL("REGN", [(155.24, 130.24), (196.0, 130.24)])
RAIL("REGN", [(155.24, 132.78), (160.0, 132.78), (160.0, 130.24)])
RAIL("REGN", [(187.81, 127.70), (190.0, 127.70), (190.0, 130.24)])
C("C10", "4.7u/25V", 182, 142, "REGN", "GND", C4U7)
RAIL("REGN", [(182.0, 130.24), (182.0, 138.19)])

R("R2", "5.1k 1%", 196, 142, "REGN", "TS", "C23186")
RAIL("REGN", [(196.0, 130.24), (196.0, 138.19)])
RAIL("TS", [(155.24, 135.32), (172.0, 135.32), (172.0, 150.0), (210.0, 150.0)])
RAIL("TS", [(196.0, 145.81), (196.0, 150.0)])
R("R3", "30k 1%", 196, 160, "TS", "GND", "C22984")
RAIL("TS", [(196.0, 150.0), (196.0, 156.19)])
P("J3", "Connector_Generic:Conn_01x02", "NTC 10k (103AT)", 215.08, 160, {"1": "TS", "2": "GND"}, CONN2)
RAIL("TS", [(210.0, 150.0), (210.0, 160.0)])

R("R1", "4.7k 1%", 164, 152, "PROG", "GND", "C23162")
RAIL("PROG", [(155.24, 137.86), (164.0, 137.86), (164.0, 148.19)])

T(20, 174, "SS54 = OR на двата входа + защита от обратна полярност. D3 (SMBJ24A) пази VBUS до 25 V.", 1.27)
T(20, 177.5, "CE (D0): JP2 1-2 (Li-ion) зарежда автоматично; 2-3 (LiFePO4) изчаква фърмуера (VREG=3.6V, D0=0). TP2 (QON) към GND = wake.", 1.27)

# ---- 3. SYS Rail -> Ideal Diode D5 (LM66100) -> XIAO nRF52840 + Wio-SX1262 ---
BLOCK(225, 22, "2. СИСТЕМНО ЗАХРАНВАНЕ (SYS -> LM66100 -> XIAO 5V) + XIAO nRF52840 & Wio-SX1262 + I2C")
# Continuous SYS wire from U1.SYS (155.24, 109.92) up to y = 55 and right into D5.VIN (266.11, 55)
RAIL("SYS", [(155.24, 109.92), (168.0, 109.92), (168.0, 55.0), (266.11, 55.0)])
for i, (ref, val, lcsc, fp) in enumerate([
        ("C16", "100n", C100N0402, C0402), ("C11", "10u/25V", C10U, C0805),
        ("C12", "10u/25V", C10U, C0805), ("C13", "10u/25V", C10U, C0805),
        ("C14", "10u/25V", C10U, C0805), ("C15", "10u/25V", C10U, C0805)]):
    C(ref, val, 180 + 12 * i, 64, "SYS", "GND", lcsc, fp)
TP("TP5", "SYS", 252, 55.0, rot=0)

P("D5", "custom:LM66100", "LM66100", 275, 57.54,
  {"1": "SYS", "2": "GND", "3": "XIAO_5V", "4": "GND", "5": "GND", "6": "XIAO_5V"},
  "Package_TO_SOT_SMD:SOT-363_SC-70-6", "C2869734")
RAIL("XIAO_5V", [(283.89, 55.0), (305.0, 55.0)])
RAIL("XIAO_5V", [(283.89, 57.54), (288.0, 57.54), (288.0, 55.0)])
C("C20", "10u/25V", 296, 64, "XIAO_5V", "GND", C10U)

# Both U3 (XIAO nRF52840) and U4 (Wio-SX1262) have the EXACT SAME Top-View orientation (D0..D6 left, 5V..D7 right)!
P("U3", "custom:XIAO_nRF52840", "XIAO nRF52840", 342, 62.62,
  {"1": "CE", "2": "LORA_DIO1", "3": "LORA_RST", "4": "LORA_BUSY", "5": "LORA_NSS",
   "6": "LORA_RXEN", "7": "SCL", "14": "XIAO_5V", "13": "GND", "12": "3V3",
   "11": "SPI_MOSI", "10": "SPI_MISO", "9": "SPI_SCK", "8": "SDA"}, "xiao_solar:XIAO_14P_2.54_21x17.8mm")
# Connect XIAO_5V directly from D5/C20 over the top of U3 into U3 pin 14 (5V at 353.43, 55.0)
RAIL("XIAO_5V", [(305.0, 55.0), (305.0, 42.0), (358.0, 42.0), (358.0, 55.0), (353.43, 55.0)])
# Connect CE directly from U1.~CE (108.0, 125.16) over the top lane (y=36.0) into U3.D0 (330.57, 55.0)
RAIL("CE", [(108.0, 125.16), (108.0, 36.0), (326.0, 36.0), (326.0, 55.0), (330.57, 55.0)])

P("U4", "custom:Wio-SX1262_for_XIAO", "Wio-SX1262 for XIAO", 342, 108.34,
  {"1": None, "2": "LORA_DIO1", "3": "LORA_RST", "4": "LORA_BUSY", "5": "LORA_NSS",
   "6": "LORA_RXEN", "7": None, "14": None, "13": "GND", "12": "3V3",
   "11": "SPI_MOSI", "10": "SPI_MISO", "9": "SPI_SCK", "8": None}, "xiao_solar:XIAO_14P_2.54_21x17.8mm")

# Clean parallel bus connecting U3 (XIAO) and U4 (Wio-SX1262) with identical pinouts
for i, net in enumerate(["LORA_DIO1", "LORA_RST", "LORA_BUSY", "LORA_NSS", "LORA_RXEN"]):
    y_u3 = 57.54 + 2.54 * i
    y_u4 = 103.26 + 2.54 * i
    lane_x = 324.0 - 2.54 * i
    RAIL(net, [(330.57, y_u3), (lane_x, y_u3), (lane_x, y_u4), (328.03, y_u4)])

for i, net in enumerate(["3V3", "SPI_MOSI", "SPI_MISO", "SPI_SCK"]):
    y_u3 = 60.08 + 2.54 * i
    y_u4 = 105.80 + 2.54 * i
    lane_x = 370.0 + 2.54 * i
    RAIL(net, [(353.43, y_u3), (lane_x, y_u3), (lane_x, y_u4), (355.97, y_u4)])
RAIL("GND", [(353.43, 57.54), (362.0, 57.54), (362.0, 122.0)])
RAIL("GND", [(355.97, 103.26), (362.0, 103.26)])

# 3V3 decoupling caps right on the 3V3 rail next to U3/U4
RAIL("3V3", [(370.0, 60.08), (398.0, 60.08)])
C("C21", "10u/25V", 386, 69, "3V3", "GND", C10U)
C("C22", "100n", 398, 69, "3V3", "GND", C100N, C0603)
TP("TP8", "3V3", 386, 60.08, rot=0)

# I2C bus (SCL, SDA, 3V3) + Pull-ups R11/R12 + J5 + SHT40 (U5) — wired directly to U1 and U3!
P("J5", "Connector_Generic:Conn_01x04", "I2C rail", 252, 146.0,
  {"1": "SCL", "2": "SDA", "3": "3V3", "4": "GND"}, CONN4, mirror=True)
R("R11", "4.7k", 272, 134, "3V3", "SCL", "C23162")
R("R12", "4.7k", 282, 134, "3V3", "SDA", "C23162")
TP("TP9", "SCL", 292, 143.46, rot=0)
TP("TP10", "SDA", 300, 146.00, rot=0)
P("U5", "custom:SHT40_FUNC", "SHT40-AD1B", 332, 146.0,
  {"1": "SDA", "2": "SCL", "3": "3V3", "4": "GND", "5": "GND"},
  "xiao_solar:DFN-4_L1.5-W1.5-P0.8-TL-EP", "C2848306")
C("C23", "100n", 316, 156, "3V3", "GND", C100N, C0603)

# Wire SCL & SDA continuously from U1 (155.24, 112.46 & 115.0) across J5, R11, R12, TP9, TP10, U5, U3, and down to U7!
RAIL("SCL", [(257.08, 143.46), (323.11, 143.46)])
RAIL("SDA", [(257.08, 146.00), (323.11, 146.00)])
RAIL("SCL", [(155.24, 112.46), (262.0, 112.46), (262.0, 143.46)])
RAIL("SDA", [(155.24, 115.00), (260.0, 115.00), (260.0, 146.00)])
RAIL("SCL", [(272.0, 137.81), (272.0, 143.46)])
RAIL("SDA", [(282.0, 137.81), (282.0, 146.00)])
RAIL("3V3", [(257.08, 148.54), (323.11, 148.54)])
RAIL("3V3", [(272.0, 130.19), (282.0, 130.19), (282.0, 126.0), (264.0, 126.0), (264.0, 148.54)])
RAIL("3V3", [(316.0, 148.54), (316.0, 152.19)])
RAIL("3V3", [(370.0, 105.80), (370.0, 148.54), (323.11, 148.54)])
RAIL("SCL", [(330.57, 70.24), (310.0, 70.24), (310.0, 143.46)])
RAIL("SDA", [(353.43, 70.24), (364.0, 70.24), (364.0, 146.00), (323.11, 146.00)])

# ---- 4. Battery Path: BAT -> Ship FET Q2 (+JP3) -> Shunt RS1 (INA219) -> J4 + BMS ----
BLOCK(20, 188, "3. БАТЕРИЯ 1S (Li-ion / LiFePO4): Ship FET (Q2 + JP3) -> Шунт RS1 (INA219) -> BMS (DW01A + HJ8205) -> J4")
# Continuous BAT wire from U1.BAT (155.24, 120.08 & 122.62) down to y = 210 and right into Q2.2 (Source at 214.92, 210.0)
RAIL("BAT", [(155.24, 120.08), (158.0, 120.08), (158.0, 122.62), (155.24, 122.62)])
RAIL("BAT", [(158.0, 121.35), (222.0, 121.35), (222.0, 210.0), (254.92, 210.0)])
C("C17", "10u/25V", 228, 219, "BAT", "GND", C10U)
C("C18", "10u/25V", 240, 219, "BAT", "GND", C10U)
TP("TP6", "BAT", 222, 210.0, rot=0)

# Ship FET Q2 (rot=270 at (260, 207.46): Pin 1 Gate=(260, 202.38), Pin 2 Source=(254.92, 210.0), Pin 3 Drain=(265.08, 210.0))
P("Q2", "Transistor_FET:BSS138", "AO3400A (ship FET)", 260, 207.46,
  {"1": "SDRV", "2": "BAT", "3": "BATS"}, SOT23, "C20917", rot=270)
RAIL("SDRV", [(155.24, 117.54), (226.0, 117.54), (226.0, 198.0), (260.0, 198.0), (260.0, 202.38)])
P("JP3", "Jumper:SolderJumper_2_Open", "no ship FET (LiFePO4)", 260, 191.0,
  {"1": "BAT", "2": "BATS"}, "Jumper:SolderJumper-2_P1.3mm_Open_RoundedPad1.0x1.5mm")
RAIL("BAT", [(250.0, 210.0), (250.0, 191.0), (254.92, 191.0)])
RAIL("BATS", [(265.08, 191.0), (270.0, 191.0), (270.0, 210.0)])

# BATP Kelvin sense through R8 (100R) below Q2 to BATS (no crossover with SDRV!)
R("R8", "100", 260, 224.0, "BATP", "BATS", "C22775", rot=90)
RAIL("BATP", [(155.24, 125.16), (218.0, 125.16), (218.0, 224.0), (256.19, 224.0)])
RAIL("BATS", [(263.81, 224.0), (270.0, 224.0), (270.0, 210.0)])

# Current sense shunt RS1 (0.05R 1W, rot=270 so Pin 2 BATS is at left 291.19 and Pin 1 BATT+ is at right 298.81!)
RAIL("BATS", [(265.08, 210.0), (291.19, 210.0)])
TP("TP11", "BATS", 276, 210.0, rot=0)
P("RS1", "Device:R", "0.05 1W", 295, 210.0, {"1": "BATT+", "2": "BATS"},
  "Resistor_SMD:R_1206_3216Metric", "C154645", rot=270)
P("U7", "custom:INA219_FUNC", "INA219AIDCNR", 295, 229.0,
  {"1": "BATT+", "2": "BATS", "3": "GND", "4": "3V3", "5": "SCL", "6": "SDA", "7": "GND", "8": "GND"},
  "Package_TO_SOT_SMD:SOT-23-8", "C87469")
RAIL("BATS", [(292.46, 221.38), (292.46, 218.0), (288.0, 218.0), (288.0, 210.0)])
RAIL("BATT+", [(297.54, 221.38), (297.54, 218.0), (302.0, 218.0), (302.0, 210.0)])
C("C24", "100n", 278, 233, "3V3", "GND", C100N, C0603)
RAIL("3V3", [(278.0, 229.19), (278.0, 226.46), (286.11, 226.46)])
RAIL("SCL", [(303.89, 226.46), (310.0, 226.46), (310.0, 143.46)])
RAIL("SDA", [(303.89, 229.00), (313.0, 229.00), (313.0, 146.00)])

# BATT+ rail continues right from RS1 (298.81, 210.0) to J4 (BATTERY) pin 1 (374.92, 210.0)
RAIL("BATT+", [(298.81, 210.0), (374.92, 210.0)])
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

# Direct horizontal gate wires DW_OD and DW_OC from U2 to Q3, plus BATT- and DW_CS (R10)
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

# ERC power flags placed directly on their rail wires (so no detached PWR_FLAG box is needed!)
for i, (net, fx, fy) in enumerate([
        ("VBUS", 55.0, 78.0), ("PMID", 112.0, 120.08), ("SYS", 172.0, 55.0),
        ("BAT", 222.0, 150.0), ("BATS", 275.0, 210.0), ("BATT+", 318.0, 210.0),
        ("BATT-", 355.0, 212.54), ("REGN", 170.0, 130.24), ("XIAO_5V", 290.0, 55.0),
        ("3V3", 376.0, 60.08), ("GND", 202.0, 212.54)]):
    P("#FLG%02d" % (i + 1), "power:PWR_FLAG", "PWR_FLAG", fx, fy, {"1": net})
for i in range(4):
    P("H%d" % (i + 1), "Mechanical:MountingHole", "M2/M2.5", 370 + (i % 2) * 18, 252 + (i // 2) * 10, {},
      "xiao_solar:MountingHole_2.8mm_Keepout6mm", blk=-1)

# ---------------------------------------------------------------- writer

def fmt(v):
    s = "%.4f" % v
    s = s.rstrip("0").rstrip(".")
    return "0" if s == "-0" else s

POWER = ["GND", "3V3"]
OUTWARD = {0: (-1, 0), 180: (1, 0), 90: (0, 1), 270: (0, -1)}
EPS = 1e-6
NC_POINTS = []

def power_symbol(net):
    """GND hangs down from its pin, the rails stand up from it (bar + name)."""
    name = "PWR_" + net
    if net == "GND":
        g = '(polyline (pts (xy 0 0) (xy 0 -1.27) (xy 1.27 -1.27) (xy 0 -2.54) (xy -1.27 -1.27) (xy 0 -1.27)) (stroke (width 0) (type default)) (fill (type none)))'
        vy = -3.81
    else:
        g = ('(polyline (pts (xy 0 0) (xy 0 1.27)) (stroke (width 0) (type default)) (fill (type none))) '
             '(polyline (pts (xy -1.27 1.27) (xy 1.27 1.27)) (stroke (width 0.254) (type default)) (fill (type none)))')
        vy = 2.54
    return name, ('(symbol "xiao_solar:%s" (power) (pin_names (offset 0)) (in_bom yes) (on_board yes)\n'
                  '  (property "Reference" "#PWR" (at 0 -5 0) (effects (font (size 1.27 1.27)) hide))\n'
                  '  (property "Value" "%s" (at 0 %s 0) (effects (font (size 1.27 1.27))))\n'
                  '  (property "Footprint" "" (at 0 0 0) (effects (font (size 1.27 1.27)) hide))\n'
                  '  (property "Datasheet" "" (at 0 0 0) (effects (font (size 1.27 1.27)) hide))\n'
                  '  (symbol "%s_0_1" %s)\n'
                  '  (symbol "%s_1_1" (pin power_in line (at 0 0 90) (length 0) hide (name "%s" (effects (font (size 1.27 1.27)))) (number "1" (effects (font (size 1.27 1.27))))))\n'
                  ')') % (name, net, fmt(vy), name, g, name, net)

class Sheet:
    """Wire bookkeeping with the checks that keep the netlist exact: a new segment may not
    touch another net's pin, wire or symbol body, and runs only over its own net."""
    def __init__(self):
        self.wires = []      # (net, x1, y1, x2, y2)
        self.bodies = []     # (ref, x0, y0, x1, y1)
        self.pins = []       # (net, ref, x, y)
        self.texts = []      # (x0, y0, x1, y1)

    @staticmethod
    def _on(px, py, x1, y1, x2, y2, tol=0.05):
        if abs(x1 - x2) < EPS:
            return abs(px - x1) < tol and min(y1, y2) - tol <= py <= max(y1, y2) + tol
        return abs(py - y1) < tol and min(x1, x2) - tol <= px <= max(x1, x2) + tol

    @staticmethod
    def _hits_box(x1, y1, x2, y2, b, grow=0.0):
        bx0, by0, bx1, by1 = b[0] - grow, b[1] - grow, b[2] + grow, b[3] + grow
        sx0, sx1, sy0, sy1 = min(x1, x2), max(x1, x2), min(y1, y2), max(y1, y2)
        return sx1 > bx0 and sx0 < bx1 and sy1 > by0 and sy0 < by1

    def seg_ok(self, net, x1, y1, x2, y2, own=()):
        if abs(x1 - x2) > EPS and abs(y1 - y2) > EPS:
            return False
        for (n, r, px, py) in self.pins:
            if n != net and self._on(px, py, x1, y1, x2, y2, 0.4):
                return False
        for (r, bx0, by0, bx1, by1) in self.bodies:
            if self._hits_box(x1, y1, x2, y2, (bx0, by0, bx1, by1), 0.0 if r in own else 0.4):
                return False
        for t in self.texts:
            if self._hits_box(x1, y1, x2, y2, t):
                return False
        for (n, a1, b1, a2, b2) in self.wires:
            if n == net:
                continue
            if self._on(x1, y1, a1, b1, a2, b2, 0.4) or self._on(x2, y2, a1, b1, a2, b2, 0.4):
                return False
            if self._on(a1, b1, x1, y1, x2, y2, 0.4) or self._on(a2, b2, x1, y1, x2, y2, 0.4):
                return False
            hor1, hor2 = abs(y1 - y2) < EPS, abs(b1 - b2) < EPS
            if hor1 == hor2:
                if hor1 and abs(y1 - b1) < 0.6 and min(x1, x2) < max(a1, a2) and min(a1, a2) < max(x1, x2):
                    return False
                if not hor1 and abs(x1 - a1) < 0.6 and min(y1, y2) < max(b1, b2) and min(b1, b2) < max(y1, y2):
                    return False
        return True

    def box_free(self, net, b):
        for (n, a1, b1, a2, b2) in self.wires:
            if self._hits_box(a1, b1, a2, b2, b, 0.3):
                return False
        for (n, r, px, py) in self.pins:
            if b[0] - 0.3 < px < b[2] + 0.3 and b[1] - 0.3 < py < b[3] + 0.3:
                return False
        for (r, bx0, by0, bx1, by1) in self.bodies:
            if self._hits_box(b[0], b[1], b[2], b[3], (bx0, by0, bx1, by1)):
                return False
        return not any(self._hits_box(b[0], b[1], b[2], b[3], t) for t in self.texts)

    def path_ok(self, net, pts, own=()):
        return all(self.seg_ok(net, a[0], a[1], b[0], b[1], own) for a, b in zip(pts, pts[1:])
                   if abs(a[0] - b[0]) > EPS or abs(a[1] - b[1]) > EPS)

    def add(self, net, pts):
        for a, b in zip(pts, pts[1:]):
            if abs(a[0] - b[0]) > EPS or abs(a[1] - b[1]) > EPS:
                self.wires.append((net, a[0], a[1], b[0], b[1]))

def _routes(a, b):
    (ax, ay, (adx, ady)), (bx, by, (bdx, bdy)) = a, b
    if abs(ax - bx) < EPS or abs(ay - by) < EPS:
        yield [(ax, ay), (bx, by)]
    yield [(ax, ay), (bx, ay), (bx, by)]
    yield [(ax, ay), (ax, by), (bx, by)]
    for d in (2.54, 5.08, 7.62, 10.16):
        sa = (ax + adx * d, ay + ady * d)
        sb = (bx + bdx * d, by + bdy * d)
        yield [(ax, ay), sa, (sa[0], by), (bx, by)]
        yield [(ax, ay), sa, (bx, sa[1]), (bx, by)]
        yield [(ax, ay), sa, (sa[0], sb[1]), sb, (bx, by)]
        yield [(ax, ay), sa, (sb[0], sa[1]), sb, (bx, by)]
    for d in (2.54, 5.08, 7.62, 10.16):
        sa = (ax + adx * d, ay + ady * d)
        sb = (bx + bdx * d, by + bdy * d)
        for k in range(1, 20):
            for lane in (max(sa[1], sb[1]) + 2.54 * k, min(sa[1], sb[1]) - 2.54 * k):
                yield [(ax, ay), sa, (sa[0], lane), (sb[0], lane), sb, (bx, by)]
            for lane in (max(sa[0], sb[0]) + 2.54 * k, min(sa[0], sb[0]) - 2.54 * k):
                yield [(ax, ay), sa, (lane, sa[1]), (lane, sb[1]), sb, (bx, by)]

def _segs_touch(s1, s2):
    _, x1, y1, x2, y2 = s1
    _, a1, b1, a2, b2 = s2
    return (Sheet._on(x1, y1, a1, b1, a2, b2, 0.05) or Sheet._on(x2, y2, a1, b1, a2, b2, 0.05) or
            Sheet._on(a1, b1, x1, y1, x2, y2, 0.05) or Sheet._on(a2, b2, x1, y1, x2, y2, 0.05))

def _build_net_comps(net, qs, wires):
    """Groups pins in qs that already touch connected wire components of `net`."""
    nsegs = [w for w in wires if w[0] == net]
    parent = list(range(len(nsegs)))
    def find(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i
    for i in range(len(nsegs)):
        for j in range(i + 1, len(nsegs)):
            if _segs_touch(nsegs[i], nsegs[j]):
                parent[find(i)] = find(j)
    seg_comp = {}
    comps = []
    comp_of = {}
    todo = []
    done = []
    for q in qs:
        hit = None
        for i, s in enumerate(nsegs):
            if Sheet._on(q["x"], q["y"], s[1], s[2], s[3], s[4], 0.05):
                hit = find(i)
                break
        if hit is not None:
            if hit not in seg_comp:
                seg_comp[hit] = []
                comps.append(seg_comp[hit])
            seg_comp[hit].append(q)
            comp_of[id(q)] = seg_comp[hit]
            done.append(q)
        else:
            todo.append(q)
    if not done and todo:
        first = todo.pop(0)
        c = [first]
        comps.append(c)
        comp_of[id(first)] = c
        done.append(first)
    return comps, comp_of, done, todo

def wire_up(pinpts):
    sh = Sheet()
    for q in pinpts:
        sh.pins.append((q["net"], q["ref"], q["x"], q["y"]))
    for (x, y) in NC_POINTS:
        sh.pins.append(("", "", x, y))
    for p in parts:
        if "body" in p:
            sh.bodies.append((p["ref"],) + p["body"])
    for (x, y, s_, size, blk) in texts:
        sh.texts.append((x - 0.5, y - size * 1.1, x + len(s_) * size * 0.62, y + 0.5))

    # Add explicit backbone rails first so components tap directly onto them with T-junctions
    for net, pts in rails:
        sh.add(net, pts)

    groups = {}
    for q in pinpts:
        groups.setdefault((q["net"], q["block"]), []).append(q)
    nets_blocks = {}
    for (net, blk), qs in groups.items():
        if any(not q["ref"].startswith("#") for q in qs):
            nets_blocks.setdefault(net, set()).add(blk)
    labels, powers, junc, names = [], [], [], []

    clusters = []
    for (net, blk), qs in sorted(groups.items(), key=lambda kv: (kv[0][0] in POWER, -len(kv[1]))):
        if blk < 0:
            comps = [[q] for q in qs]
        else:
            limit = 14.0 if net == "GND" else 26.0 if net in POWER else 260.0
            comps, comp_of, done, todo = _build_net_comps(net, qs, sh.wires)
            while todo:
                best = None
                for b in todo:
                    for a in done:
                        d = abs(a["x"] - b["x"]) + abs(a["y"] - b["y"])
                        if d <= limit and (best is None or d < best[0]):
                            best = (d, a, b, False)
                    if net != "GND":
                        for (n, x1, y1, x2, y2) in sh.wires:
                            if n != net:
                                continue
                            if abs(y1 - y2) < EPS and min(x1, x2) - EPS <= b["x"] <= max(x1, x2) + EPS:
                                d = abs(b["y"] - y1)
                                if d <= limit and (best is None or d < best[0]):
                                    # Find a pin in `done` belonging to this wire component
                                    owner = next((dp for dp in done if Sheet._on(dp["x"], dp["y"], x1, y1, x2, y2, 50.0)), done[0])
                                    p_proj = dict(x=b["x"], y=y1, out=(0, 1 if b["y"] > y1 else -1), ref="", owner=owner)
                                    best = (d, p_proj, b, True)
                            elif abs(x1 - x2) < EPS and min(y1, y2) - EPS <= b["y"] <= max(y1, y2) + EPS:
                                d = abs(b["x"] - x1)
                                if d <= limit and (best is None or d < best[0]):
                                    owner = next((dp for dp in done if Sheet._on(dp["x"], dp["y"], x1, y1, x2, y2, 50.0)), done[0])
                                    p_proj = dict(x=x1, y=b["y"], out=(1 if b["x"] > x1 else -1, 0), ref="", owner=owner)
                                    best = (d, p_proj, b, True)
                if best is None:
                    b = todo.pop(0)
                    done.append(b)
                    comp_of[id(b)] = [b]
                    comps.append(comp_of[id(b)])
                    continue
                _, a, b, is_proj = best
                todo.remove(b)
                done.append(b)
                own = (b["ref"],) if is_proj else (a["ref"], b["ref"])
                path = None
                for cand in _routes((a["x"], a["y"], a["out"]), (b["x"], b["y"], b["out"])):
                    if sh.path_ok(net, cand, own):
                        path = cand
                        break
                if path is None:
                    comp_of[id(b)] = [b]
                    comps.append(comp_of[id(b)])
                    continue
                sh.add(net, path)
                target_comp = comp_of[id(a["owner"])] if is_proj else comp_of[id(a)]
                target_comp.append(b)
                comp_of[id(b)] = target_comp
            # Re-evaluate true connected components from the final wire graph
            comps, _, _, rem = _build_net_comps(net, qs, sh.wires)
            for r_ in rem:
                comps.append([r_])
        clusters.append((net, comps))

    # Place one label / power symbol per wired cluster
    for net, comps in clusters:
        local = len(nets_blocks.get(net, ())) <= 1 and len(comps) == 1
        if local and net not in POWER:
            segs = [w for w in sh.wires if w[0] == net]
            if segs:
                w = max(segs, key=lambda w: abs(w[1] - w[3]) + abs(w[2] - w[4]))
                names.append((net, (w[1] + w[3]) / 2, (w[2] + w[4]) / 2, 0 if abs(w[2] - w[4]) < EPS else 90))
            continue
        for comp in comps:
            real_comp = [q for q in comp if not q["ref"].startswith("#")] or comp
            placed = False
            if net in POWER:
                pref = (0, 1) if net == "GND" else (0, -1)
                order = sorted(real_comp, key=lambda q: (q["out"] != pref, -q["y"] if net == "GND" else q["y"]))
            else:
                order = sorted(real_comp, key=lambda q: q["x"])
            pts_ = {(round(q["x"], 3), round(q["y"], 3)) for q in real_comp}
            segs = [w for w in sh.wires if w[0] == net]
            grew = True
            while grew:
                grew = False
                for (n, x1, y1, x2, y2) in segs:
                    a_, b_ = (round(x1, 3), round(y1, 3)), (round(x2, 3), round(y2, 3))
                    if (a_ in pts_) != (b_ in pts_):
                        pts_ |= {a_, b_}
                        grew = True
            pins_at = {(round(q["x"], 3), round(q["y"], 3)) for q in real_comp}
            order = order + [dict(x=x, y=y, out=d, ref="") for (x, y) in sorted(pts_ - pins_at)
                             for d in ((-1, 0), (1, 0), (0, -1), (0, 1))]
            for q in order:
                dx, dy = q["out"]
                tries = [([(q["x"] + dx * d, q["y"] + dy * d)], (dx, dy)) for d in (2.54, 5.08, 7.62)]
                kinds = [(t, net in POWER) for t in tries]
                if net in POWER and q["out"] != pref:
                    kinds = [(([m, (m[0] + pref[0] * 2.54, m[1] + pref[1] * 2.54)], pref), True)
                             for m in [(q["x"] + dx * d, q["y"] + dy * d) for d in (2.54, 5.08)]] + kinds
                if net in POWER:
                    kinds += [(t, False) for t in tries[:2]]
                for (pts, dirn), as_sym in kinds:
                    path = [(q["x"], q["y"])] + pts
                    ex, ey = pts[-1]
                    if as_sym:
                        ln = 4.5 if net == "GND" else 4.0
                        wd = len(net) * 0.6 + 0.6 if dirn[0] == 0 else 1.6 + len(net) * 0.8
                    else:
                        ln, wd = len(net) * 1.0 + 3.0, 1.0
                    bx = (min(ex, ex + dirn[0] * ln) - (wd if dirn[0] == 0 else 0.1),
                          min(ey, ey + dirn[1] * ln) - (wd if dirn[1] == 0 else 0.1),
                          max(ex, ex + dirn[0] * ln) + (wd if dirn[0] == 0 else 0.1),
                          max(ey, ey + dirn[1] * ln) + (wd if dirn[1] == 0 else 0.1))
                    bx = (bx[0] + (0.4 if dirn == (1, 0) else 0), bx[1] + (0.4 if dirn == (0, 1) else 0),
                          bx[2] - (0.4 if dirn == (-1, 0) else 0), bx[3] - (0.4 if dirn == (0, -1) else 0))
                    if sh.path_ok(net, path, (q["ref"],)) and sh.box_free(net, bx):
                        sh.texts.append(bx)
                        sh.add(net, path)
                        (powers if as_sym else labels).append((net, pts[-1][0], pts[-1][1], dirn))
                        placed = True
                        break
                if placed:
                    break
            if not placed:
                q = order[0]
                labels.append((net, q["x"], q["y"], q["out"]))

    ends = {}
    for (n, x1, y1, x2, y2) in sh.wires:
        for pnt in ((x1, y1), (x2, y2)):
            k = (round(pnt[0], 3), round(pnt[1], 3))
            ends[k] = ends.get(k, 0) + 1
    for k, c in ends.items():
        mid = any(Sheet._on(k[0], k[1], x1, y1, x2, y2, 0.01) and (round(x1, 3), round(y1, 3)) != k
                  and (round(x2, 3), round(y2, 3)) != k for (n, x1, y1, x2, y2) in sh.wires)
        pinhere = any(abs(px - k[0]) < 0.01 and abs(py - k[1]) < 0.01 for (_, _, px, py) in sh.pins)
        if c >= 3 or mid or (pinhere and c >= 2):
            junc.append(k)
    for (_, _, px, py) in sh.pins:
        k = (round(px, 3), round(py, 3))
        if k not in junc and any(Sheet._on(px, py, x1, y1, x2, y2, 0.01) and (round(x1, 3), round(y1, 3)) != k
                                 and (round(x2, 3), round(y2, 3)) != k for (n, x1, y1, x2, y2) in sh.wires):
            junc.append(k)
    return sh.wires, junc, labels, powers, names

def emit():
    used = []
    for p in parts:
        if p["lib"] not in used:
            used.append(p["lib"])
    install_custom()
    head = ['(kicad_sch (version 20230121) (generator eeschema)', '  (uuid %s)' % ROOT_UUID, '  (paper "A3")',
            '  (title_block (title "%s") (date "2026-10-31") (rev "%s") (company "n3tk0/MeshCore"))' % (TITLE, REV),
            '  (lib_symbols']
    for lid in used:
        head.append(symbol_def(lid))
    out = []
    pinpts = []
    ncs = []
    for p in parts:
        pins = [dict(q) for q in symbol_pins(p["lib"])]
        rot, mir = p.get("rot", 0), p.get("mirror", False)
        for q in pins:
            if mir:
                q["x"], q["ang"] = -q["x"], (180 - q["ang"]) % 360
            if rot:
                c, s_ = {90: (0, 1), 180: (-1, 0), 270: (0, -1)}[rot]
                q["x"], q["y"] = q["x"] * c - q["y"] * s_, q["x"] * s_ + q["y"] * c
                q["ang"] = (q["ang"] + rot) % 360
        u = uid()
        ref = p["ref"]
        hidden_ref = ref.startswith("#")
        out.append('  (symbol (lib_id "%s") (at %s %s %d)%s (unit 1) (in_bom %s) (on_board %s) (dnp %s) (uuid %s)' % (
            p["lib"].replace("custom:", "xiao_solar:"), fmt(p["x"]), fmt(p["y"]), rot, " (mirror y)" if mir else "",
            "no" if hidden_ref else "yes", "no" if hidden_ref else "yes", "yes" if p["dnp"] else "no", u))
        xs_ = [q["x"] for q in pins] or [0]
        ys_ = [q["y"] for q in pins] or [0]
        horiz = len(pins) == 2 and abs(ys_[0] - ys_[1]) < EPS
        vj = None
        if len(pins) <= 4 and min(xs_) > 1:
            cy = p["y"] - sum(ys_) / len(ys_)
            rx, ry, vx, vy, j = p["x"] - 2.0, cy - 0.6, p["x"] - 2.0, cy + 1.6, " (justify right)"
        elif len(pins) <= 4 and max(xs_) < -1:
            cy = p["y"] - sum(ys_) / len(ys_)
            rx, ry, vx, vy, j = p["x"] + 2.0, cy - 0.6, p["x"] + 2.0, cy + 1.6, " (justify left)"
        elif len(pins) <= 3 and not horiz:
            rx, ry, vx, vy, j = p["x"] + max(xs_) + 1.6, p["y"] - 0.9, p["x"] + max(xs_) + 1.6, p["y"] + 1.4, " (justify left)"
        elif horiz:
            rx, ry, vx, vy, j = p["x"], p["y"] - 2.5, p["x"], p["y"] + 2.8, ""
        else:
            top = max(ys_ + [2.54])
            has_top_pins = any(q["ang"] == 270 for q in pins)
            if has_top_pins:
                rx, ry, vx, vy, j = p["x"], p["y"] - 1.2, p["x"], p["y"] + 1.4, ""
            else:
                rx, ry, vx, vy, j = p["x"], p["y"] - top - 4.2, p["x"], p["y"] - top - 2.0, ""
        fa = 90 if rot in (90, 270) else 0
        if mir or rot == 180:
            j = j.replace("right", "LEFT").replace("left", "right").replace("LEFT", "left")
        if vj is None:
            vj = j
        elif mir or rot == 180:
            vj = vj.replace("right", "LEFT").replace("left", "right").replace("LEFT", "left")
        out.append('    (property "Reference" "%s" (at %s %s %d) (effects (font (size 1.15 1.15))%s%s))' % (
            ref, fmt(rx), fmt(ry), fa, j, " hide" if hidden_ref else ""))
        out.append('    (property "Value" "%s" (at %s %s %d) (effects (font (size 1.05 1.05))%s%s))' % (
            p["value"], fmt(vx), fmt(vy), fa, vj, " hide" if hidden_ref else ""))
        out.append('    (property "Footprint" "%s" (at %s %s 0) (effects (font (size 1.27 1.27)) hide))' % (p["fp"], fmt(p["x"]), fmt(p["y"])))
        out.append('    (property "Datasheet" "" (at %s %s 0) (effects (font (size 1.27 1.27)) hide))' % (fmt(p["x"]), fmt(p["y"])))
        out.append('    (property "LCSC" "%s" (at %s %s 0) (effects (font (size 1.27 1.27)) hide))' % (p["lcsc"], fmt(p["x"]), fmt(p["y"])))
        seen = set()
        for pin in pins:
            if pin["num"] in seen:
                continue
            seen.add(pin["num"])
            out.append('    (pin "%s" (uuid %s))' % (pin["num"], uid()))
        out.append('    (instances (project "%s" (path "/%s" (reference "%s") (unit 1))))' % (PROJECT, ROOT_UUID, ref))
        out.append('  )')
        inner = [(p["x"] + q["x"] - OUTWARD[q["ang"]][0] * q["length"], p["y"] - q["y"] - OUTWARD[q["ang"]][1] * q["length"])
                 for q in pins] or [(p["x"], p["y"])]
        xs, ys = [c[0] for c in inner], [c[1] for c in inner]
        if not hidden_ref:
            p["body"] = (min(xs) - 0.8, min(ys) - 0.8, max(xs) + 0.8, max(ys) + 0.8)
        for pin in pins:
            if pin["num"] not in p["nets"]:
                if p["nets"] or not ref.startswith("H"):
                    if p["lib"].startswith("power:"):
                        continue
                    raise KeyError("%s pin %s has no net" % (ref, pin["num"]))
                continue
            net = p["nets"][pin["num"]]
            ax = p["x"] + pin["x"]
            ay = p["y"] - pin["y"]
            if net is None:
                ncs.append((ax, ay))
                continue
            if any(abs(q["x"] - ax) < EPS and abs(q["y"] - ay) < EPS and q["ref"] == ref for q in pinpts):
                continue
            pinpts.append(dict(net=net, ref=ref, x=ax, y=ay, out=OUTWARD[pin["ang"]], block=p["block"]))
    NC_POINTS[:] = ncs
    wires, junc, labels, powers, names = wire_up(pinpts)
    for (net, x, y, a) in names:
        out.append('  (label "%s" (at %s %s %d) (fields_autoplaced) (effects (font (size 1.15 1.15) bold) (justify left bottom)) (uuid %s))' % (
            net, fmt(x), fmt(y), a, uid()))
    for (n, x1, y1, x2, y2) in wires:
        out.append('  (wire (pts (xy %s %s) (xy %s %s)) (stroke (width 0) (type default)) (uuid %s))' % (
            fmt(x1), fmt(y1), fmt(x2), fmt(y2), uid()))
    for (x, y) in junc:
        out.append('  (junction (at %s %s) (diameter 0) (color 0 0 0 0) (uuid %s))' % (fmt(x), fmt(y), uid()))
    for (net, x, y, d) in labels:
        a = {(1, 0): 0, (-1, 0): 180, (0, -1): 90, (0, 1): 270}[d]
        just = "left" if a in (0, 90) else "right"
        out.append('  (global_label "%s" (shape bidirectional) (at %s %s %d) (fields_autoplaced) (effects (font (size 1.27 1.27)) (justify %s)) (uuid %s)\n'
                   '    (property "Intersheetrefs" "${INTERSHEET_REFS}" (at %s %s 0) (effects (font (size 1.27 1.27)) hide)))' % (
            net, fmt(x), fmt(y), a, just, uid(), fmt(x), fmt(y)))
    used_pwr = []
    for i, (net, x, y, d) in enumerate(powers):
        name, body = power_symbol(net)
        if body not in used_pwr:
            used_pwr.append(body)
        if net == "GND":
            rot = {(0, 1): 0, (0, -1): 180, (1, 0): 90, (-1, 0): 270}[d]
        else:
            rot = {(0, -1): 0, (0, 1): 180, (-1, 0): 90, (1, 0): 270}[d]
        ref = "#PWR%03d" % (i + 1)
        off = 4.3 if net == "GND" else 2.9
        vx, vy = x + d[0] * (off + len(net) * 0.45), y + d[1] * off
        out.append('  (symbol (lib_id "xiao_solar:%s") (at %s %s %d) (unit 1) (in_bom yes) (on_board yes) (dnp no) (uuid %s)\n'
                   '    (property "Reference" "%s" (at %s %s 0) (effects (font (size 1.27 1.27)) hide))\n'
                   '    (property "Value" "%s" (at %s %s 0) (effects (font (size 1.15 1.15))))\n'
                   '    (property "Footprint" "" (at %s %s 0) (effects (font (size 1.27 1.27)) hide))\n'
                   '    (property "Datasheet" "" (at %s %s 0) (effects (font (size 1.27 1.27)) hide))\n'
                   '    (pin "1" (uuid %s))\n'
                   '    (instances (project "%s" (path "/%s" (reference "%s") (unit 1))))\n  )' % (
            name, fmt(x), fmt(y), rot, uid(), ref, fmt(x), fmt(y), net, fmt(vx), fmt(vy), fmt(x), fmt(y), fmt(x), fmt(y),
            uid(), PROJECT, ROOT_UUID, ref))
    head += used_pwr
    head.append('  )')
    for (x, y) in ncs:
        out.append('  (no_connect (at %s %s) (uuid %s))' % (fmt(x), fmt(y), uid()))
    for (x, y, s_, size, blk) in texts:
        out.append('  (text "%s" (at %s %s 0) (effects (font (size %s %s)) (justify left bottom)) (uuid %s))' % (
            s_.replace('"', "'"), fmt(x), fmt(y), fmt(size), fmt(size), uid()))
    out.append('  (sheet_instances (path "/" (page "1")))')
    out.append(')')
    return "\n".join(head + out) + "\n"

def write_bom():
    """JLC BOM (Comment, Designator, Footprint, LCSC). Hand-soldered parts are listed apart."""
    import csv
    groups, hand = {}, []
    for p in parts:
        r = p["ref"]
        if r.startswith(("#", "H", "TP", "JP")):
            continue
        if not p["lcsc"]:
            hand.append(p)
            continue
        k = next((g for g in groups if g[2] == p["lcsc"]), (p["value"], p["fp"].split(":")[-1], p["lcsc"]))
        groups.setdefault(k, []).append(r)
    os.makedirs(os.path.join(ROOT, "production"), exist_ok=True)
    with open(os.path.join(ROOT, "production", "bom-jlc.csv"), "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["Comment", "Designator", "Footprint", "LCSC"])
        for (v, fp, l), refs in sorted(groups.items(), key=lambda kv: kv[1][0]):
            w.writerow([v, ",".join(sorted(refs, key=lambda s: (s[0], int(re.sub(r"\D", "", s) or 0)))), fp, l])
    with open(os.path.join(ROOT, "production", "bom-hand.csv"), "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["Designator", "Value", "Footprint"])
        for p in hand:
            w.writerow([p["ref"], p["value"], p["fp"].split(":")[-1]])
    return groups, hand

if __name__ == "__main__":
    path = os.path.join(ROOT, PROJECT + ".kicad_sch")
    open(path, "w", encoding="utf-8").write(emit())
    g, h = write_bom()
    print("wrote", path, len(parts), "parts;", len(g), "JLC lines,", len(h), "hand parts")
