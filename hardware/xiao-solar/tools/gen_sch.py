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
REV = "0.3"

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
                         ang=int(m.group(4)), num=m.group(7)))
    return pins

# ---------------------------------------------------------------- design

parts = []      # dict(ref, lib, value, x, y, nets, fp, lcsc, dnp, note)
texts = []

def P(ref, lib, value, x, y, nets, fp="", lcsc="", dnp=False, mpn=""):
    parts.append(dict(ref=ref, lib=lib, value=value, x=x, y=y, nets=nets, fp=fp, lcsc=lcsc, dnp=dnp, mpn=mpn))

def T(x, y, s, size=2.0):
    texts.append((x, y, s, size))

R0603 = "Resistor_SMD:R_0603_1608Metric"
C0603 = "Capacitor_SMD:C_0603_1608Metric"
C0805 = "Capacitor_SMD:C_0805_2012Metric"
SOT23 = "Package_TO_SOT_SMD:SOT-23"
CONN2 = "Connector_PinHeader_2.54mm:PinHeader_1x02_P2.54mm_Vertical"
CONN4 = "Connector_PinHeader_2.54mm:PinHeader_1x04_P2.54mm_Vertical"

def R(ref, val, x, y, a, b, lcsc, dnp=False):
    P(ref, "Device:R", val, x, y, {"1": a, "2": b}, R0603, lcsc, dnp)

def C(ref, val, x, y, a, b, lcsc, fp=C0805):
    P(ref, "Device:C", val, x, y, {"1": a, "2": b}, fp, lcsc)

C10U = "C15850"     # 10uF 25V X5R 0805
C10U50 = "C13585"   # 10uF 50V X5R 1206 (VBUS/PMID: derating at 25 V)
C1206 = "Capacitor_SMD:C_1206_3216Metric"
C100N = "C14663"    # 100nF 50V X7R 0603
C47N = "C1622"      # 47nF 50V 0603
C4U7 = "C1779"      # 4.7uF 25V 0805

# ---- 1. Inputs -------------------------------------------------------------
T(15, 15, "1. ВХОДОВЕ: соларен панел 5-22 V (Voc до 25 V на студ) и втори вход 5-20 V", 2.0)
P("J1", "Connector_Generic:Conn_01x02", "SOLAR", 20, 35, {"1": "SOLAR_IN", "2": "GND"}, CONN2)
P("J2", "Connector_Generic:Conn_01x02", "VIN2 5-20V", 20, 55, {"1": "VIN2_IN", "2": "GND"}, CONN2)
P("D1", "Device:D_Schottky", "SS34", 45, 30, {"2": "SOLAR_IN", "1": "VBUS"}, "Diode_SMD:D_SMA", "C8678")
P("D2", "Device:D_Schottky", "SS34", 45, 50, {"2": "VIN2_IN", "1": "VBUS"}, "Diode_SMD:D_SMA", "C8678")
P("D3", "xiao_solar:SMBJ24A-C87268", "SMBJ24A", 65, 40, {"1": "VBUS", "2": "GND"}, "xiao_solar:SMB_L4.6-W3.6-LS5.3-RD", "C19077578")
C("C1", "10u/50V", 78, 40, "VBUS", "GND", C10U50, C1206)
C("C2", "10u/50V", 88, 40, "VBUS", "GND", C10U50, C1206)
C("C3", "100n", 98, 40, "VBUS", "GND", C100N, C0603)
T(15, 68, "SS34 = OR на двата входа + защита от обратна полярност. VAC1=VAC2=VBUS, ACDRV към GND (без ACFET).", 1.27)

# ---- 2. Charger --------------------------------------------------------------
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
# TS: 103AT NTC via pads, RT1/RT2 per TI (0..60 C window); firmware sets TS_IGNORE by default
R("R2", "5.1k 1%", 160, 100, "REGN", "TS", "C23186")
R("R3", "30k 1%", 160, 115, "TS", "GND", "C22984")
P("J3", "Connector_Generic:Conn_01x02", "NTC 10k (103AT)", 175, 108, {"1": "TS", "2": "GND"}, CONN2)
# CE (active low) on D0. JP2 picks what R4 does while D0 is not driven (Xiao off/resetting):
# 1-2 (default, Li-ion) pull-down = charges without the Xiao, on BQ defaults 4.2 V / 1 A after a POR;
# 2-3 (LiFePO4) pull-up to 3V3 = charges only after firmware wrote VREG 3.6 V and drives D0 low.
R("R4", "100k", 30, 160, "CE_PULL", "CE", "C25803")
P("JP2", "Jumper:SolderJumper_3_Bridged12", "CE: 1-2 Li-ion / 2-3 LiFePO4", 30, 175,
  {"1": "GND", "2": "CE_PULL", "3": "3V3"}, "Jumper:SolderJumper-3_P1.3mm_Bridged12_RoundedPad1.0x1.5mm")
