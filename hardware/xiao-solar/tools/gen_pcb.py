#!/usr/bin/env python3
"""Builds xiao-solar.kicad_pcb from the part list in gen_sch.py: footprints, nets,
placement, board outline, net classes and GND zones. Routing is done afterwards
by tools/route.py (Freerouting), so this file only owns placement.

Run: python3 tools/gen_pcb.py  (KiCad 7 pcbnew Python module)
"""
import os, sys
import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import gen_sch  # noqa: E402  (builds the part list on import)
import charger  # noqa: E402
import dfm  # noqa: E402

PROJECT = "xiao-solar"
PCB = os.path.join(ROOT, PROJECT + ".kicad_pcb")
FPLIB_SYS = "/usr/share/kicad/footprints"
if not os.path.isdir(FPLIB_SYS):
    FPLIB_SYS = "C:/Program Files/KiCad/10.0/share/kicad/footprints"
FPLIB_LOCAL = os.path.join(ROOT, "lib", "xiao_solar.pretty")
# Design coordinates run from y = 0 to H; the board itself starts at y = TOP (v0.5: the top
# 5 mm were cut off, 50 x 65 mm), and its top-left corner sits at (100, 100) on the sheet.
W, H = 50.0, 70.0          # design area, mm
TOP = 5.0                  # first design y on the board
OX, OY = 100.0, 100.0 - TOP

def mm(v):
    return pcbnew.FromMM(v)

# 50 x 65 (Petko, 2026-11-12, after the JLC DFM report): the coordinates written in this file are
# those of the 50 x 60 layout (bottom edge H0) and ys() stretches them: +1 mm under the top row
# (J5, pull-ups), +3.1 mm from the band under the modules down (every SMD pad >= 3 mm from the
# module pins), +5 mm for the bottom row (D1 / D3 >= 3 mm from the connector pins).
H0 = 65.0

def ys(y):
    if y < 10.5:
        return y
    if y < 30.0:
        return y + 1.0
    if y < 60.0:
        return y + 3.1
    return y + 5.0

def pt(x, y):
    return pcbnew.VECTOR2I(mm(OX + x), mm(OY + ys(y)))

# ref: (x, y, rotation_deg) in design coordinates (board y + TOP).
# v0.5 50 x 65 (Petko, 2026-11-12; laid out as 50 x 60, see ys()): Xiao (U3, left) and Wio (U4, right, turned 180) side by side
# under the top edge, the ideal diode in the channel between them; HT7533, MSPM0 and BMS in a
# band under the modules; the charger below, connectors along the bottom edge.
PLACE = {
    # JP3 bridges the ship FET Q2 (closed = no ship FET, for LiFePO4)
    "JP3": (32.8, 58.0, 90),
}
# charger cluster (tools/charger.py), U1 at its position before the DY / LO shifts below
PLACE.update(charger.place(charger.Frame(16.0, 55.0), set(charger.PLACE)))
# DY: the 50 x 75 layout moved everything down by 6 mm for the top holes; LO: the 50 x 60 layout
# lifts the charger by 10 mm into the band left free by the side-by-side modules
DY = 6.0
LO = -10.0
CHARGER_FRAME = charger.Frame(16.0, ys(55.0 + DY + LO), ox=OX, oy=OY)
PLACE = {r: (x, y + DY + LO, a) for r, (x, y, a) in PLACE.items()}
# C9 (PMID HF cap) is 0603 now (basic 50 V part, as on solar-smart-mppt): it stands upright
# right above pin 29 (PMID pad down), its GND pad ties into the pin 27 GND line;
# C16 steps 0.35 mm right, clear of the C9 courtyard (fix_charger_routes)
PLACE["C9"] = (PLACE["C9"][0] - 0.42, PLACE["C9"][1] - 0.87, 90)
PLACE["C16"] = (PLACE["C16"][0] + 0.35, PLACE["C16"][1], PLACE["C16"][2])
MCU_D = (-5.2, -10.85)   # the MSPM0 cluster moved by this from its first v0.5 place (U6 27.2, 45.6)
def _m(x, y):
    return (round(x + MCU_D[0], 3), round(y + MCU_D[1], 3))
