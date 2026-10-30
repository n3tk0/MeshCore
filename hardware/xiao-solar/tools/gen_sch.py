#!/usr/bin/env python3
"""Generates xiao-solar.kicad_sch (KiCad 7) from the part list below.

Connections are made with net labels placed on every pin end, so the
netlist is defined here in one place and the drawing stays readable.
Run: python3 tools/gen_sch.py  (from hardware/xiao-solar)
"""
import os, re, uuid

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SYSLIB = "/usr/share/kicad/symbols"
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
        _libcache[lib] = open(path).read()
    return _libcache[lib]

def find_symbol(lib, name):
    t = lib_text(lib)
    m = re.search(r'\(symbol "%s"' % re.escape(name), t)
    if not m:
        raise KeyError(lib + ":" + name)
    return sexpr_block(t, m.start())

CUSTOM = {}

def custom_box(name, ref, left, right, top=(), bottom=(), width=15.24, fp=""):
    """left/right/top/bottom: lists of (number, name, type)."""
    rows = max(len(left), len(right))
    h = max(rows, 1) * 2.54 + 2.54
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
        out.append('    (pin %s line (at %.2f %.2f %d) (length 2.54) (name "%s" (effects (font (size 1.27 1.27)))) (number "%s" (effects (font (size 1.27 1.27)))))' % (ptype, x, y, ang, pname, num))
    for i, (n, pn, t) in enumerate(left):
        pin(n, pn, t, -w / 2 - 2.54, top_y - 2.54 * (i + 1), 0)
    for i, (n, pn, t) in enumerate(right):
        pin(n, pn, t, w / 2 + 2.54, top_y - 2.54 * (i + 1), 180)
    for i, (n, pn, t) in enumerate(top):
        pin(n, pn, t, -w / 2 + 2.54 * (i + 1), top_y + 2.54, 270)
    for i, (n, pn, t) in enumerate(bottom):
        pin(n, pn, t, -w / 2 + 2.54 * (i + 1), -top_y - 2.54, 90)
    out.append('  )')
    out.append(')')
    CUSTOM[name] = "\n".join(out)

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
custom_box("LM66100", "U", [("1", "VIN", "power_in"), ("3", "~{CE}", "input"), ("4", "NC", "passive")],
           [("6", "VOUT", "power_out"), ("5", "~{ST}", "open_collector"), ("2", "GND", "power_in")], width=12.7,
           fp="Package_TO_SOT_SMD:SOT-363_SC-70-6")
custom_box("Wio-SX1262_for_XIAO", "U", WIO_L, WIO_R, width=22.86,
           fp="xiao_solar:XIAO_14P_2.54_21x17.8mm")

def install_custom():
    """Keeps the hand-made XIAO/Wio symbols in lib/xiao_solar.kicad_sym too."""
    t = open(LOCAL).read()
    for name, body in CUSTOM.items():
        m = re.search(r'\n  \(symbol "%s"' % re.escape(name), t)
        if m:
            old = sexpr_block(t, m.start() + 3)
            t = t.replace(old, body)
        else:
            t = t.rstrip()[:-1].rstrip() + "\n  " + body.replace("\n", "\n  ") + "\n)\n"
    open(LOCAL, "w").write(t)
    _libcache.pop("xiao_solar", None)

def symbol_def(lib_id):
    lib, name = lib_id.split(":")
    if lib == "custom":
        body = CUSTOM[name]
        lib_id = "xiao_solar:" + name
    else:
        body = find_symbol(lib, name)
        m = re.match(r'\(symbol "[^"]+" \(extends "([^"]+)"\)', body)
        if m:
            raise ValueError("extends not supported: " + lib_id)
    return body.replace('(symbol "%s"' % name, '(symbol "%s"' % lib_id, 1)

def symbol_pins(lib_id):
    body = symbol_def(lib_id)
    pins = []
    for m in re.finditer(r'\(pin (\w+) \w+\s*\(at ([-\d.]+) ([-\d.]+) (\d+)\)\s*\(length ([-\d.]+)\)(.*?)\(number "([^"]*)"', body, re.S):
        hide = ' hide' in m.group(6)[:40]
        pins.append(dict(type=m.group(1), x=float(m.group(2)), y=float(m.group(3)),
                         ang=int(m.group(4)), num=m.group(7), length=float(m.group(5))))
    return pins

# ---------------------------------------------------------------- design

