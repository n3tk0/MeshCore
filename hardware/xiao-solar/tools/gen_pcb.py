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
W, H = 48.0, 71.5          # board size, mm
OX, OY = 100.0, 100.0      # board origin on the sheet

def mm(v):
    return pcbnew.FromMM(v)

def pt(x, y):
    return pcbnew.VECTOR2I(mm(OX + x), mm(OY + y))

# ref: (x, y, rotation_deg). Coordinates are mm from the board's top-left corner.
# Columns: XIAO/Wio on the left (USB to the left edge), MSPM0 between U3 and the right column,
# inputs, BMS, shunt and heater FET on the right, charger below the modules, all connectors
# in one column at the right edge.
PLACE = {
    # modules, rotated so the USB / pin-1 end faces the left edge
    "U4": (11.5, 12.0, 90), "U3": (11.5, 35.0, 90),
    # between the modules: Xiao 5V feed and 3V3 decoupling
    "D5": (5.5, 23.5, 180), "C20": (9.8, 23.5, 0), "C21": (13.9, 23.5, 180), "C22": (17.1, 23.5, 0),
    # right column connectors (pin 1 at the given point, pins go down)
    "J5": (45.5, 3.5, 0), "J3": (45.5, 15.0, 0), "J1": (45.5, 21.5, 0), "J2": (45.5, 28.0, 0),
    "J4": (45.5, 37.0, 0),
    # I2C / sensor, top right (away from the charger heat)
    # SHT40 sits on a tongue cut free by slots (SENSOR_TONGUE)
    "U5": (26.0, 2.6, 0), "C23": (28.6, 2.6, 90), "R11": (33.0, 2.4, 0), "R12": (33.0, 4.4, 0),
    # inputs beside J1/J2, TVS where the two cathodes meet
    "D1": (38.5, 22.8, 0), "D2": (38.5, 29.3, 0), "D3": (31.5, 26.0, 90),
    # battery / BMS (right of the charger, next to J4)
    "R9": (35.5, 32.5, 0), "C19": (35.5, 35.0, 90),
    "U2": (39.0, 34.5, 0), "Q3": (39.5, 39.5, 0), "R10": (35.5, 38.5, 0),
    # JP3 bridges the ship FET Q2 (closed = no ship FET, for LiFePO4)
    "JP3": (32.8, 58.0, 90),
}
# charger cluster (tools/charger.py), U1 at its position before the DY shift below
PLACE.update(charger.place(charger.Frame(16.0, 55.0), set(charger.PLACE)))
# Everything except the SHT40 tongue moves down by DY to leave the top corners free for
# the mounting holes: 2.8 mm (M2/M2.5) with 6 mm clear for the head, 3.5 mm from the edges.
DY = 6.0
CHARGER_FRAME = charger.Frame(16.0, 55.0 + DY)
PLACE = {r: (x, y if r in ("U5", "C23") else y + DY, a) for r, (x, y, a) in PLACE.items()}
# v0.5 (final board coordinates, after DY)
PLACE.update({
    # shunt RS1 (20 mOhm) under Q3: Q3 S2 (BATT_S) -> RS1 -> GND. Kelvin taps R21 (BATT_S)
    # and R22 (SH_K, net tie NT1 at the RS1 GND pad), C24 across them, bias R23 / R24
    "RS1": (41.2, 49.6, 270), "R21": (38.8, 48.0, 180), "NT1": (39.83, 51.06, 0),
    "R22": (37.8, 51.06, 180), "C24": (37.8, 49.3, 0), "R23": (35.5, 48.5, 0), "R24": (35.5, 49.7, 0),
    # battery voltage divider for the MSPM0 ADC
    "R25": (35.5, 46.3, 180), "C30": (35.5, 47.4, 0), "R26": (35.3, 51.25, 0),
    # heater: J6 under J4 on the edge, fed by a 1.2 mm SYS run at y 53 (pre-route);
    # Q4 under that run with its drain next to J6 pin 2, gate parts to its left
    "J6": (45.5, 53.0, 0), "Q4": (41.6, 56.3, 0), "R27": (37.6, 55.35, 0), "R28": (37.6, 57.1, 0),
    # MSPM0 between U3 and the SYS trunk (x 32.6): I2C pins face down (pull-ups below),
    # VDD / NRST face U3 (their caps in the strip beside it), the analog pins and HEAT
    # escape up and the SWD / shunt pins right, all through the locked stubs in MCU_STUBS
    "U6": (27.2, 45.6, 0),
    "C27": (23.55, 45.35, 180), "C29": (23.55, 44.3, 180), "R13": (23.55, 43.3, 0),
    "C28": (24.9, 41.9, 180),
    "R14": (29.62, 48.95, 180), "R15": (29.62, 50.9, 180), "R4": (31.3, 48.75, 90),
    # HT7533 in the corner under the SYS diagonal: C25 takes SYS straight down from the
    # trunk corner to the tab, C26 at the output
    "U8": (25.0, 38.0, 0), "C25": (25.0, 34.6, 180), "C26": (28.3, 38.6, 270),
    # CHEM jumper and the SWD column on the bottom side (B_SIDE): the column (2.54 mm) sits
    # under the Xiao's right edge, its lines run left under U6 on B.Cu
    "JP2": (34.6, 41.0, 270),
    "TP12": (21.3, 38.8, 0), "TP15": (21.3, 41.34, 0), "TP11": (21.3, 43.88, 0),
    "TP14": (21.3, 46.42, 0), "TP13": (21.3, 48.96, 0),
})
B_SIDE = {"TP11", "TP12", "TP13", "TP14", "TP15", "JP2"}
PLACE.update({"H1": (3.5, 3.5, 0), "H2": (W - 3.5, 3.5, 0), "H3": (3.5, H - 3.5, 0), "H4": (W - 3.5, H - 3.5, 0)})