PLACE.update({
    # modules: Xiao USB to the left edge, Wio turned 180 so its pins 2-6 face the Xiao pins 9-12
    # diagonally (two nested groups, one per layer)
    "U3": (11.2, 20.8, 90), "U4": (38.8, 20.8, 270),
    # I2C (J5) along the top edge above the Xiao (pin 1 = SCL on the left), pull-ups right of it
    "J5": (9.5, TOP + 1.6, 90), "R11": (22.8, 7.2, 0), "R12": (22.8, 9.2, 0),
    # channel between the modules: ideal-diode mirror (R30, Q6 on SYS, Q7 on XIAO_5V, R31) and
    # the FET Q5 at the bottom (D down to the SYS run, S / G up)
    "R30": (25.0, 12.8, 0), "Q6": (25.0, 15.7, 0), "Q7": (25.0, 19.3, 0), "R31": (25.0, 22.2, 0),
    "Q5": (25.0, 25.6, 270),
    # band under the modules, left to right: Xiao 5V bulk, HT7533 with C25 (SYS) / C26 (3V3M),
    # Xiao 3V3 cap, MSPM0 cluster, VBAT divider, CHEM jumper, SWD (bottom), BMS
    "C20": (2.0, 35.5, 270), "C25": (4.6, 35.5, 90), "U8": (8.8, 35.5, 0), "C26": (12.4, 35.5, 90),
    "C21": (15.0, 35.5, 90), "C22": (41.3, 31.3, 0),
    "U6": (_m(27.2, 45.6)[0], _m(27.2, 45.6)[1], 0),
    "C27": _m(23.55, 45.35) + (180,), "C29": _m(23.55, 44.3) + (180,), "R13": _m(23.55, 43.3) + (0,),
    "C28": _m(24.9, 41.9) + (180,),
    "R14": _m(29.62, 48.95) + (180,), "R15": _m(29.62, 50.9) + (180,), "R4": _m(31.3, 48.75) + (90,),
    "R25": (30.5, 32.7, 180), "C30": (30.5, 33.9, 0), "R26": (30.5, 35.4, 0),
    "JP2": (35.5, 34.0, 90),
    # SWD: a row of five pads on the bottom under the Wio pin row, above the BQ bus lanes
    "TP12": (31.5, 31.6, 0), "TP15": (34.04, 31.6, 0), "TP11": (36.58, 31.6, 0),
    "TP14": (39.12, 31.6, 0), "TP13": (41.66, 31.6, 0),
    # BMS (DW01A + HJ8205) and the 20 mOhm low-side shunt on the right: Q3 S2 (BATT_S) -> RS1
    # (BATT_S end at the bottom, next to Q3 pin 3) -> GND (top, vias to the right edge), Kelvin
    # taps R21 (BATT_S, bottom row) and R22 (SH_K, net tie NT1 on the RS1 GND pad, top row), C24
    # across them, bias R23 / R24 to 3V3M
    "R9": (45.0, 31.5, 0), "U2": (45.0, 34.7, 0), "C19": (48.2, 34.7, 90), "R10": (45.0, 37.9, 0),
    "R23": (40.6, 37.6, 0), "R24": (40.6, 38.8, 0),
    "Q3": (41.0, 41.8, 0), "RS1": (46.8, 42.2, 90),
    "R22": (44.2, 40.74, 180), "C24": (44.2, 42.2, 0), "R21": (44.2, 43.66, 180), "NT1": (45.66, 40.74, 0),
    # connectors: SOLAR | BAT | NTC along the bottom edge (pin 1 = + square), HEAT (J6) on the right
    "J1": (9.0, 63.3, 90), "J4": (27.0, 63.3, -90), "J3": (35.0, 63.3, 90), "J6": (47.5, 52.0, 0),
    # SOLAR -> D1 (reverse polarity) -> VBUS, TVS D3 beside it, both above the bottom row
    "D1": (11.8, 59.5, 180), "D3": (21.0, 59.5, 0),
    # heater: Q4 left of J6 (>= 3 mm from its pins), gate parts left of it, LED (D6 + R29 across
    # J6) under Q4
    "Q4": (41.9, 54.0, 0), "R27": (38.4, 53.3, 0), "R28": (38.4, 55.0, 0),
    "D6": (41.2, 56.9, 180), "R29": (41.2, 58.6, 180),
})
B_SIDE = {"TP11", "TP12", "TP13", "TP14", "TP15"}
# four holes 2.8 mm (M2/M2.5), 6 mm clear for the head, 3.5 mm from the edges
PLACE.update({"H1": (3.5, TOP + 3.5, 0), "H2": (W - 3.5, TOP + 3.5, 0), "H3": (3.5, H0 - 3.5, 0),
              "H4": (W - 3.5, H0 - 3.5, 0)})

OUTLINE = [(0, TOP), (W, TOP), (W, H0), (0, H0)]

# Bring-up test points: net -> (region x0, y0, x1, y1, preferred x, y). Charger nets sit
# inside their copper zone when there is room; the others are wired by the router.
TEST_POINTS = {
    "VBUS": (9.0, 48.0, 15.0, 54.0, 10.0, 51.5), "PMID": (4.5, 44.0, 12.0, 49.0, 6.0, 48.0),
    "SYS": (20.0, 44.0, 32.0, 49.0, 30.5, 45.5), "BAT": (22.0, 48.5, 34.0, 56.0, 29.5, 55.6),
    "SCL": (24.3, 5.6, 36.0, 10.3, 25.5, 8.0), "SDA": (24.3, 5.6, 36.0, 10.3, 28.0, 8.0),
    "3V3": (24.3, 5.6, 36.0, 10.3, 30.5, 8.0), "GND": (24.3, 5.6, 36.0, 10.3, 33.0, 8.0),
}
# three fiducials: region x0, y0, x1, y1 and the preferred point
FIDUCIALS = [(35.0, 5.6, 40.0, 10.3, 37.5, 8.0), (0.8, 38.2, 4.2, 41.0, 2.5, 39.6),
             (28.0, 56.0, 33.0, 62.0, 30.5, 60.5)]
FIDUCIALS = [(x0, ys(y0), x1, ys(y1), px, ys(py)) for x0, y0, x1, y1, px, py in FIDUCIALS]
TEST_POINTS = {n: (x0, ys(y0), x1, ys(y1), px, ys(py)) for n, (x0, y0, x1, y1, px, py) in TEST_POINTS.items()}
# B.Cu under the Wio (between its pin rows) stays whole GND (Petko: no link under the Wio on the
# bottom; the Xiao-Wio links cross under the Xiao)
WIO_IN = (28.0, ys(13.18) + 1.05, 49.6, ys(28.42) - 1.05)
B_CLEAR = [WIO_IN]
# and no autorouted copper on F.Cu there either: whole GND on both layers under the radio
FB_CLEAR = [WIO_IN]
# pre-routes stay out from between the Xiao pin rows too (but the LoRa control links): the Xiao
# top row (SPI, 3V3) goes down the channel to the Wio bottom row from below, the Xiao bottom row
# (LoRa control) under the Xiao, up through the channel and over the Wio to its top row
XIAO_IN = (0.0, ys(13.18) + 1.05, 19.9, ys(28.42) - 1.05)
SPI_B = ("SPI_SCK", "SPI_MISO", "SPI_MOSI", "3V3")
# the charger's whole-GND B.Cu area starts 1.4 mm lower than charger.B_CLEAR (still above the
# bulk caps): the strip above it carries the BQ25798 bus from the right to the MSPM0
CHARGER_B_CLEAR = (-11.5, -9.0, 15.8, 1.0)