parts = []      # dict(ref, lib, value, x, y, nets, fp, lcsc, dnp, note)
texts = []
blocks = []     # titles; parts and texts carry the index of the block they were made in
OFF = [0.0, 0.0]  # moves the parts and notes that follow (a whole block at once)

def BLOCK(x, y, title):
    """Starts a block: a framed group of parts wired to each other. Nets that leave the
    block get a label (or a power symbol), nets inside it are drawn as wires."""
    blocks.append(title)
    T(x, y, title, 2.0)

def P(ref, lib, value, x, y, nets, fp="", lcsc="", dnp=False, mpn="", rot=0, mirror=False):
    """rot: 0/90/180/270 (counter-clockwise), mirror: flipped left-right (not with rot)."""
    assert not (rot and mirror)
    x, y = x + OFF[0], y + OFF[1]
    parts.append(dict(ref=ref, lib=lib, value=value, x=x, y=y, nets=nets, fp=fp, lcsc=lcsc, dnp=dnp, mpn=mpn,
                      block=len(blocks) - 1, rot=rot, mirror=mirror))

# Bring-up test points (1 mm top-side pads), one per net, in a row on the sheet.
TP_FP = "TestPoint:TestPoint_Pad_D1.0mm"
def test_points(nets, x, y, first=3, step=14):
    for i, net in enumerate(nets):
        P("TP%d" % (first + i), "Connector:TestPoint", net, x + step * i, y, {"1": net}, TP_FP)

def T(x, y, s, size=2.0):
    texts.append((x + OFF[0], y + OFF[1], s, size, len(blocks) - 1))

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

C10U = "C15850"     # 10uF 25V X5R 0805
C10U50 = "C13585"   # 10uF 50V X5R 1206 (VBUS/PMID: derating at 25 V)
C1206 = "Capacitor_SMD:C_1206_3216Metric"
C100N = "C14663"    # 100nF 50V X7R 0603
C47N = "C1622"      # 47nF 50V 0603
C4U7 = "C1779"      # 4.7uF 25V 0805
C0402 = "Capacitor_SMD:C_0402_1005Metric"
C100N0402 = "C1525"      # 100nF 16V X7R 0402 (SYS)
C100N0402HV = "C131394"  # 100nF 50V X7R 0402 (PMID: up to 25 V)

# ---- 1. Inputs -------------------------------------------------------------
BLOCK(15, 15, "1. ВХОДОВЕ: соларен панел 5-22 V (Voc до 25 V на студ) и втори вход 5-20 V")
P("J1", "Connector_Generic:Conn_01x02", "SOLAR", 25, 35, {"1": "SOLAR_IN", "2": "GND"}, CONN2, mirror=True)
P("J2", "Connector_Generic:Conn_01x02", "VIN2 5-20V", 25, 55, {"1": "VIN2_IN", "2": "GND"}, CONN2, mirror=True)
P("D1", "Device:D_Schottky", "SS54", 45, 35, {"2": "SOLAR_IN", "1": "VBUS"}, "Diode_SMD:D_SMA", "C22452", mirror=True)
P("D2", "Device:D_Schottky", "SS54", 45, 55, {"2": "VIN2_IN", "1": "VBUS"}, "Diode_SMD:D_SMA", "C22452", mirror=True)
P("D3", "xiao_solar:SMBJ24A-C87268", "SMBJ24A", 62, 49.08, {"1": "VBUS", "2": "GND"}, "xiao_solar:SMB_L4.6-W3.6-LS5.3-RD", "C19077578", rot=270)
C("C1", "10u/50V", 75, 48, "VBUS", "GND", C10U50, C1206)
C("C2", "10u/50V", 87, 48, "VBUS", "GND", C10U50, C1206)
C("C3", "100n", 99, 48, "VBUS", "GND", C100N, C0603)
T(15, 70, "SS54 = OR на двата входа + защита от обратна полярност. VAC1=VAC2=VBUS, ACDRV към GND (без ACFET).", 1.27)

# ---- 2. Charger --------------------------------------------------------------
BLOCK(15, 80, "2. MPPT ЗАРЯДНО BQ25798 (1S, 750 kHz, 2.2 uH), I2C 0x6B")
OFF[1] = -12.0   # blocks 2 and 3 drawn 12 mm higher than their coordinates
bq = {"1": "STAT", "2": "VBUS", "3": "VBUS", "4": "BTST1", "5": "REGN", "6": None, "7": None,
      "8": "VBUS", "9": "VBUS", "10": "GND", "11": "GND", "12": "QON", "13": "CE", "14": "SCL",
      "15": "SDA", "16": "TS", "17": "REGN", "18": "BATP", "19": "BTST2", "20": "PROG",
      "21": None, "22": "BAT", "23": "BAT", "24": "SDRV", "25": "SYS", "26": "SW2", "27": "GND",
      "28": "SW1", "29": "PMID"}