# SHT40 tongue: x0, y0, x1, y1 (mm). 1 mm slots on the left, right and below it,
# with a 3.4 mm neck in the middle of the bottom slot for SDA/SCL/3V3/GND.
SENSOR_TONGUE = (23.0, 0.0, 30.0, 6.0)
NECK = (24.8, 28.2)
_tx0, _, _tx1, _ty1 = SENSOR_TONGUE
OUTLINE = [(0, 0), (_tx0 - 1, 0), (_tx0 - 1, _ty1 + 1), (NECK[0], _ty1 + 1), (NECK[0], _ty1),
           (_tx0, _ty1), (_tx0, 0), (_tx1, 0), (_tx1, _ty1), (NECK[1], _ty1), (NECK[1], _ty1 + 1),
           (_tx1 + 1, _ty1 + 1), (_tx1 + 1, 0), (W, 0), (W, H), (0, H)]

# Bring-up test points: net -> (region x0, y0, x1, y1, preferred x, y). Charger nets sit
# inside their copper zone when there is room; the others are wired by the router.
TEST_POINTS = {
    "VBUS": (9.0, 58.0, 15.0, 64.0, 10.0, 61.5), "PMID": (4.5, 54.0, 12.0, 59.0, 6.0, 58.0),
    "SYS": (20.0, 54.0, 32.0, 59.0, 30.5, 55.5), "BAT": (22.0, 58.5, 34.0, 66.0, 29.5, 65.6),
    "SCL": (35.0, 6.0, 43.0, 14.0, 37.5, 7.5), "SDA": (35.0, 6.0, 43.0, 14.0, 39.5, 7.5),
    "3V3": (35.0, 6.0, 43.0, 14.0, 37.5, 10.0), "GND": (35.0, 6.0, 43.0, 14.0, 39.5, 10.0),
}
# three fiducials: region x0, y0, x1, y1 and the preferred point
FIDUCIALS = [(6.5, 1.0, 21.0, 6.0, 8.0, 2.5), (32.0, 1.0, 41.0, 6.0, 40.0, 2.5), (6.5, H - 6.0, 26.0, H - 1.0, 8.0, H - 2.0)]
# B.Cu stays whole GND under the Wio (U4) as well as under the charger (charger.B_CLEAR)
# (between its two pin rows only: the Xiao-Wio links need B.Cu at the rows)
B_CLEAR = [(0.7, 11.8, 22.3, 23.8)]
# the charger's whole-GND B.Cu area starts 1.4 mm lower than charger.B_CLEAR (still above the
# bulk caps): the strip above it carries the BQ25798 bus from the right edge to the MSPM0
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
        fp.SetPosition(pt(*at))
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
    "Power": (0.6, 0.2, 0.8, 0.4, ["BATT+", "BATT-", "BATT_S", "FS_D", "SOLAR_IN", "VIN2_IN", "XIAO_5V",
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
# or to a via, board mm. Top pins nest upwards (the leftmost goes highest) to vias left of the
# trunk, where they cross it on B.Cu; SWD / SH_N right to vias; I2C down to the pull-ups.
MCU_STUBS = [
    ("NRST", [(25.21, 44.85), (24.45, 44.85), (24.03, 44.43)]),
    ("NRST", [(24.03, 43.3), (24.03, 44.3)]),
    ("NRST", [(24.06, 43.3), (24.85, 42.95)]),
    # NRST to its SWD pad (TP15) on B.Cu, below the 3V3M via of R13
    ("NRST", [(24.85, 42.95), (22.4, 42.95), (21.3, 41.85), (21.3, 41.34)], 0.2, "B"),
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
    ("3V3M", [(34.99, 48.5), (34.99, 49.7)], 0.25),
    # BQ25798 bus down the B.Cu lane beside the trunk (SCL_I x 32.0, SDA_I 32.5, CE 33.0) and
    # back left under the charger (below its whole-GND area and the BATP run). SCL_I hops over
    # SDA_I on F.Cu at x 25 so the order matches the U1 stubs (SDA_I, SCL_I, CE from the top).
    ("SCL_I", [(29.11, 51.7), (32.0, 51.7), (32.0, 62.95), (25.2, 62.95)], 0.2, "B"),
    ("SCL_I", [(25.2, 62.95), (24.3, 64.2)]),
    ("SCL_I", [(24.3, 64.2), (20.4, 64.2), (18.95, 65.65), (18.95, 65.75)], 0.2, "B"),
    ("SCL_I", [(18.95, 65.75), (18.2, 65.6)]),
    ("SDA_I", [(29.11, 49.9), (29.81, 50.6), (32.5, 50.6), (32.5, 63.6), (19.75, 63.6), (18.95, 64.4),
               (18.95, 64.8)], 0.2, "B"),
    ("SDA_I", [(18.95, 64.8), (18.2, 64.8)]),
    ("CE", [(30.4, 48.1), (31.9, 49.6), (33.0, 49.6), (33.0, 64.8), (21.2, 64.8), (18.0, 68.0),
            (16.2, 67.98)], 0.2, "B"),
    # the charger's right corner: REGN over TS to R2, BATP on B.Cu above the bus to R8 (as v0.4)
    ("REGN", [(17.9, 62.18), (18.7, 62.18), (19.0, 62.48), (22.6, 62.48), (23.22, 63.1), (23.22, 64.8)], 0.2),
    ("TS", [(17.8, 62.58), (18.3, 63.08), (20.1, 63.08), (21.58, 64.56), (21.58, 64.8)], 0.2),
    ("BATP", [(17.92, 61.78), (18.62, 61.78), (18.9, 61.56)]),
    ("BATP", [(18.9, 61.56), (19.65, 62.25), (28.6, 62.25), (28.85, 62.0)], 0.2, "B"),
    ("BATP", [(28.85, 62.0), (28.78, 63.0)]),
    # BAT from the charger's BAT copper down to JP3 (bypass of the ship FET), as v0.4
    ("BAT", [(26.25, 60.64), (25.6, 61.3), (25.6, 61.78), (28.47, 64.65), (32.8, 64.65)], 0.6),
    # the pull-up group gets 3V3M from the C27 via on B.Cu, under the bottom pins of U6
    ("3V3M", [(30.13, 49.9), (29.7, 49.47), (29.7, 48.6), (29.3, 48.2), (24.6, 48.2), (24.1, 47.7),
              (24.1, 46.3)], 0.25, "B"),
    # 3V3M to the shunt bias resistors: a via left of them, then B.Cu between the CE lane and
    # the SH_N via into the pull-up feed (the F.Cu trunk walls the column off on the top side)
    ("3V3M", [(34.99, 49.1), (34.45, 49.1)], 0.25),
    ("3V3M", [(34.45, 49.1), (32.3, 49.1), (31.0, 47.45), (29.6, 47.45), (29.3, 47.75), (29.3, 48.2)], 0.25, "B"),
    # GND ties the pour cannot reach: C29 + C27 (MSPM0 VDD caps) and C30 (VBAT filter) to vias
    ("GND", [(23.07, 44.3), (23.07, 45.35), (23.4, 46.6)], 0.3),
    ("GND", [(35.98, 47.4), (36.75, 47.4)], 0.3),
    # VBAT divider top from R9 (BATT+) down the gap left of C19 / R10, then over R25
    ("BATT+", [(34.68, 38.5), (33.95, 39.23), (33.95, 45.2), (34.15, 45.4), (35.7, 45.4), (36.01, 45.71),
               (36.01, 46.3)], 0.25),
    # VBAT divider: R25 - C30 - R26 down the F.Cu gap between the trunk and the bias column
    ("ADC_VBAT", [(34.99, 46.3), (35.02, 47.4)]),
    ("ADC_VBAT", [(35.02, 47.4), (34.3, 47.4), (33.8, 47.9), (33.8, 50.6), (34.47, 51.25)]),
    ("SDA", [(26.95, 47.59), (26.95, 48.2), (26.1, 49.05)]),
    ("SCL", [(27.45, 47.59), (27.45, 48.6), (26.95, 49.1), (26.95, 50.0)]),
    # HT7533: SYS from the trunk to C25 and the tab, 3V3M and GND to C26
    ("SYS", [(25.95, 34.6), (26.6, 33.8)], 0.8),
    ("SYS", [(25.95, 34.6), (24.5, 36.05), (23.9, 37.4)], 0.5),
    ("3V3M", [(26.36, 36.5), (27.2, 36.5), (28.3, 37.6)], 0.3),
    ("GND", [(26.36, 39.5), (28.3, 39.55)], 0.3),
]
MCU_VIAS = [("3V3M", (24.1, 46.3)), ("3V3M", (23.07, 42.4)), ("HEAT", (30.2, 41.2)), ("SH_P", (31.0, 41.9)),
            ("ADC_VBAT", (30.2, 42.6)), ("ADC_CHEM", (31.0, 43.2)), ("SWCLK", (30.4, 44.85)),
            ("SWDIO", (31.2, 45.85)), ("SH_N", (31.6, 46.85)),
            ("CE", (30.4, 48.1)), ("SDA_I", (29.11, 49.9)), ("SCL_I", (29.11, 51.7)),
            ("SCL_I", (25.2, 62.95)), ("SCL_I", (24.3, 64.2)), ("SCL_I", (18.95, 65.75)),
            ("SDA_I", (18.95, 64.8)), ("CE", (16.2, 67.98)), ("BATP", (18.9, 61.56)), ("BATP", (28.85, 62.0)),
            ("NRST", (24.85, 42.95)), ("3V3M", (30.13, 49.9)), ("3V3M", (34.45, 49.1)), ("GND", (23.4, 46.6)), ("GND", (36.75, 47.4)),
            # 4 GND vias in the exposed pad, tented from below
            ("GND", (26.7, 45.1)), ("GND", (27.7, 45.1)), ("GND", (26.7, 46.1)), ("GND", (27.7, 46.1))]

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

def preroute(board):
    charger.preroute(board, CHARGER_FRAME)
    # the input bus comes up on B.Cu: two vias into the VBUS copper for it to land on
    vbus = board.FindNet("VBUS")
    vv = [CHARGER_FRAME.pt(-5.0, 1.9), CHARGER_FRAME.pt(-4.2, 1.9)]
    charger._track(board, vv[0], vv[1], vbus, 0.8, pcbnew.B_Cu)
    for v in vv:
        charger._via(board, v, vbus)
    # SHT40 tongue: the rule area keeps pour and vias off it, so its four nets are
    # drawn here and run out through the neck (SDA, SCL, 3V3, GND from left to right).
    def path(net, pts):
        n = board.FindNet(net)
        for a, b in zip(pts, pts[1:]):
            _track(board, pt(*a), pt(*b), n, 0.25)
    path("SDA", [(25.32, 2.2), (24.4, 2.2), (24.4, 4.8), (25.4, 5.8), (25.4, 7.8)])
    path("SCL", [(25.32, 3.0), (25.0, 3.0), (25.0, 4.6), (26.0, 5.6), (26.0, 7.8)])
    path("3V3", [(26.68, 3.0), (26.7, 4.2), (26.7, 7.8)])
    path("3V3", [(28.6, 3.375), (28.6, 4.2), (26.7, 4.2)])
    path("GND", [(26.0, 2.2), (26.68, 2.2), (27.3, 1.825), (28.6, 1.825), (29.4, 1.825),
                 (29.4, 5.0), (27.5, 5.0), (27.5, 7.8)])
    # C20 and C22 GND between the module rows get cut off by the traces there: own vias
    for ref in ("C20", "C22"):
        cg = _pad(board, ref, "2")
        at = pcbnew.VECTOR2I(cg.GetPosition().x + mm(1.1), cg.GetPosition().y)
        _track(board, cg.GetPosition(), at, cg.GetNet(), 0.4)
        v = pcbnew.PCB_VIA(board)
        v.SetPosition(at); v.SetWidth(mm(0.6)); v.SetDrill(mm(0.3)); v.SetNet(cg.GetNet())
        v.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu); v.SetLocked(True)
        board.Add(v)
    # SYS to the Xiao: one 0.8 mm run from the right end of the SYS copper (the bulk caps
    # close its top side) up past RS1, across to the channel between the modules and the
    # right column and along it to the LM66100 input
    sys_ = board.FindNet("SYS")
    vin = _pad(board, "D5", "1").GetPosition()
    # v0.5: the diagonal dips to B.Cu between (28.9, 36.1) and (31.9, 39.1), two vias at each
    # end, so 3V3M can cross it on F.Cu to the shunt bias resistors (the B.Cu lines of the
    # MSPM0 already cross the vertical part below)
    pts = [pt(32.6, 53.0), pt(32.6, 39.8), pt(31.9, 39.1)]
    for a_, b_ in zip(pts, pts[1:]):
        _track(board, a_, b_, sys_, 0.8)
    pts = [pt(28.9, 36.1), pt(23.8, 31.0), pcbnew.VECTOR2I(vin.x, mm(OY + 31.0))]
    for a_, b_ in zip(pts, pts[1:]):
        _track(board, a_, b_, sys_, 0.8)
    charger._track(board, pt(28.2, 35.4), pt(32.6, 39.8), sys_, 0.8, pcbnew.B_Cu)
    for v in ((28.9, 36.1), (28.2, 35.4), (31.9, 39.1), (32.6, 39.8)):
        charger._via(board, pt(*v), sys_, 0.8, 0.4)
    _track(board, pts[-1], vin, sys_, 0.3)  # the SC-70 pins are 0.65 mm apart
    # heater feed (up to ~2.5 A), all on F.Cu: 1.2 mm from the SYS copper down the trunk and
    # along y 53 to J6 pin 1; the return from the Q4 drain to J6 pin 2
    run = [pt(31.3, 57.6), pt(32.6, 56.3), pt(32.6, 53.0), _pad(board, "J6", "1").GetPosition()]
    for a_, b_ in zip(run, run[1:]):
        _track(board, a_, b_, sys_, 1.2)
    d, j62 = _pad(board, "Q4", "3").GetPosition(), _pad(board, "J6", "2").GetPosition()
    heat = board.FindNet("HEAT_D")
    knee = pcbnew.VECTOR2I(j62.x - mm(1.2), d.y)
    _track(board, d, knee, heat, 1.0)
    _track(board, knee, j62, heat, 1.0)
    # BATT+ (charge current up to 3 A) crosses under all of it: Q2 drain and JP3 to two vias,
    # then 1.0 mm on B.Cu straight to J4 pin 1 (through-hole)
    bat = board.FindNet("BATT+")
    v1, v2 = pt(34.0, 60.3), pt(34.0, 61.5)
    for v in (v1, v2):
        charger._via(board, v, bat)
    _track(board, _pad(board, "Q2", "3").GetPosition(), v1, bat, 0.8)
    _track(board, v1, v2, bat, 0.8)
    jp = _pad(board, "JP3", "2").GetPosition()
    _track(board, jp, pcbnew.VECTOR2I(jp.x, v2.y), bat, 0.8)
    _track(board, pcbnew.VECTOR2I(jp.x, v2.y), v2, bat, 0.8)
    j4 = _pad(board, "J4", "1").GetPosition()
    for a_, b_ in ((v1, pt(42.6, 48.0)), (pt(42.6, 48.0), pt(42.6, 44.3)), (pt(42.6, 44.3), j4)):
        charger._track(board, a_, b_, bat, 1.0, pcbnew.B_Cu)
    charger._track(board, v1, v2, bat, 1.0, pcbnew.B_Cu)
    add_mcu_stubs(board)
    # VBUS (solar / VIN2 after the SS54s, up to 26 V, 2 A) from the diodes to the charger:
    # down the right edge (B.Cu under the heater runs), diagonally to the bottom edge and along
    # it to the charger's VBUS vias, as in v0.4; the MSPM0 bus lane leaves no way across inside
    vb = board.FindNet("VBUS")
    def run(pts_, layer=pcbnew.F_Cu, w=0.6):
        for a_, b_ in zip(pts_, pts_[1:]):
            charger._track(board, pt(*a_), pt(*b_), vb, w, layer)
    run([(36.5, 28.8), (36.5, 35.3)])
    run([(36.5, 35.3), (34.0, 35.3), (33.1, 34.4), (31.5, 34.4)])
    run([(36.5, 35.3), (36.5, 36.9), (37.1, 37.5), (43.0, 37.5), (43.6, 38.1), (43.6, 51.5)])
    run([(43.6, 50.6), (43.6, 58.6)], pcbnew.B_Cu)
    run([(43.6, 57.7), (43.6, 58.6), (31.5, 70.7), (11.6, 70.7), (9.8, 68.9), (9.8, 65.0)])
    run([(9.8, 65.9), (9.8, 65.0), (11.4, 63.4), (11.4, 62.9)], pcbnew.B_Cu)
    for v in ((43.6, 50.6), (43.6, 51.5), (43.6, 57.7), (43.6, 58.6), (9.8, 65.0), (9.8, 65.9)):
        charger._via(board, pt(*v), vb, 0.8, 0.4)

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
    # outline; the 1 mm slots around the SHT40 tongue are part of it
    corners = OUTLINE
    for i in range(len(corners)):
        s = pcbnew.PCB_SHAPE(board)
        s.SetShape(pcbnew.SHAPE_T_SEGMENT)
        s.SetStart(pt(*corners[i])); s.SetEnd(pt(*corners[(i + 1) % len(corners)]))
        s.SetLayer(pcbnew.Edge_Cuts); s.SetWidth(mm(0.1))
        board.Add(s)
    # no copper pour on the tongue: the pour would carry heat from the rest of the board
    tx0, ty0, tx1, ty1 = SENSOR_TONGUE
    ka = pcbnew.ZONE(board)
    ka.SetIsRuleArea(True); ka.SetDoNotAllowCopperPour(True)
    ka.SetDoNotAllowTracks(False); ka.SetDoNotAllowVias(True); ka.SetDoNotAllowPads(False)
    ka.SetDoNotAllowFootprints(False)
    ka.SetLayerSet(pcbnew.LSET.AllCuMask())
    ol = ka.Outline(); ol.NewOutline()
    for (x, y) in [(tx0 - 0.5, ty0), (tx1 + 0.5, ty0), (tx1 + 0.5, ty1 + 1.5), (tx0 - 0.5, ty1 + 1.5)]:
        ol.Append(mm(OX + x), mm(OY + y))
    board.Add(ka)
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
        for (x, y) in [(0.3, 0.3), (W - 0.3, 0.3), (W - 0.3, H - 0.3), (0.3, H - 0.3)]:
            ol.Append(mm(OX + x), mm(OY + y))
        board.Add(z)
    place_test_points(board, tps, CHARGER_FRAME, TEST_POINTS)
    for ref in sorted(B_SIDE):
        fp = board.FindFootprintByReference(ref)
        fp.Flip(fp.GetPosition(), pcbnew.FLIP_DIRECTION_LEFT_RIGHT)
    dfm.add_fiducials(board, FIDUCIALS, load_fp)
    plus_marks(board, ["J1", "J2", "J4", "J6"], dx=-2.2, dy=0.0)
    # silkscreen labels for the connectors, beside pin 2 so the "+" keeps pin 1
    labels = {"J5": "GND 3V3 SDA SCL", "J4": "BAT", "J3": "NTC", "J1": "SOLAR", "J2": "VIN2", "J6": "HEAT"}
    for ref, txt in labels.items():
        x, y, _ = PLACE[ref]
        t = pcbnew.PCB_TEXT(board)
        t.SetText(txt)
        t.SetPosition(pt(x - 2.2, y + 1.27 * 3 if ref == "J5" else y + 2.54 + 0.4 * len(txt) / 2 - 0.4))
        t.SetLayer(pcbnew.F_SilkS)
        t.SetTextSize(pcbnew.VECTOR2I(mm(0.8), mm(0.8))); t.SetTextThickness(mm(0.12))
        t.SetTextAngleDegrees(90)
        board.Add(t)
    t = pcbnew.PCB_TEXT(board)
    t.SetText("xiao-solar v0.5  n3tk0/MeshCore")
    t.SetPosition(pt(26.0, H - 1.5)); t.SetLayer(pcbnew.F_SilkS)
    t.SetTextSize(pcbnew.VECTOR2I(mm(0.8), mm(0.8))); t.SetTextThickness(mm(0.12))
    board.Add(t)
    # SWD column on the bottom (top to bottom 3V3M NRST SWDIO SWCLK GND): a mirrored label
    # beside each pad on B.SilkS
    for txt, ref in (("SWD", None), ("3V3", "TP12"), ("RST", "TP15"), ("DIO", "TP11"), ("CLK", "TP14"),
                     ("GND", "TP13")):
        x, y, _ = PLACE[ref] if ref else (PLACE["TP12"][0], PLACE["TP12"][1] - 1.4, 0)
        t = pcbnew.PCB_TEXT(board)
        t.SetText(txt)
        t.SetPosition(pt(x + 1.7, y) if ref else pt(x, y)); t.SetLayer(pcbnew.B_SilkS); t.SetMirrored(True)
        t.SetTextSize(pcbnew.VECTOR2I(mm(0.7), mm(0.7))); t.SetTextThickness(mm(0.12))
        board.Add(t)
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
    # analog across the trunk on B.Cu
    ("HEAT", (30.2, 41.2), ("R27", "1")), ("SH_P", (31.0, 41.9), ("R23", "2")),
    ("ADC_VBAT", (30.2, 42.6), ("R25", "2")), ("ADC_CHEM", (31.0, 43.2), ("JP2", "1")),
    ("SH_N", (31.6, 46.85), ("R24", "2")),
    # Xiao bus from the MSPM0 vias to the Xiao pins
    ("SCL", (26.95, 50.0), ("U3", "7")), ("SDA", (26.1, 49.05), ("U3", "8")),
    # 3V3M from the HT7533 output to the R13 via (the rest: Freerouting)
    ("3V3M", ("C26", "1"), (23.07, 42.4)),
]

def pre_route(b):
    import patch2, route
    KB = [(x0 - OX, y0 - OY, x1 - OX, y1 - OY) for x0, y0, x1, y1 in route.b_clear(b)]
    def end(e):
        if isinstance(e[0], float):
            return e
        p = _pad(b, *e).GetPosition()
        return (pcbnew.ToMM(p.x) - OX, pcbnew.ToMM(p.y) - OY)
    for net_, a_, b_ in PRE_ROUTES:
        try:
            print(patch2.route_net(b, net_, (end(a_), end(b_)), lock=True, width=0.2, clear=0.2,
                                   layer_cost=(1.0, 1.0), keepout_b=KB))
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