def place_test_points(board, tps, frame, spec, ox=OX, oy=OY):
    """tps: ref -> (footprint, net name). Each goes into its charger zone when it fits there,
    else to the free spot nearest the preferred point."""
    placed = {}
    for ref, (fp, net) in sorted(tps.items(), key=lambda t: int(t[0][2:])):
        x0, y0, x1, y1, px, py = spec[net]
        inside = charger.zone_poly(frame, net, 0.7) if net in charger.ZONES else None
        at = None
        if inside is not None and inside.OutlineCount():
            at = dfm.free_spot(board, (x0, y0, x1, y1), (px, py), (1.05, 0.75), ox, oy, inside=inside, net=net)
        if at is None:
            at = dfm.free_spot(board, (x0, y0, x1, y1), (px, py), (1.05, 0.75), ox, oy, net=net)
        if at is None:
            raise SystemExit("no room for test point %s (%s)" % (ref, net))
        fp.SetPosition(pcbnew.VECTOR2I(mm(OX + at[0]), mm(OY + at[1])))   # spec is stretched already
        board.Add(fp)
        placed[ref] = at
    return placed

def plus_marks(board, refs, dx=0.0, dy=-1.8, size=0.8):
    """A '+' next to pin 1 of the power connectors."""
    for ref in refs:
        p = _pad(board, ref, "1").GetPosition()
        t = pcbnew.PCB_TEXT(board)
        t.SetText("+"); t.SetPosition(pcbnew.VECTOR2I(p.x + mm(dx), p.y + mm(dy))); t.SetLayer(pcbnew.F_SilkS)
        t.SetTextSize(pcbnew.VECTOR2I(mm(size), mm(size))); t.SetTextThickness(mm(0.15))
        board.Add(t)

NETCLASSES = {
    # name: (track mm, clearance mm, via dia, via drill, nets)
    # Power: input and battery paths, up to 3 A (0.6 mm routed, widened to 0.8 by route.py where it fits).
    "Power": (0.6, 0.2, 0.8, 0.4, ["BATT+", "BATT-", "BATT_S", "FS_D", "SOLAR_IN", "XIAO_5V",
                                    "VBUS", "SYS", "BAT", "PMID", "HEAT_D"]),
    # SW nodes are copper zones at the charger (tools/charger.py); nothing else routes them.
    "PowerQFN": (0.25, 0.13, 0.8, 0.4, ["SW1", "SW2"]),
    # 0.3 mm: the MSPM0 pins are 0.25 mm wide at 0.5 mm pitch
    "Supply": (0.3, 0.13, 0.6, 0.3, ["3V3", "3V3M", "GND"]),
}

def load_fp(fpid):
    lib, name = fpid.split(":")
    path = FPLIB_LOCAL if lib == "xiao_solar" else os.path.join(FPLIB_SYS, lib + ".pretty")
    fp = pcbnew.FootprintLoad(path, name)
    if fp is None:
        raise KeyError(fpid)
    return fp

def _pad(board, ref, num):
    for pad in board.FindFootprintByReference(ref).Pads():
        if pad.GetNumber() == num:
            return pad
    raise KeyError(ref + "." + num)

def _track(board, a, b, net, width):
    t = pcbnew.PCB_TRACK(board)
    t.SetStart(a); t.SetEnd(b); t.SetWidth(mm(width)); t.SetLayer(pcbnew.F_Cu); t.SetNet(net)
    t.SetLocked(True)  # exported as fixed wiring, so the router keeps it
    board.Add(t)

# MSPM0 fan-out by hand (as on solar-smart-mppt): locked F.Cu stubs from each pin to its part
# or to a via. MCU_LOCAL is in the coordinates of the first v0.5 place and moves with the
# cluster by MCU_D; top pins nest upwards to vias, SWD / SH_N right, I2C down to the pull-ups.
MCU_LOCAL = [
    ("NRST", [(25.21, 44.85), (24.45, 44.85), (24.03, 44.43)]),
    ("NRST", [(24.03, 43.3), (24.03, 44.3)]),
    ("NRST", [(24.06, 43.3), (24.85, 42.95)]),
    ("3V3M", [(25.21, 45.35), (24.03, 45.35)], 0.25),
    ("3V3M", [(24.03, 45.35), (24.1, 46.3)], 0.25),
    ("3V3M", [(23.07, 43.3), (23.07, 42.4)], 0.25),
    ("GND", [(25.21, 45.85), (26.2, 45.85)]),
    ("VCORE", [(26.45, 43.61), (26.45, 42.5), (25.85, 41.9)]),
    ("HEAT", [(26.95, 43.61), (26.95, 41.2), (30.2, 41.2)]),
    ("SH_P", [(27.45, 43.61), (27.45, 41.9), (31.0, 41.9)]),
    ("ADC_VBAT", [(27.95, 43.61), (27.95, 42.6), (30.2, 42.6)]),
    ("ADC_CHEM", [(28.45, 43.61), (28.45, 43.2), (31.0, 43.2)]),
    ("SWCLK", [(29.19, 45.35), (29.9, 45.35), (30.4, 44.85)]),
    ("SWDIO", [(29.19, 45.85), (31.2, 45.85)]),
    ("SH_N", [(29.19, 46.35), (30.1, 46.35), (30.6, 46.85), (31.6, 46.85)]),
    ("CE", [(29.19, 46.85), (29.6, 46.85), (30.2, 47.45), (30.85, 47.45), (31.3, 47.92)]),
    ("CE", [(30.4, 48.1), (31.3, 47.92)]),
    # BQ25798 bus: pull-ups beside the bottom pins, bus pads facing the pins, a via under each
    ("SDA_I", [(28.45, 47.59), (28.45, 48.3), (29.11, 48.95), (29.11, 49.9)]),
    ("SCL_I", [(27.95, 47.59), (27.95, 49.6), (29.11, 50.76), (29.11, 51.7)]),
    ("3V3M", [(30.13, 48.95), (30.13, 50.9)], 0.25),
    ("3V3M", [(30.13, 49.58), (31.3, 49.58)], 0.25),
    # the pull-up group gets 3V3M from the C27 via on B.Cu, under the bottom pins of U6
    ("3V3M", [(30.13, 49.9), (29.7, 49.47), (29.7, 48.6), (29.3, 48.2), (24.6, 48.2), (24.1, 47.7),
              (24.1, 46.3)], 0.25, "B"),
    # GND tie the pour cannot reach: C29 + C27 (MSPM0 VDD caps) to a via
    ("GND", [(23.07, 44.3), (23.07, 45.35), (23.4, 46.6)], 0.3),
    ("SDA", [(26.95, 47.59), (26.95, 48.2), (26.1, 49.05)]),
    ("SCL", [(27.45, 47.59), (27.45, 48.6), (26.95, 49.1), (26.95, 50.0)]),
]
MCU_LOCAL_VIAS = [("3V3M", (24.1, 46.3)), ("3V3M", (23.07, 42.4)), ("HEAT", (30.2, 41.2)), ("SH_P", (31.0, 41.9)),
                  ("ADC_VBAT", (30.2, 42.6)), ("ADC_CHEM", (31.0, 43.2)), ("SWCLK", (30.4, 44.85)),
                  ("SWDIO", (31.2, 45.85)), ("SH_N", (31.6, 46.85)),
                  ("CE", (30.4, 48.1)), ("SDA_I", (29.11, 49.9)), ("SCL_I", (29.11, 51.7)),
                  ("NRST", (24.85, 42.95)), ("3V3M", (30.13, 49.9)), ("GND", (23.4, 46.6)),
                  # 4 GND vias in the exposed pad, tented from below
                  ("GND", (26.7, 45.1)), ("GND", (27.7, 45.1)), ("GND", (26.7, 46.1)), ("GND", (27.7, 46.1))]