P("U1", "xiao_solar:BQ25798RQMR", "BQ25798RQMR", 80, 140, bq,
  "xiao_solar:QFN-29_L4.0-W4.0-P0.40-TL-BQ25792RQMR", "C2876593")
# switch node: bootstrap caps from BTST to SW, the inductor between SW1 and SW2
C("C4", "47n", 80, 112, "BTST1", "SW1", C47N, C0603, rot=90)
P("L1", "xiao_solar:FNR4030S2R2MT", "2.2uH 4A molded (MWSA0402S)", 112, 112, {"1": "SW1", "2": "SW2"},
  "xiao_solar:IND-SMD_L4.4-W4.2_MWSA0402S", "C408335")
C("C5", "47n", 112, 147.62, "BTST2", "SW2", C47N, C0603, rot=90)
R("R1", "4.7k 1%", 104, 165, "PROG", "GND", "C23162")
# cap banks along the bottom: PMID, REGN, SYS, BAT
for i, (ref, val, net, lcsc, fp) in enumerate([
        ("C6", "10u/50V", "PMID", C10U50, C1206), ("C7", "10u/50V", "PMID", C10U50, C1206),
        ("C8", "10u/50V", "PMID", C10U50, C1206),
        # 100 nF right at the PMID/SYS and GND (27) pins, 0402 as in the TI layout guide
        ("C9", "100n/50V", "PMID", C100N0402HV, C0402)]):
    C(ref, val, 30 + 11 * i, 185, net, "GND", lcsc, fp)
C("C10", "4.7u/25V", 78, 185, "REGN", "GND", C4U7)
for i, (ref, val, lcsc, fp) in enumerate([("C11", "10u/25V", C10U, C0805), ("C12", "10u/25V", C10U, C0805),
                                          ("C13", "10u/25V", C10U, C0805), ("C14", "10u/25V", C10U, C0805),
                                          ("C15", "10u/25V", C10U, C0805), ("C16", "100n", C100N0402, C0402)]):
    C(ref, val, 92 + 11 * i, 185, "SYS", "GND", lcsc, fp)
C("C17", "10u/25V", 162, 185, "BAT", "GND", C10U)
C("C18", "10u/25V", 173, 185, "BAT", "GND", C10U)
# TS: 103AT NTC via pads, RT1/RT2 per TI (0..60 C window); firmware sets TS_IGNORE by default
R("R2", "5.1k 1%", 135, 150, "REGN", "TS", "C23186")
R("R3", "30k 1%", 135, 162, "TS", "GND", "C22984")
P("J3", "Connector_Generic:Conn_01x02", "NTC 10k (103AT)", 152, 156, {"1": "TS", "2": "GND"}, CONN2)
# CE (active low) on D0. JP2 picks what R4 does while D0 is not driven (Xiao off/resetting):
# 1-2 (default, Li-ion) pull-down = charges without the Xiao, on BQ defaults 4.2 V / 1 A after a POR;
# 2-3 (LiFePO4) pull-up to 3V3 = charges only after firmware wrote VREG 3.6 V and drives D0 low.
R("R4", "100k", 45, 155.24, "CE_PULL", "CE", "C25803", rot=90)
P("JP2", "Jumper:SolderJumper_3_Bridged12", "CE: 1-2 Li-ion / 2-3 LiFePO4", 41.19, 168,
  {"1": "GND", "2": "CE_PULL", "3": "3V3"}, "Jumper:SolderJumper-3_P1.3mm_Bridged12_RoundedPad1.0x1.5mm", rot=180)
# STAT LED (only lit while charging, firmware can disable with DIS_STAT)
R("R6", "2.2k", 40, 124.76, "REGN", "LED_STAT", "C4190", rot=90)
P("D4", "Device:LED", "green", 52, 124.76, {"1": "STAT", "2": "LED_STAT"}, "LED_SMD:LED_0805_2012Metric", "C2297", mirror=True)
# INT (pin 21) is left open: the firmware polls the status registers, and the pin has no
# room to escape between the BAT caps and PROG.
P("TP2", "Connector:TestPoint", "QON", 40, 147, {"1": "QON"}, "TestPoint:TestPoint_Pad_D1.0mm")
T(18, 199, "CE: JP2 1-2 (Li-ion) зарежда и без Xiao; 2-3 (LiFePO4) само след запис на VREG и D0=0.", 1.27)
T(18, 202.5, "Watchdog off. D+/D- не се ползват. TP2 (QON) към GND за 1 s = събуждане от ship mode.", 1.27)