# STAT LED (only lit while charging, firmware can disable with DIS_STAT)
R("R6", "2.2k", 55, 160, "REGN", "LED_STAT", "C4190")
P("D4", "Device:LED", "green", 55, 175, {"1": "STAT", "2": "LED_STAT"}, "LED_SMD:LED_0805_2012Metric", "C2297")
# INT (pin 21) is left open: the firmware polls the status registers, and the pin has no
# room to escape between the BAT caps and PROG.
P("TP2", "Connector:TestPoint", "QON (to GND = wake)", 90, 165, {"1": "QON"}, "TestPoint:TestPoint_Pad_D1.0mm")
T(15, 192, "CE: JP2 1-2 (Li-ion) зарежда и без Xiao; 2-3 (LiFePO4) само след запис на VREG и D0=0. Watchdog off. D+/D- не се ползват.", 1.27)

# ---- 3. Battery / BMS --------------------------------------------------------
T(15, 205, "3. БАТЕРИЯ 1S (Li-ion по подразбиране, LiFePO4 с настройка): ship FET + DW01A/HJ8205", 2.0)
P("J4", "Connector_Generic:Conn_01x02", "BATTERY", 20, 230, {"1": "BATT+", "2": "BATT-"}, CONN2)
P("Q2", "Transistor_FET:BSS138", "AO3400A (ship FET)", 50, 225, {"1": "SDRV", "2": "BAT", "3": "BATT+"}, SOT23, "C20917")
R("R8", "100", 70, 225, "BATT+", "BATP", "C22775")
R("R9", "100", 90, 245, "BATT+", "DW_VCC", "C22775")
C("C19", "100n", 100, 255, "DW_VCC", "BATT-", C100N, C0603)
P("U2", "xiao_solar:DW01A_C351410", "DW01A", 130, 250,
  {"1": "DW_OD", "2": "DW_CS", "3": "DW_OC", "4": None, "5": "DW_VCC", "6": "BATT-"},
  "xiao_solar:SOT-23-6_L2.9-W1.6-P0.95-LS2.8-BL", "C351410")
R("R10", "1k", 160, 255, "DW_CS", "GND", "C21190")
P("Q3", "xiao_solar:FS8205A_C2830320", "HJ8205", 175, 235,
  {"1": "BATT-", "2": "FS_D", "3": "GND", "4": "DW_OC", "5": "FS_D", "6": "DW_OD"},
  "xiao_solar:SOT-23-6_L2.9-W1.6-P0.95-LS2.8-BL", "C20069150")
T(15, 270, "Q2: D към батерията, S към BAT; в shutdown (SDRV_CTRL=01, 0.5 uA) диодът на Q2 спира разреждането,", 1.27)
T(15, 274, "събужда се само когато се появи напрежение от панела. DW01A: 2.4 V / 4.3 V резерв, HJ8205 в минуса.", 1.27)

# ---- 4. XIAO + Wio ----------------------------------------------------------------
T(205, 15, "4. XIAO nRF52840 + Wio-SX1262 (пинове като variants/xiao_nrf52)", 2.0)
P("U3", "custom:XIAO_nRF52840", "XIAO nRF52840", 235, 45,
  {"1": "CE", "2": "LORA_DIO1", "3": "LORA_RST", "4": "LORA_BUSY", "5": "LORA_NSS",
   "6": "LORA_RXEN", "7": "SCL", "14": "XIAO_5V", "13": "GND", "12": "3V3",
   "11": "SPI_MOSI", "10": "SPI_MISO", "9": "SPI_SCK", "8": "SDA"}, "xiao_solar:XIAO_14P_2.54_21x17.8mm")
P("U4", "custom:Wio-SX1262_for_XIAO", "Wio-SX1262 for XIAO", 300, 45,
  {"1": None, "2": "LORA_DIO1", "3": "LORA_RST", "4": "LORA_BUSY", "5": "LORA_NSS",
   "6": "LORA_RXEN", "7": None, "14": None, "13": "GND", "12": "3V3",
   "11": "SPI_MOSI", "10": "SPI_MISO", "9": "SPI_SCK", "8": None}, "xiao_solar:XIAO_14P_2.54_21x17.8mm")