_cl, _cv = _m(29.11, 51.7), _m(30.4, 48.1)          # SCL_I and CE vias of the cluster
_sv = _m(29.11, 49.9)                               # SDA_I via
MCU_STUBS = [(st[0], [_m(*q) for q in st[1]]) + tuple(st[2:]) for st in MCU_LOCAL] + [
    # BQ25798 bus on B.Cu: east from the cluster vias in nested steps (CE top / outermost) to the
    # lanes x 32.0 / 32.5 / 33.0 and down them, then back west under the charger (below its
    # whole-GND area and the BATP run) to the U1 stubs (SDA_I, SCL_I, CE from the top).
    ("CE", [_cv, (33.0, _cv[1]), (33.0, 54.8), (21.2, 54.8), (18.0, 58.0), (16.2, 57.98)], 0.2, "B"),
    ("SDA_I", [_sv, (_sv[0] + 0.7, _sv[1] + 0.7), (32.5, _sv[1] + 0.7), (32.5, 53.6), (19.75, 53.6),
               (18.95, 54.4), (18.95, 54.8)], 0.2, "B"),
    ("SDA_I", [(18.95, 54.8), (18.2, 54.8)]),
    ("SCL_I", [_cl, (32.0, _cl[1]), (32.0, 52.95), (25.2, 52.95)], 0.2, "B"),
    ("SCL_I", [(25.2, 52.95), (24.3, 54.2)]),
    ("SCL_I", [(24.3, 54.2), (20.4, 54.2), (18.95, 55.65), (18.95, 55.75)], 0.2, "B"),
    ("SCL_I", [(18.95, 55.75), (18.2, 55.6)]),
    # the charger's right corner: REGN over TS to R2, BATP on B.Cu above the bus to R8 (as v0.4)
    ("REGN", [(17.9, 52.18), (18.7, 52.18), (19.0, 52.48), (22.6, 52.48), (23.22, 53.1), (23.22, 54.8)], 0.2),
    ("TS", [(17.8, 52.58), (18.3, 53.08), (20.1, 53.08), (21.58, 54.56), (21.58, 54.8)], 0.2),
    ("BATP", [(17.92, 51.78), (18.62, 51.78), (18.9, 51.56)]),
    ("BATP", [(18.9, 51.56), (19.65, 52.25), (28.6, 52.25), (28.85, 52.0)], 0.2, "B"),
    ("BATP", [(28.85, 52.0), (28.78, 53.0)]),
    # BAT from the charger's BAT copper down to JP3 (bypass of the ship FET), as v0.4
    ("BAT", [(26.25, 50.64), (25.6, 51.3), (25.6, 51.78), (28.47, 54.65), (32.8, 54.65)], 0.6),
]
MCU_VIAS = [(n, _m(*q)) for n, q in MCU_LOCAL_VIAS] + [
    ("SCL_I", (25.2, 52.95)), ("SCL_I", (24.3, 54.2)), ("SCL_I", (18.95, 55.75)),
    ("SDA_I", (18.95, 54.8)), ("CE", (16.2, 57.98)), ("BATP", (18.9, 51.56)), ("BATP", (28.85, 52.0))]

def add_mcu_stubs(board):
    for st in MCU_STUBS:
        n = board.FindNet(st[0])
        w = st[2] if len(st) > 2 else 0.2
        layer = pcbnew.B_Cu if len(st) > 3 and st[3] == "B" else pcbnew.F_Cu
        for a_, b_ in zip(st[1], st[1][1:]):
            charger._track(board, pt(*a_), pt(*b_), n, w, layer)
    for name, at in MCU_VIAS:
        v = pcbnew.PCB_VIA(board); v.SetPosition(pt(*at)); v.SetWidth(mm(0.6)); v.SetDrill(mm(0.3))
        v.SetNet(board.FindNet(name)); v.SetLocked(True)
        if name == "GND":
            v.SetBackTentingMode(pcbnew.TENTING_MODE_TENTED)
        board.Add(v)

def fix_charger_routes(board):
    """The shared charger pre-routes assume the 0402 C9: PMID now goes straight up from pin 29
    into the upright 0603, whose GND pad ties into the pin 27 GND line."""
    U = lambda n: charger._pad(board, "U1", n)
    p29 = U("29").GetPosition()
    c9 = charger._pad(board, "C9", "1").GetPosition()
    for t in [t for t in board.GetTracks() if t.GetClass() == "PCB_TRACK"]:
        if t.GetNetname() == "PMID" and p29 in (t.GetStart(), t.GetEnd()):
            board.Remove(t)
    charger._path(board, [p29, pcbnew.VECTOR2I(p29.x, c9.y)], U("29").GetNet(), 0.22)
    g9 = charger._pad(board, "C9", "2")
    charger._path(board, [g9.GetPosition(), pcbnew.VECTOR2I(U("27").GetPosition().x, g9.GetPosition().y)],
                  g9.GetNet(), 0.3)