# ---- 3. Battery / BMS --------------------------------------------------------
BLOCK(15, 211, "3. БАТЕРИЯ 1S (Li-ion по подразбиране, LiFePO4 с настройка): ship FET + DW01A/HJ8205")
OFF[1] = -8.0    # block 3 parts a little lower: room under the title
P("J4", "Connector_Generic:Conn_01x02", "BATTERY", 35, 225, {"1": "BATT+", "2": "BATT-"}, CONN2, mirror=True)
# INA219 high side on the battery lead: RS1 sees charge and discharge current, bus voltage
# is read at IN- (BATS). 50 mOhm alloy, 1 W: 3 A gives 150 mV (PGA /4 or /8), 0.45 W.
P("RS1", "Device:R", "0.05 1W", 58, 225, {"1": "BATT+", "2": "BATS"}, "Resistor_SMD:R_1206_3216Metric", "C154645", rot=90)
P("U7", "Sensor_Energy:INA219AxDCN", "INA219AIDCNR", 40, 252,
  {"1": "BATT+", "2": "BATS", "3": "GND", "4": "3V3", "5": "SCL", "6": "SDA", "7": "GND", "8": "GND"},
  "Package_TO_SOT_SMD:SOT-23-8", "C87469", mirror=True)
C("C24", "100n", 52, 268, "3V3", "GND", C100N, C0603)
P("Q2", "Transistor_FET:BSS138", "AO3400A", 82, 227.54, {"1": "SDRV", "2": "BAT", "3": "BATS"}, SOT23, "C20917", rot=90)
# JP3 closed = no ship FET: BQ25798 then runs down to 2.4 V instead of 3.2 V (needed for
# LiFePO4); leave SFET_PRESENT=0 in firmware. Q2 may stay fitted or be left off.
P("JP3", "Jumper:SolderJumper_2_Open", "no ship FET (LiFePO4)", 82, 213, {"1": "BAT", "2": "BATS"},
  "Jumper:SolderJumper-2_P1.3mm_Open_RoundedPad1.0x1.5mm", mirror=True)
# BATP senses at Q2 (BATS): RS1 adds at most 150 mV at 3 A, ~5 mV at the termination current
R("R8", "100", 70, 240, "BATS", "BATP", "C22775")
R("R9", "100", 92, 255, "BATT+", "DW_VCC", "C22775")
C("C19", "100n", 102, 272, "DW_VCC", "BATT-", C100N, C0603, rot=90)
P("U2", "xiao_solar:DW01A_C351410", "DW01A", 130, 268,
  {"1": "DW_OD", "2": "DW_CS", "3": "DW_OC", "4": None, "5": "DW_VCC", "6": "BATT-"},
  "xiao_solar:SOT-23-6_L2.9-W1.6-P0.95-LS2.8-BL", "C351410", mirror=True)
R("R10", "1k", 152, 274, "DW_CS", "GND", "C21190")
P("Q3", "xiao_solar:FS8205A_C2830320", "HJ8205", 130, 242,
  {"1": "BATT-", "2": "FS_D", "3": "GND", "4": "DW_OC", "5": "FS_D", "6": "DW_OD"},
  "xiao_solar:SOT-23-6_L2.9-W1.6-P0.95-LS2.8-BL", "C20069150")
T(100, 220, "Q2: D към батерията, S към BAT; в shutdown (SDRV_CTRL=01, 0.5 uA)", 1.27)
T(100, 223.5, "диодът на Q2 спира разреждането; събужда се, когато панелът даде напрежение.", 1.27)
T(100, 227, "DW01A: 2.4 V / 4.3 V резерв, HJ8205 в минуса.", 1.27)