P("D5", "Device:D_Schottky", "B5819W", 235, 80, {"2": "SYS", "1": "XIAO_5V"}, "Diode_SMD:D_SOD-123", "C8598")
C("C20", "10u/25V", 255, 85, "XIAO_5V", "GND", C10U)
C("C21", "10u/25V", 300, 85, "3V3", "GND", C10U)
C("C22", "100n", 310, 85, "3V3", "GND", C100N, C0603)
T(205, 100, "D5: SYS към пина 5V на Xiao (USB на Xiao не захранва SYS). 3V3 идва от регулатора на Xiao.", 1.27)

# ---- 5. I2C ---------------------------------------------------------------------------
T(205, 115, "5. I2C (SCL=D6, SDA=D7): BQ25798 0x6B, SHT40 0x44, PCA9685 0x41", 2.0)
R("R11", "4.7k", 215, 130, "3V3", "SCL", "C23162")
R("R12", "4.7k", 225, 130, "3V3", "SDA", "C23162")
P("J5", "Connector_Generic:Conn_01x04", "I2C rail", 215, 150, {"1": "SCL", "2": "SDA", "3": "3V3", "4": "GND"}, CONN4)
P("U5", "xiao_solar:SHT40-AD1B-R3", "SHT40-AD1B", 255, 140,
  {"1": "SDA", "2": "SCL", "3": "3V3", "4": "GND", "5": "GND"},
  "xiao_solar:DFN-4_L1.5-W1.5-P0.8-TL-EP", "C2848306")
C("C23", "100n", 275, 140, "3V3", "GND", C100N, C0603)

# ---- 6. Loads ---------------------------------------------------------------------------
T(205, 160, "6. ИЗХОДИ ЗА ТОВАРИ (PWM, до 2 A): нагревател, вентилатор, резерв", 2.0)
# LED2 (pin 8) drives OUT1 and LED0 (pin 6) OUT3: the pin order then matches the output order on the board
pca = {"1": "3V3", "2": "GND", "3": "GND", "4": "GND", "5": "GND", "6": "PWM3", "7": "PWM2", "8": "PWM1",
       "14": "GND", "23": "GND", "24": "GND", "25": "GND", "26": "SCL", "27": "SDA", "28": "3V3"}
for n in range(9, 23):
    if str(n) not in pca:
        pca[str(n)] = None
P("U6", "xiao_solar:PCA9685PW,118", "PCA9685PW", 240, 215, pca,
  "xiao_solar:TSSOP-28_L9.7-W4.4-P0.65-LS6.4-TL", "C2678753")
C("C24", "100n", 220, 245, "3V3", "GND", C100N, C0603)
# Loads run from SYS through a ferrite bead; the bulk caps sit at the load end so the PWM
# pulse current circulates locally. Load returns use PGND, tied to GND in one point (NT1).
# same part as L1 (one JLC extended slot instead of two); 2.2 uH + 44 uF = LC filter ~16 kHz
P("FB1", "xiao_solar:FNR4030S2R2MT", "2.2uH 3.2A", 285, 172, {"1": "SYS", "2": "VLOAD"},
  "xiao_solar:IND-SMD_L4.0-W4.0_FNR40XXS", "C167869")
C("C25", "22u/25V", 300, 172, "VLOAD", "PGND", "C12891", C1206)
C("C26", "22u/25V", 310, 172, "VLOAD", "PGND", "C12891", C1206)
C("C27", "100n", 320, 172, "VLOAD", "PGND", C100N, C0603)
P("NT1", "Device:NetTie_2", "PGND-GND", 265, 190, {"1": "PGND", "2": "GND"}, "NetTie:NetTie-2_SMD_Pad0.5mm")
for i, (name, x) in enumerate([("OUT1 heater", 300), ("OUT2 fan", 335), ("OUT3 spare", 370)]):
    k = i + 1
    # 1k gate resistor slows the edges (less RF hash near the radio)
    R("R%d" % (13 + 2 * i), "1k", x, 200, "PWM%d" % k, "G%d" % k, "C21190")
    R("R%d" % (14 + 2 * i), "100k", x - 8, 222, "G%d" % k, "PGND", "C25803")
    P("Q%d" % (4 + i), "Transistor_FET:BSS138", "AO3400A", x + 5, 215,
      {"1": "G%d" % k, "2": "PGND", "3": "OUT%d-" % k}, SOT23, "C20917")
    C("C%d" % (28 + i), "100n", x + 35, 215, "OUT%d+" % k, "OUT%d-" % k, C100N, C0603)
    P("F%d" % k, "Device:Polyfuse", "2A/16V", x + 15, 185, {"1": "VLOAD", "2": "OUT%d+" % k},
      "Fuse:Fuse_1206_3216Metric", "C2685749")
    P("D%d" % (6 + i), "Device:D_Schottky", "B5819W", x + 25, 205, {"1": "OUT%d+" % k, "2": "OUT%d-" % k},
      "Diode_SMD:D_SOD-123", "C8598")
    P("J%d" % (7 + i), "Connector_Generic:Conn_01x02", name, x + 25, 232,
      {"1": "OUT%d+" % k, "2": "OUT%d-" % k}, CONN2)