def preroute(board):
    charger.preroute(board, CHARGER_FRAME)
    fix_charger_routes(board)
    # the input bus comes up on B.Cu: two vias into the VBUS copper for it to land on
    vbus = board.FindNet("VBUS")
    vv = [CHARGER_FRAME.pt(-5.0, 1.9), CHARGER_FRAME.pt(-4.2, 1.9)]
    charger._track(board, vv[0], vv[1], vbus, 0.8, pcbnew.B_Cu)
    for v in vv:
        charger._via(board, v, vbus)
    # C20 and C22 GND (small islands between tracks): own vias
    for ref in ("C20", "C22"):
        cg = _pad(board, ref, "2")
        # below the GND pad (C20 stands upright, C22 has R9 right of it)
        off = (0, 1.1)
        at = pcbnew.VECTOR2I(cg.GetPosition().x + mm(off[0]), cg.GetPosition().y + mm(off[1]))
        _track(board, cg.GetPosition(), at, cg.GetNet(), 0.4)
        v = pcbnew.PCB_VIA(board)
        v.SetPosition(at); v.SetWidth(mm(0.6)); v.SetDrill(mm(0.3)); v.SetNet(cg.GetNet())
        v.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu); v.SetLocked(True)
        board.Add(v)
    # SYS to the Xiao: one 0.8 mm run from the right end of the SYS copper (the bulk caps close
    # its top side) up past C15, west above the charger caps, up between the MSPM0 cluster and
    # the VBAT divider, west above the cluster's top vias and up to the drain of Q5
    sys_ = board.FindNet("SYS")
    vin = _pad(board, "Q5", "3").GetPosition()
    pts = [pt(32.6, 46.3), pt(32.6, 41.3), pt(28.0, 41.3), pt(28.0, 29.0), pcbnew.VECTOR2I(vin.x, pt(0, 29.0).y), vin]
    for a_, b_ in zip(pts, pts[1:]):
        _track(board, a_, b_, sys_, 0.8)
    # XIAO_5V from the Q5 source west under the Xiao (between its pin rows) and up to its 5V
    # pin (top-left); C20 at the left edge joins it from below, past the D0 pin
    v5 = board.FindNet("XIAO_5V")
    src, p5 = _pad(board, "Q5", "2").GetPosition(), _pad(board, "U3", "14").GetPosition()
    yv = src.y
    pts = [src, pcbnew.VECTOR2I(p5.x, yv), p5]
    for a_, b_ in zip(pts, pts[1:]):
        _track(board, a_, b_, v5, 0.6)
    c20 = _pad(board, "C20", "1").GetPosition()
    knee = pcbnew.VECTOR2I(c20.x, yv + (p5.x - c20.x))
    _track(board, c20, knee, v5, 0.6)
    _track(board, knee, pcbnew.VECTOR2I(p5.x, yv), v5, 0.6)
    # heater feed (up to ~2.5 A), all on F.Cu: 1.2 mm from the SYS copper along y 56.3 and down
    # the right edge to J6 pin 1; the return from the Q4 drain to J6 pin 2
    run = [pt(31.3, 47.6), pt(32.6, 46.3), pt(46.2, 46.3), pt(47.5, 47.6), _pad(board, "J6", "1").GetPosition()]
    for a_, b_ in zip(run, run[1:]):
        _track(board, a_, b_, sys_, 1.2)
    d, j62 = _pad(board, "Q4", "3").GetPosition(), _pad(board, "J6", "2").GetPosition()
    heat = board.FindNet("HEAT_D")
    knee = pcbnew.VECTOR2I(j62.x - mm(1.2), d.y)
    _track(board, d, knee, heat, 1.0)
    _track(board, knee, j62, heat, 1.0)
    # BATT+ (charge current up to 3 A): Q2 drain and JP3 joined on F.Cu, then 1.0 mm down to
    # J4 pin 1 (BAT +) on the bottom edge
    bat = board.FindNet("BATT+")
    v1, v2 = pt(34.0, 50.3), pt(34.0, 51.5)
    _track(board, _pad(board, "Q2", "3").GetPosition(), v1, bat, 1.0)
    _track(board, v1, v2, bat, 1.0)
    jp = _pad(board, "JP3", "2").GetPosition()
    _track(board, jp, pcbnew.VECTOR2I(jp.x, v2.y), bat, 1.0)
    _track(board, pcbnew.VECTOR2I(jp.x, v2.y), v2, bat, 1.0)
    j41 = _pad(board, "J4", "1").GetPosition()
    pts = [v2, pt(34.4, 51.9), pt(34.4, 57.0), pcbnew.VECTOR2I(j41.x + mm(1.6), j41.y - mm(1.6)), j41]
    for i, (a_, b_) in enumerate(zip(pts, pts[1:])):
        # 1.0 mm, but 0.8 on the diagonal past the TS via
        _track(board, a_, b_, bat, 0.8 if i == 2 else 1.0)
    # BATT_S (battery current): Q3 pin 3 (S2) straight down and right under the Kelvin column to
    # the RS1 BATT_S pad; R21 taps that pad. RS1 GND: 0.8 mm right to two vias at the edge.
    bs = board.FindNet("BATT_S")
    q3, r1 = _pad(board, "Q3", "3").GetPosition(), _pad(board, "RS1", "1").GetPosition()
    yb = q3.y + mm(1.7)
    _track(board, q3, pcbnew.VECTOR2I(q3.x, yb - mm(0.75)), bs, 0.5)
    pts = [pcbnew.VECTOR2I(q3.x, yb - mm(0.75)), pcbnew.VECTOR2I(q3.x + mm(0.75), yb), pcbnew.VECTOR2I(r1.x, yb), r1]
    for a_, b_ in zip(pts, pts[1:]):
        _track(board, a_, b_, bs, 0.8)
    _track(board, _pad(board, "R21", "1").GetPosition(), r1, bs, 0.25)
    gn = board.FindNet("GND")
    r2 = _pad(board, "RS1", "2").GetPosition()
    ex = mm(OX + W - 1.25)
    _track(board, r2, pcbnew.VECTOR2I(ex, r2.y), gn, 0.8)
    for dy in (-0.45, 0.45):
        at = pcbnew.VECTOR2I(ex, r2.y + mm(dy))
        _track(board, pcbnew.VECTOR2I(ex, r2.y), at, gn, 0.6)
        v = pcbnew.PCB_VIA(board); v.SetPosition(at); v.SetWidth(mm(0.6)); v.SetDrill(mm(0.3)); v.SetNet(gn); v.SetLocked(True)
        board.Add(v)
    # BATT- from J4 pin 2 on B.Cu along the bottom (under J4 / J3) and up to the HJ8205
    bm = board.FindNet("BATT-")
    j42 = _pad(board, "J4", "2").GetPosition()
    q1 = _pad(board, "Q3", "1").GetPosition()
    up = (pcbnew.ToMM(q1.x) - OX - 1.2, 45.0)   # 50 x 60 coordinates (pt stretches them)
    pts = [j42, pcbnew.VECTOR2I(j42.x, pt(0, 60.8).y), pt(up[0], 60.8), pt(*up)]   # 2.5 mm above the pins: B.Cu GND passes under J3
    for a_, b_ in zip(pts, pts[1:]):
        charger._track(board, a_, b_, bm, 1.0, pcbnew.B_Cu)
    for v in (up, (up[0], up[1] - 0.9)):
        charger._via(board, pt(*v), bm, 0.8, 0.4)
    charger._track(board, pt(up[0], up[1] - 0.9), pt(*up), bm, 1.0, pcbnew.B_Cu)
    _track(board, pt(up[0], up[1] - 0.9), q1, bm, 0.6)
    _track(board, pt(up[0], up[1] - 0.9), pt(*up), bm, 1.0)
    add_mcu_stubs(board)
    # SOLAR -> D1 -> VBUS (up to 22 V, 2 A): J1 pin 1 up to the D1 anode; from the cathode past
    # the TVS and to two vias, then on B.Cu up into the charger's VBUS landing vias
    sol = board.FindNet("SOLAR_IN")
    j11, a1 = _pad(board, "J1", "1").GetPosition(), _pad(board, "D1", "2").GetPosition()
    _track(board, j11, pcbnew.VECTOR2I(j11.x, a1.y + mm(1.4)), sol, 0.8)
    _track(board, pcbnew.VECTOR2I(j11.x, a1.y + mm(1.4)), a1, sol, 0.8)
    vb = board.FindNet("VBUS")
    k1, t1 = _pad(board, "D1", "1").GetPosition(), _pad(board, "D3", "1").GetPosition()
    _track(board, k1, t1, vb, 0.8)
    kv = [pt(12.4, 56.9), pt(13.2, 56.9)]
    _track(board, k1, kv[1], vb, 0.8)
    _track(board, kv[1], kv[0], vb, 0.8)
    for v in kv:
        charger._via(board, v, vb, 0.8, 0.4)
    land = CHARGER_FRAME.pt(-4.6, 1.9)
    for a_, b_ in ((kv[1], kv[0]), (kv[0], pcbnew.VECTOR2I(land.x, kv[0].y - mm(0.8))),
                   (pcbnew.VECTOR2I(land.x, kv[0].y - mm(0.8)), land)):
        charger._track(board, a_, b_, vb, 0.8, pcbnew.B_Cu)