# ---- 4. XIAO + Wio ----------------------------------------------------------------
OFF[1] = 0.0
BLOCK(192, 15, "4. XIAO nRF52840 + Wio-SX1262 (пинове като variants/xiao_nrf52)")
# the Wio sits on the Xiao pin for pin: drawn mirrored on its left, D1..D5 wired straight across
P("U4", "custom:Wio-SX1262_for_XIAO", "Wio-SX1262 for XIAO", 222, 45,
  {"1": None, "2": "LORA_DIO1", "3": "LORA_RST", "4": "LORA_BUSY", "5": "LORA_NSS",
   "6": "LORA_RXEN", "7": None, "14": None, "13": "GND", "12": "3V3",
   "11": "SPI_MOSI", "10": "SPI_MISO", "9": "SPI_SCK", "8": None}, "xiao_solar:XIAO_14P_2.54_21x17.8mm", mirror=True)
P("U3", "custom:XIAO_nRF52840", "XIAO nRF52840", 275, 45,
  {"1": "CE", "2": "LORA_DIO1", "3": "LORA_RST", "4": "LORA_BUSY", "5": "LORA_NSS",
   "6": "LORA_RXEN", "7": "SCL", "14": "XIAO_5V", "13": "GND", "12": "3V3",
   "11": "SPI_MOSI", "10": "SPI_MISO", "9": "SPI_SCK", "8": "SDA"}, "xiao_solar:XIAO_14P_2.54_21x17.8mm")
# ideal diode: SYS to the Xiao 5V pin with ~30 mV drop; CE tied to VOUT blocks the Xiao USB
# from feeding SYS back
P("D5", "custom:LM66100", "LM66100", 312, 80, {"1": "SYS", "2": "GND", "3": "XIAO_5V", "4": "GND", "5": "GND", "6": "XIAO_5V"},
  "Package_TO_SOT_SMD:SOT-363_SC-70-6", "C2869734")
C("C20", "10u/25V", 332, 82, "XIAO_5V", "GND", C10U)
C("C21", "10u/25V", 312, 45, "3V3", "GND", C10U)
C("C22", "100n", 323, 45, "3V3", "GND", C100N, C0603)
T(195, 97, "D5 (идеален диод LM66100): SYS към пина 5V на Xiao (USB на Xiao не захранва SYS).", 1.27)
T(195, 100.5, "3V3 идва от регулатора на Xiao.", 1.27)

# ---- 5. I2C ---------------------------------------------------------------------------
BLOCK(192, 112, "5. I2C (SCL=D6, SDA=D7): BQ25798 0x6B, SHT40 0x44, INA219 0x40")
R("R11", "4.7k", 222, 130, "3V3", "SCL", "C23162")
R("R12", "4.7k", 232, 130, "3V3", "SDA", "C23162")
P("J5", "Connector_Generic:Conn_01x04", "I2C rail", 205, 150, {"1": "SCL", "2": "SDA", "3": "3V3", "4": "GND"}, CONN4, mirror=True)
P("U5", "xiao_solar:SHT40-AD1B-R3", "SHT40-AD1B", 268, 145,
  {"1": "SDA", "2": "SCL", "3": "3V3", "4": "GND", "5": "GND"},
  "xiao_solar:DFN-4_L1.5-W1.5-P0.8-TL-EP", "C2848306")
C("C23", "100n", 290, 150, "3V3", "GND", C100N, C0603)

# ---- test points / power flags / mechanics ------------------------------------------
BLOCK(192, 183, "6. Тест точки за първото пускане (падове 1 mm отгоре)")
test_points(["VBUS", "PMID", "SYS", "BAT", "GND", "3V3", "SCL", "SDA", "BATS"], 200, 200, step=13)