# ---- power flags / mechanics ---------------------------------------------------------
for i, net in enumerate(["GND", "VBUS", "SYS", "BATT-", "VLOAD", "BAT", "BATT+", "REGN", "PMID", "XIAO_5V", "PGND"]):
    P("#FLG%02d" % (i + 1), "power:PWR_FLAG", "PWR_FLAG", 330 + (i % 5) * 15, 120 + (i // 5) * 15, {"1": net})
for i in range(4):
    P("H%d" % (i + 1), "Mechanical:MountingHole", "M2/M2.5", 360 + (i % 2) * 15, 160 + (i // 2) * 12, {},
      "xiao_solar:MountingHole_2.8mm_Keepout6mm")

# ---------------------------------------------------------------- writer

def fmt(v):
    s = "%.4f" % v
    s = s.rstrip("0").rstrip(".")
    return "0" if s == "-0" else s

def emit():
    used = []
    for p in parts:
        if p["lib"] not in used:
            used.append(p["lib"])
    install_custom()
    out = ['(kicad_sch (version 20230121) (generator eeschema)', '  (uuid %s)' % ROOT_UUID, '  (paper "A3")',
           '  (title_block (title "%s") (date "2026-10-29") (rev "%s") (company "n3tk0/MeshCore"))' % (TITLE, REV),
           '  (lib_symbols']
    for lid in used:
        out.append(symbol_def(lid))
    out.append('  )')
    labels = []
    ncs = []
    for p in parts:
        pins = symbol_pins(p["lib"])
        u = uid()
        ref = p["ref"]
        hidden_ref = ref.startswith("#")
        out.append('  (symbol (lib_id "%s") (at %s %s 0) (unit 1) (in_bom %s) (on_board %s) (dnp %s) (uuid %s)' % (
            p["lib"].replace("custom:", "xiao_solar:"), fmt(p["x"]), fmt(p["y"]), "no" if hidden_ref else "yes", "no" if hidden_ref else "yes",
            "yes" if p["dnp"] else "no", u))
        small = all(abs(q["x"]) < 1 for q in pins) and len(pins) <= 3
        if small:
            rx, ry, vx, vy, j = p["x"] + 2.0, p["y"] - 1.0, p["x"] + 2.0, p["y"] + 1.5, " (justify left)"
        else:
            top = max([q["y"] for q in pins] + [2.54])
            rx, ry, vx, vy, j = p["x"], p["y"] - top - 4.5, p["x"], p["y"] - top - 2.2, ""
        out.append('    (property "Reference" "%s" (at %s %s 0) (effects (font (size 1.27 1.27))%s%s))' % (
            ref, fmt(rx), fmt(ry), j, " hide" if hidden_ref else ""))
        out.append('    (property "Value" "%s" (at %s %s 0) (effects (font (size 1.27 1.27))%s%s))' % (
            p["value"], fmt(vx), fmt(vy), j, " hide" if hidden_ref else ""))
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
            la = {0: 180, 180: 0, 90: 270, 270: 90}[pin["ang"]]
            labels.append((net, ax, ay, la))
    for (net, x, y, a) in labels:
        just = {0: "left bottom", 180: "right bottom", 90: "left bottom", 270: "right bottom"}[a]
        out.append('  (label "%s" (at %s %s %d) (fields_autoplaced) (effects (font (size 1.0 1.0)) (justify %s)) (uuid %s))' % (
            net, fmt(x), fmt(y), a, just, uid()))
    for (x, y) in ncs:
        out.append('  (no_connect (at %s %s) (uuid %s))' % (fmt(x), fmt(y), uid()))
    for (x, y, s, size) in texts:
        out.append('  (text "%s" (at %s %s 0) (effects (font (size %s %s)) (justify left bottom)) (uuid %s))' % (
            s.replace('"', "'"), fmt(x), fmt(y), fmt(size), fmt(size), uid()))
    out.append('  (sheet_instances (path "/" (page "1")))')
    out.append(')')
    return "\n".join(out) + "\n"

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
        k = (p["value"], p["fp"].split(":")[-1], p["lcsc"])
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