def build():
    board = pcbnew.BOARD()
    ds = board.GetDesignSettings()
    ds.SetCopperLayerCount(2)
    ds.m_TrackMinWidth = mm(0.15)
    ds.m_ViasMinSize = mm(0.6)
    ds.m_MinThroughDrill = mm(0.3)
    ds.m_MinClearance = mm(0.127)
    ds.m_HoleClearance = mm(0.25)
    ds.m_CopperEdgeClearance = mm(0.4)   # V-cut panels: JLC wants copper >= 0.4 mm from the cut
    nc = ds.m_NetSettings.m_DefaultNetClass
    nc.SetTrackWidth(mm(0.25)); nc.SetClearance(mm(0.13)); nc.SetViaDiameter(mm(0.6)); nc.SetViaDrill(mm(0.3))
    nets = {}
    def net(name):
        if name not in nets:
            n = pcbnew.NETINFO_ITEM(board, name)
            board.Add(n)
            nets[name] = n
        return nets[name]
    missing = []
    tps = {}
    for p in gen_sch.parts:
        ref = p["ref"]
        if ref.startswith("#"):
            continue
        if ref not in PLACE and not ref.startswith("TP"):
            missing.append(ref)
            continue
        fp = load_fp(p["fp"])
        fp.SetReference(ref)
        fp.SetValue(p["value"])
        fp.SetFPIDAsString(p["fp"])
        x, y, rot = PLACE.get(ref, (0, 0, 0))
        fp.SetPosition(pt(x, y))
        fp.SetOrientationDegrees(rot)
        if p["lcsc"]:
            fp.SetField("LCSC", p["lcsc"]) if hasattr(fp, "SetField") else None
        # footprints imported with easyeda2kicad carry the LCSC code of the part they came
        # from (Q3 uses the DW01A one): always the code of this part
        if fp.HasProperty("LCSC Part"):
            fp.SetProperty("LCSC Part", p["lcsc"] or "")
        if not p["lcsc"]:
            fp.SetAttributes(fp.GetAttributes() | pcbnew.FP_EXCLUDE_FROM_POS_FILES)
        for pad in fp.Pads():
            n = p["nets"].get(pad.GetNumber())
            if n:
                pad.SetNet(net(n))
        fp.Reference().SetTextSize(pcbnew.VECTOR2I(mm(0.8), mm(0.8)))
        fp.Reference().SetTextThickness(mm(0.12))
        fp.Value().SetVisible(False)
        for f in fp.GetFields():
            if f.GetName() != "Reference":
                f.SetVisible(False)
        if ref.startswith("H"):
            fp.Reference().SetVisible(False)
        if ref in B_SIDE:
            fp.Reference().SetVisible(False)   # the B.Cu silk label names them
        if ref == "RS1":   # shunt GND end carries the battery current: solid to the pour
            for pad in fp.Pads():
                if pad.GetNumber() == "2":
                    pad.SetLocalZoneConnection(pcbnew.ZONE_CONNECTION_FULL)
        if ref.startswith("TP") and ref not in PLACE:
            fp.Reference().SetVisible(False)
            tps[ref] = (fp, p["nets"]["1"])
            continue
        board.Add(fp)
    if missing:
        raise SystemExit("not placed: " + " ".join(missing))
    preroute(board)
    charger.add_zones(board, CHARGER_FRAME)
    charger.pin1_dot(board, CHARGER_FRAME)
    corners = OUTLINE
    for i in range(len(corners)):
        s = pcbnew.PCB_SHAPE(board)
        s.SetShape(pcbnew.SHAPE_T_SEGMENT)
        s.SetStart(pt(*corners[i])); s.SetEnd(pt(*corners[(i + 1) % len(corners)]))
        s.SetLayer(pcbnew.Edge_Cuts); s.SetWidth(mm(0.1))
        board.Add(s)
    # GND pour on both layers
    for layer in (pcbnew.F_Cu, pcbnew.B_Cu):
        z = pcbnew.ZONE(board)
        z.SetLayer(layer)
        z.SetNet(net("GND"))
        z.SetLocalClearance(mm(0.2))
        z.SetMinThickness(mm(0.2))
        z.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL)
        z.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_ALWAYS)
        z.SetThermalReliefGap(mm(0.3)); z.SetThermalReliefSpokeWidth(mm(0.4))
        ol = z.Outline()
        ol.NewOutline()
        for (x, y) in [(0.3, TOP + 0.3), (W - 0.3, TOP + 0.3), (W - 0.3, H - 0.3), (0.3, H - 0.3)]:
            ol.Append(mm(OX + x), mm(OY + y))
        board.Add(z)
    place_test_points(board, tps, CHARGER_FRAME, TEST_POINTS)
    for ref in sorted(B_SIDE):
        fp = board.FindFootprintByReference(ref)
        fp.Flip(fp.GetPosition(), pcbnew.FLIP_DIRECTION_LEFT_RIGHT)
    dfm.add_fiducials(board, FIDUCIALS, load_fp, OX, OY)
    # "+" at pin 1 of the power connectors: left of it on the bottom row, above it for J6
    plus_marks(board, ["J1", "J4"], dx=0.0, dy=-1.6)
    plus_marks(board, ["J6"], dx=-2.2, dy=0.0)
    # silkscreen labels: the right column (J5, J6) left of the pins, read upwards; the bottom
    # row above its two pins
    def label(txt, at, angle):
        t = pcbnew.PCB_TEXT(board)
        t.SetText(txt); t.SetPosition(at); t.SetLayer(pcbnew.F_SilkS)
        t.SetTextSize(pcbnew.VECTOR2I(mm(0.8), mm(0.8))); t.SetTextThickness(mm(0.12))
        t.SetTextAngleDegrees(angle)
        board.Add(t)
    x, y, _ = PLACE["J5"]
    label("SCL SDA 3V3 GND", pt(x + 1.27 * 3, y + 2.2), 0)
    x, y, _ = PLACE["J6"]
    label("HEAT", pt(x - 0.3, y + 4.6), 0)
    for ref, txt in (("J1", "SOLAR"), ("J4", "BAT"), ("J3", "NTC")):
        p1, p2 = _pad(board, ref, "1").GetPosition(), _pad(board, ref, "2").GetPosition()
        if ref == "J1":  # D1 sits right above J1: label to the right of the connector instead
            label(txt, pcbnew.VECTOR2I(max(p1.x, p2.x) + mm(5.6), p1.y), 0)
        else:
            label(txt, pcbnew.VECTOR2I((p1.x + p2.x) // 2, p1.y - mm(2.0)), 0)
    t = pcbnew.PCB_TEXT(board)
    t.SetText("xiao-solar v0.5\nn3tk0/MeshCore")
    t.SetPosition(pt(40.0, 48.8)); t.SetLayer(pcbnew.F_SilkS)
    t.SetTextSize(pcbnew.VECTOR2I(mm(0.8), mm(0.8))); t.SetTextThickness(mm(0.12))
    board.Add(t)
    # SWD row on the bottom (seen from below, right to left: 3V3M NRST SWDIO SWCLK GND): a
    # mirrored label under each pad on B.SilkS, "SWD" left of the row
    for txt, ref in (("SWD", None), ("3V3", "TP12"), ("RST", "TP15"), ("DIO", "TP11"), ("CLK", "TP14"),
                     ("GND", "TP13")):
        x, y, _ = PLACE[ref] if ref else (PLACE["TP12"][0] - 2.6, PLACE["TP12"][1], 0)
        t = pcbnew.PCB_TEXT(board)
        t.SetText(txt)
        t.SetPosition(pt(x, y + 1.75) if ref else pt(x, y)); t.SetLayer(pcbnew.B_SilkS); t.SetMirrored(True)
        t.SetTextSize(pcbnew.VECTOR2I(mm(0.8), mm(0.8))); t.SetTextThickness(mm(0.12))
        board.Add(t)
    # JP2 (CHEM: closed = LiFePO4) is named by function instead of its reference
    x, y, _ = PLACE["JP2"]
    board.FindFootprintByReference("JP2").Reference().SetVisible(False)
    label("LFP", pt(x + 2.1, y), 0)
    # heater LED references left of the parts (the hole and J6 are right of them)
    for ref in ("D6", "R29"):
        r = board.FindFootprintByReference(ref).Reference()
        r.SetPosition(pt(38.0, PLACE[ref][1])); r.SetTextAngleDegrees(0)
    hidden = dfm.place_silk(board)
    print("silk: hidden refs", " ".join(hidden) or "none")
    return board

def write_project():
    """Net classes live in the .kicad_pro in KiCad 7."""
    import json
    def cls(name, tw, cl, vd, vdr):
        return {"name": name, "track_width": tw, "clearance": cl, "via_diameter": vd, "via_drill": vdr,
                "microvia_diameter": 0.3, "microvia_drill": 0.1, "diff_pair_width": 0.2,
                "diff_pair_gap": 0.25, "diff_pair_via_gap": 0.25, "wire_width": 6, "bus_width": 12,
                "line_style": 0, "pcb_color": "rgba(0, 0, 0, 0.000)", "schematic_color": "rgba(0, 0, 0, 0.000)"}
    classes = [cls("Default", 0.25, 0.15, 0.6, 0.3)]
    patterns = []
    for name, (tw, cl, vd, vdr, nets) in NETCLASSES.items():
        classes.append(cls(name, tw, cl, vd, vdr))
        patterns += [{"netclass": name, "pattern": n} for n in nets]
    pro = {"meta": {"filename": "xiao-solar.kicad_pro", "version": 1},
           "board": {"design_settings": {"rules": {"min_clearance": 0.15, "min_track_width": 0.15,
                                                     "min_via_diameter": 0.6, "min_through_hole_diameter": 0.3,
                                                     "min_copper_edge_clearance": 0.3, "min_hole_clearance": 0.25}}},
           "net_settings": {"classes": classes, "meta": {"version": 3}, "netclass_patterns": patterns},
           "schematic": {}, "sheets": [[gen_sch.ROOT_UUID, ""]]}
    open(os.path.join(ROOT, "xiao-solar.kicad_pro"), "w").write(json.dumps(pro, indent=2) + "\n")

# the far ends of the MSPM0 analog signals go in before Freerouting as locked tracks (tools/patch2.py,
# two-layer grid A*), as on solar-smart-mppt: the autorouter leaves them open or long.
# (net, from, to): a (ref, pad) or a point in board mm (a stub end or via of MCU_STUBS)
PRE_ROUTES = [
    # Xiao bus from the MSPM0 stubs to the Xiao pins (first: SDA climbs the channel to the top row)
    ("SDA", _m(26.1, 49.05), ("U3", "8")), ("SCL", _m(26.95, 50.0), ("U3", "7")),
    # SPI and 3V3, Xiao top row -> Wio bottom row, on B.Cu: over the Xiao pins, down the channel
    # and in under the Wio pins from below (the F.Cu of the channel stays for the ideal diode)
    ("SPI_SCK", ("U3", "9"), ("U4", "9")), ("SPI_MISO", ("U3", "10"), ("U4", "10")),
    ("SPI_MOSI", ("U3", "11"), ("U4", "11")), ("3V3", ("U3", "12"), ("U4", "12")),
    # ideal-diode mirror (inside the channel, F.Cu): thin sense lines, the power path is Q5
    ("SYS", ("Q6", "2"), (26.5, 29.0)), ("XIAO_5V", ("Q7", "2"), ("Q5", "2")),
    ("OR_G", ("Q7", "3"), ("Q5", "1")),
    # LoRa control links Xiao bottom row -> Wio top row, innermost first so the runs nest: under the
    # Xiao, out over the top and down to the Wio pins (Petko: no link under the Wio; B.Cu there is
    # a keepout for Freerouting too, which routes SPI / 3V3 through the channel)
    ("LORA_RXEN", ("U3", "6"), ("U4", "6")), ("LORA_NSS", ("U3", "5"), ("U4", "5")),
    ("LORA_BUSY", ("U3", "4"), ("U4", "4")), ("LORA_RST", ("U3", "3"), ("U4", "3")),
    ("LORA_DIO1", ("U3", "2"), ("U4", "2")),
    # SWD on B.Cu to the pads of the row (the farthest pad first, so the runs nest)
    ("SWCLK", _m(30.4, 44.85), ("TP14", "1")), ("SWDIO", _m(31.2, 45.85), ("TP11", "1")),
    ("NRST", _m(24.85, 42.95), ("TP15", "1")), ("3V3M", _m(30.13, 49.9), ("TP12", "1")),
    # analog from the cluster vias to the shunt, divider, CHEM jumper and heater gate
    ("ADC_VBAT", _m(30.2, 42.6), ("R25", "2")), ("ADC_CHEM", _m(31.0, 43.2), ("JP2", "1")),
    ("SH_P", _m(31.0, 41.9), ("R23", "2")), ("SH_N", _m(31.6, 46.85), ("R24", "2")),
    ("HEAT", _m(30.2, 41.2), ("R27", "1")),
    # 3V3M from the HT7533 output to the R13 via and the SWD pad
    ("3V3M", ("C26", "1"), _m(23.07, 42.4)),
    # NTC from J3 to the TS node at R2 / R3
    ("TS", ("J3", "1"), (21.58, 54.8)),
    # heater LED: D6 anode to R29, cathode to the J6 return, R29 to SYS at J6 pin 1
    ("LED_HEAT", ("D6", "2"), ("R29", "2")), ("HEAT_D", ("D6", "1"), ("J6", "2")),
    ("SYS", ("R29", "1"), ("J6", "1")),
]

def pre_route(b):
    import patch2, route
    KB = [(x0 - OX, y0 - OY, x1 - OX, y1 - OY) for x0, y0, x1, y1 in route.b_clear(b)]
    def end(e):
        if isinstance(e[0], float):
            return (e[0], ys(e[1]))
        p = _pad(b, *e).GetPosition()
        return (pcbnew.ToMM(p.x) - OX, pcbnew.ToMM(p.y) - OY)
    for net_, a_, b_ in PRE_ROUTES:
        try:
            # nothing under the Wio; the LoRa control links may cross under the Xiao (vias above the
            # XIAO_5V run, over to F.Cu and out through the channel), nothing else goes there
            ko = [WIO_IN] if net_.startswith("LORA_") else [WIO_IN, XIAO_IN]
            print(patch2.route_net(b, net_, (end(a_), end(b_)), lock=True, clear=0.2,
                                   width=0.4 if net_ == "3V3" else 0.2,   # 3V3: the Wio's supply
                                   layer_cost=(6.0, 1.0) if net_ in SPI_B else (1.0, 1.0),
                                   keepout_b=KB, keepout=ko))
        except RuntimeError as e:
            print(net_, "pre-route failed:", e)

if __name__ == "__main__":
    write_project()
    b = build()
    out = os.path.join(ROOT, "xiao-solar.kicad_pcb")
    b.Save(out)
    b = pcbnew.LoadBoard(out)
    pre_route(b)
    b.Save(out)
    print("wrote", out)