BLOCK(352, 15, "PWR_FLAG и отвори")
for i, net in enumerate(["GND", "VBUS", "SYS", "BATT-", "BATS", "BAT", "BATT+", "REGN", "PMID", "XIAO_5V"]):
    P("#FLG%02d" % (i + 1), "power:PWR_FLAG", "PWR_FLAG", 360 + (i % 3) * 17, 32 + (i // 3) * 18, {"1": net})
for i in range(4):
    P("H%d" % (i + 1), "Mechanical:MountingHole", "M2/M2.5", 362 + (i % 2) * 20, 92 + (i // 2) * 12, {},
      "xiao_solar:MountingHole_2.8mm_Keepout6mm")

# ---------------------------------------------------------------- writer

def fmt(v):
    s = "%.4f" % v
    s = s.rstrip("0").rstrip(".")
    return "0" if s == "-0" else s

# Nets drawn with power symbols; every other net is wired inside its block and labelled
# where it leaves the block.
POWER = ["GND", "3V3", "VBUS", "SYS", "BAT", "PMID", "REGN", "XIAO_5V", "VOUT"]
OUTWARD = {0: (-1, 0), 180: (1, 0), 90: (0, 1), 270: (0, -1)}   # pin angle -> away from the body (sheet y down)
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
            # crossing at right angles is fine; touching or running along is not
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
        """Room for a label or power symbol: no wire (own net too: a label on its wire reads badly), no pin, no body, no text."""
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
    """Candidate orthogonal paths from pin a to pin b: the two L shapes, then shapes that
    first step out of each pin."""
    (ax, ay, (adx, ady)), (bx, by, (bdx, bdy)) = a, b
    yield [(ax, ay), (bx, ay), (bx, by)]
    yield [(ax, ay), (ax, by), (bx, by)]
    for d in (5.08, 7.62, 2.54, 10.16):
        sa = (ax + adx * d, ay + ady * d)
        sb = (bx + bdx * d, by + bdy * d)
        yield [(ax, ay), sa, (sa[0], by), (bx, by)]
        yield [(ax, ay), sa, (bx, sa[1]), (bx, by)]
        yield [(ax, ay), sa, (sa[0], sb[1]), sb, (bx, by)]
        yield [(ax, ay), sa, (sb[0], sa[1]), sb, (bx, by)]
    # around a body: out of both pins, then along a lane above/below or left/right of both
    for d in (5.08, 7.62, 2.54, 10.16):
        sa = (ax + adx * d, ay + ady * d)
        sb = (bx + bdx * d, by + bdy * d)
        for k in range(1, 16):
            for lane in (max(sa[1], sb[1]) + 2.54 * k, min(sa[1], sb[1]) - 2.54 * k):
                yield [(ax, ay), sa, (sa[0], lane), (sb[0], lane), sb, (bx, by)]
            for lane in (max(sa[0], sb[0]) + 2.54 * k, min(sa[0], sb[0]) - 2.54 * k):
                yield [(ax, ay), sa, (lane, sa[1]), (lane, sb[1]), sb, (bx, by)]

def wire_up(pinpts):
    """pinpts: dicts(net, ref, x, y, out, block). Returns wires, junctions, labels, power
    symbol placements and the set of power nets used."""
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
    groups = {}
    for q in pinpts:
        groups.setdefault((q["net"], q["block"]), []).append(q)
    nets_blocks = {}
    for (net, blk), qs in groups.items():
        # power flags sit in a block of their own and do not make a net leave its block
        if any(not q["ref"].startswith("#") for q in qs):
            nets_blocks.setdefault(net, set()).add(blk)
    labels, powers, junc, names = [], [], [], []
    # pins of one net sit in clusters; wire the cluster with a minimum spanning tree,
    # all nets first so the wiring has the room before the stubs and labels take theirs
    clusters = []
    for (net, blk), qs in sorted(groups.items(), key=lambda kv: (kv[0][0] in POWER, -len(kv[1]))):
        if blk < 0:
            comps = [[q] for q in qs]
        else:
            done = [qs[0]]
            todo = qs[1:]
            comps = []
            comp_of = {id(qs[0]): [qs[0]]}
            comps.append(comp_of[id(qs[0])])
            limit = 15.0 if net == "GND" else 30.0 if net in POWER else 150.0
            while todo:
                best = None
                for b in todo:
                    for a in done:
                        d = abs(a["x"] - b["x"]) + abs(a["y"] - b["y"])
                        if d <= limit and (best is None or d < best[0]):
                            best = (d, a, b)
                if best is None:
                    b = todo.pop(0)
                    done.append(b)
                    comp_of[id(b)] = [b]
                    comps.append(comp_of[id(b)])
                    continue
                _, a, b = best
                todo.remove(b)
                done.append(b)
                own = (a["ref"], b["ref"])
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
                comp_of[id(a)].append(b)
                comp_of[id(b)] = comp_of[id(a)]
        clusters.append((net, comps))
    # then one label / power symbol per wired cluster, on a short stub out of a pin
    for net, comps in clusters:
        local = len(nets_blocks.get(net, ())) <= 1 and len(comps) == 1 and not comps[0][0]["ref"].startswith("#")
        if local and net not in POWER:
            # name it on its longest wire, so the netlist and the reader see the net name
            segs = [w for w in sh.wires if w[0] == net]
            if segs:
                w = max(segs, key=lambda w: abs(w[1] - w[3]) + abs(w[2] - w[4]))
                names.append((net, (w[1] + w[3]) / 2, (w[2] + w[4]) / 2, 0 if abs(w[2] - w[4]) < EPS else 90))
            continue
        for comp in comps:
            placed = False
            if net in POWER:
                pref = (0, 1) if net == "GND" else (0, -1)
                order = sorted(comp, key=lambda q: (q["out"] != pref, -q["y"] if net == "GND" else q["y"]))
            else:
                order = sorted(comp, key=lambda q: q["x"])
            # no room at any pin: try the corners and ends of the cluster's own wires
            pts_ = {(round(q["x"], 3), round(q["y"], 3)) for q in comp}
            segs = [w for w in sh.wires if w[0] == net]
            grew = True
            while grew:
                grew = False
                for (n, x1, y1, x2, y2) in segs:
                    a_, b_ = (round(x1, 3), round(y1, 3)), (round(x2, 3), round(y2, 3))
                    if (a_ in pts_) != (b_ in pts_):
                        pts_ |= {a_, b_}
                        grew = True
            pins_at = {(round(q["x"], 3), round(q["y"], 3)) for q in comp}
            order = order + [dict(x=x, y=y, out=d, ref="") for (x, y) in sorted(pts_ - pins_at)
                             for d in ((-1, 0), (1, 0), (0, -1), (0, 1))]
            for q in order:
                dx, dy = q["out"]
                tries = [([(q["x"] + dx * d, q["y"] + dy * d)], (dx, dy)) for d in (2.54, 5.08, 7.62)]
                kinds = [(t, net in POWER) for t in tries]
                if net in POWER and q["out"] != pref:
                    # step out of the pin, then turn so the symbol stands the usual way
                    kinds = [(([m, (m[0] + pref[0] * 2.54, m[1] + pref[1] * 2.54)], pref), True)
                             for m in [(q["x"] + dx * d, q["y"] + dy * d) for d in (2.54, 5.08)]] + kinds
                if net in POWER:
                    # no room for a symbol (IC side): a port label with the rail's name
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
                    # the box starts a little past the stub end, which sits on the wire itself
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
    # junctions: three or more wire ends/pins at a point, or an end on the middle of a wire
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
    return sh.wires, junc, labels, powers, names

def emit():
    used = []
    for p in parts:
        if p["lib"] not in used:
            used.append(p["lib"])
    install_custom()
    head = ['(kicad_sch (version 20230121) (generator eeschema)', '  (uuid %s)' % ROOT_UUID, '  (paper "A3")',
            '  (title_block (title "%s") (date "2026-10-30") (rev "%s") (company "n3tk0/MeshCore"))' % (TITLE, REV),
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
            # pins all on the right (mirrored connector): text on the left of the body
            cy = p["y"] - sum(ys_) / len(ys_)
            rx, ry, vx, vy, j = p["x"] - 2.0, cy - 0.6, p["x"] - 2.0, cy + 1.6, " (justify right)"
        elif len(pins) <= 4 and max(xs_) < -1:
            cy = p["y"] - sum(ys_) / len(ys_)
            rx, ry, vx, vy, j = p["x"] + 2.0, cy - 0.6, p["x"] + 2.0, cy + 1.6, " (justify left)"
        elif len(pins) <= 3 and not horiz:
            rx, ry, vx, vy, j = p["x"] + max(xs_) + 2.0, p["y"] - 1.0, p["x"] + max(xs_) + 2.0, p["y"] + 1.5, " (justify left)"
        elif horiz:
            rx, ry, vx, vy, j = p["x"], p["y"] - 2.6, p["x"], p["y"] + 2.9, ""
        else:
            top = max(ys_ + [2.54])
            rx, ry, vx, vy, j = p["x"], p["y"] - top - 4.5, p["x"], p["y"] - top - 2.2, ""
            if any(abs(q["y"] - top) < EPS and abs(q["x"]) < 1.3 for q in pins):
                # a pin out of the top middle carries a power symbol: reference beside the pin
                rx, ry, j = p["x"] + 2.0, p["y"] - top + 1.5, " (justify left)"
                vx, vy, vj = p["x"] - 2.0, ry, " (justify right)"
        fa = 90 if rot in (90, 270) else 0   # field angles are relative to the symbol
        if mir or rot == 180:   # and so is the justification
            j = j.replace("right", "LEFT").replace("left", "right").replace("LEFT", "left")
        if vj is None:
            vj = j
        elif mir or rot == 180:
            vj = vj.replace("right", "LEFT").replace("left", "right").replace("LEFT", "left")
        out.append('    (property "Reference" "%s" (at %s %s %d) (effects (font (size 1.27 1.27))%s%s))' % (
            ref, fmt(rx), fmt(ry), fa, j, " hide" if hidden_ref else ""))
        out.append('    (property "Value" "%s" (at %s %s %d) (effects (font (size 1.27 1.27))%s%s))' % (
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
        # body box from the inner ends of the pins (the symbol outline sits there)
        inner = [(p["x"] + q["x"] - OUTWARD[q["ang"]][0] * q["length"], p["y"] - q["y"] - OUTWARD[q["ang"]][1] * q["length"])
                 for q in pins] or [(p["x"], p["y"])]
        xs, ys = [c[0] for c in inner], [c[1] for c in inner]
        if not hidden_ref:
            p["body"] = (min(xs) - 1.0, min(ys) - 1.0, max(xs) + 1.0, max(ys) + 1.0)
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
                continue   # stacked pins with one connection point (e.g. GND pins of the same part)
            pinpts.append(dict(net=net, ref=ref, x=ax, y=ay, out=OUTWARD[pin["ang"]], block=p["block"]))
    NC_POINTS[:] = ncs
    wires, junc, labels, powers, names = wire_up(pinpts)
    for (net, x, y, a) in names:
        out.append('  (label "%s" (at %s %s %d) (fields_autoplaced) (effects (font (size 1.0 1.0)) (justify left bottom)) (uuid %s))' % (
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
                   '    (property "Value" "%s" (at %s %s 0) (effects (font (size 1.27 1.27))))\n'
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
    # block frames around their parts, labels and notes
    for bi, title in enumerate(blocks):
        xs, ys = [], []
        for p in parts:
            if p["block"] == bi and "body" in p:
                xs += [p["body"][0] - 3, p["body"][2] + 3]; ys += [p["body"][1] - 5, p["body"][3] + 3]
        for q in pinpts:
            if q["block"] == bi:
                xs += [q["x"] - 6, q["x"] + 6]; ys += [q["y"] - 4, q["y"] + 6]
        for (x, y, s_, size, blk) in texts:
            if blk == bi:
                xs += [x - 3, x + len(s_) * size * 0.62 + 2]; ys += [y - size - 3, y + 3]
        if not xs:
            continue
        x0, y0, x1, y1 = min(xs), min(ys), max(xs), max(ys)
        # grow over the wires, labels and symbols that start inside it
        inside = lambda x, y: x0 - 0.1 <= x <= x1 + 0.1 and y0 - 0.1 <= y <= y1 + 0.1
        for _ in range(3):
            for (n, ax, ay, bx_, by_) in wires:
                if inside(ax, ay) or inside(bx_, by_):
                    x0, y0 = min(x0, ax - 2, bx_ - 2), min(y0, ay - 2, by_ - 2)
                    x1, y1 = max(x1, ax + 2, bx_ + 2), max(y1, ay + 2, by_ + 2)
        for (net, x, y, d) in labels + powers:
            if inside(x, y):
                ln = len(net) * 0.85 + 3.5
                ex, ey = x + d[0] * ln, y + d[1] * ln
                x0, y0 = min(x0, ex - 2.5), min(y0, ey - 2.5)
                x1, y1 = max(x1, ex + 2.5), max(y1, ey + 2.5)
        out.append('  (polyline (pts (xy %s %s) (xy %s %s) (xy %s %s) (xy %s %s) (xy %s %s)) (stroke (width 0.3) (type default) (color 0 0 194 1)) (uuid %s))' % (
            fmt(x0), fmt(y0), fmt(x1), fmt(y0), fmt(x1), fmt(y1), fmt(x0), fmt(y1), fmt(x0), fmt(y0), uid()))
    out.append('  (sheet_instances (path "/" (page "1")))')
    out.append(')')
    return "\n".join(head + out) + "\n"

if __name__ == "__main__":
    path = os.path.join(ROOT, PROJECT + ".kicad_sch")
    open(path, "w").write(emit())
    # BOM-ish summary for the notes
    print("wrote", path, len(parts), "parts")

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
        # one row per LCSC part: JLC rejects a part number that shows up on two rows
        # (R1 "4.7k 1%" and R11/R12 "4.7k" are the same C23162)
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
    g, h = write_bom()
    print(len(g), "JLC lines,", len(h), "hand parts")
