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
# 5 mm were cut off, 50 x 70 mm), and its top-left corner sits at (100, 100) on the sheet.
W, H = 50.0, 75.0          # design area, mm
TOP = 5.0                  # first design y on the board
OX, OY = 100.0, 100.0 - TOP

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
    # between the modules: Xiao 5V bulk, 3V3 decoupling and the ideal-diode FET Q5 (D down to SYS)
    "Q5": (20.5, 23.5, 270), "C20": (9.8, 23.5, 0), "C21": (13.5, 23.5, 180), "C22": (16.7, 23.5, 0),
    # right column connectors (pin 1 at the given point, pins go down)
    # Xiao I2C pull-ups, top right (no SHT40 since v0.5: the MSPM0 reports the temperatures)
    "R11": (33.0, 4.4, 0), "R12": (33.0, 5.9, 0),
    # JP3 bridges the ship FET Q2 (closed = no ship FET, for LiFePO4)
    "JP3": (32.8, 58.0, 90),
}
# charger cluster (tools/charger.py), U1 at its position before the DY shift below
PLACE.update(charger.place(charger.Frame(16.0, 55.0), set(charger.PLACE)))
# Everything moves down by DY to leave the top corners free for the mounting holes: 2.8 mm
# (M2/M2.5) with 6 mm clear for the head, 3.5 mm from the edges.
DY = 6.0
CHARGER_FRAME = charger.Frame(16.0, 55.0 + DY, ox=OX, oy=OY)
PLACE = {r: (x, y + DY, a) for r, (x, y, a) in PLACE.items()}
# C9 (PMID HF cap) is 0603 now (basic 50 V part, as on solar-smart-mppt): its GND pad stays on
# stands upright right above pin 29 (PMID pad down), its GND pad ties into the pin 27 GND line;
# C16 steps 0.35 mm right, clear of the C9 courtyard (fix_charger_routes)
PLACE["C9"] = (PLACE["C9"][0] - 0.42, PLACE["C9"][1] - 0.87, 90)
PLACE["C16"] = (PLACE["C16"][0] + 0.35, PLACE["C16"][1], PLACE["C16"][2])
# v0.5 (final board coordinates, after DY). Connectors: SOLAR | BAT | NTC along the bottom
# edge (pins along it, pin 1 = + square), I2C (J5) and HEAT (J6) in one column at the right edge.
MCU_D = (8.5, -8.0)   # the MSPM0 cluster moved by this from its first v0.5 place (U6 27.2, 45.6)
def _m(x, y):
    return (round(x + MCU_D[0], 3), round(y + MCU_D[1], 3))
PLACE.update({
    # I2C (J5) lies along the top edge between the two top holes (pin 1 = SCL on the left)
    "J5": (33.2, TOP + 1.6, 90), "J6": (47.5, 62.0, 0),
    "J1": (9.0, 73.3, 90), "J4": (27.0, 73.3, -90), "J3": (35.0, 73.3, 90),
    # SOLAR -> D1 (reverse polarity) -> VBUS, TVS D3 beside it, both above the bottom row
    "D1": (11.8, 69.5, 180), "D3": (21.0, 69.5, 0),
    # BMS (DW01A + HJ8205) and the 20 mOhm low-side shunt on the right: Q3 S2 (BATT_S) -> RS1
    # -> GND, Kelvin taps R21 (BATT_S) and R22 (SH_K, net tie NT1 on the RS1 GND pad), C24
    # across them, bias R23 / R24 to 3V3M
    "R9": (45.0, 41.5, 0), "U2": (45.0, 44.7, 0), "C19": (48.2, 44.7, 90), "R10": (45.0, 47.9, 0),
    "Q3": (41.0, 51.8, 0), "RS1": (46.8, 52.2, 270),
    "R21": (44.2, 50.74, 180), "C24": (44.2, 52.2, 0), "R22": (44.2, 53.66, 180), "NT1": (45.66, 53.66, 0),
    "R23": (40.6, 47.6, 0), "R24": (40.6, 48.8, 0),
    # battery voltage divider for the MSPM0 ADC
    "R25": (37.4, 45.3, 180), "C30": (37.4, 46.5, 0), "R26": (37.4, 47.9, 0),
    # heater: J6 above the bottom-right hole, Q4 beside it, gate parts to its left
    "Q4": (43.4, 64.0, 0), "R27": (39.8, 63.3, 0), "R28": (39.8, 65.0, 0),
    # MSPM0 right of the SYS trunk, away from the Xiao pins: I2C pins face down (pull-ups below),
    # VDD / NRST left (caps beside it), analog pins and HEAT up, SWD / shunt right (MCU_STUBS)
    "U6": (_m(27.2, 45.6)[0], _m(27.2, 45.6)[1], 0),
    "C27": _m(23.55, 45.35) + (180,), "C29": _m(23.55, 44.3) + (180,), "R13": _m(23.55, 43.3) + (0,),
    "C28": _m(24.9, 41.9) + (180,),
    "R14": _m(29.62, 48.95) + (180,), "R15": _m(29.62, 50.9) + (180,), "R4": _m(31.3, 48.75) + (90,),
    # HT7533 above the MSPM0, C25 (SYS) left of it, C26 (3V3M) right
    "U8": (30.5, 28.0, 0), "C25": (27.0, 25.4, 90), "C26": (34.4, 28.0, 90),
    # CHEM jumper on top above the MSPM0 (label LFP), the SWD column on the bottom side (B_SIDE)
    "JP2": (39.5, 31.0, 90),
    # heater LED (D6 + R29 across J6): under Q4, left of the bottom-right hole
    "D6": (42.6, 66.9, 180), "R29": (42.6, 68.6, 180),
    # ideal-diode mirror (Q6 on SYS, Q7 on XIAO_5V, R30 / R31 1M) right of the Wio, above C25
    "Q6": (24.6, 18.0, 0), "Q7": (24.6, 21.75, 0), "R30": (24.0, 15.5, 0), "R31": (24.0, 14.0, 0),
    "TP12": (44.5, 22.5, 0), "TP15": (44.5, 25.04, 0), "TP11": (44.5, 27.58, 0),
    "TP14": (44.5, 30.12, 0), "TP13": (44.5, 32.66, 0),
})
B_SIDE = {"TP11", "TP12", "TP13", "TP14", "TP15"}
# three holes (Petko, 2026-11-12): the Wio fills the top-left corner, so only top right and the
# two bottom corners
PLACE.update({"H2": (W - 3.5, TOP + 3.5, 0), "H3": (3.5, H - 3.5, 0), "H4": (W - 3.5, H - 3.5, 0)})

OUTLINE = [(0, TOP), (W, TOP), (W, H), (0, H)]

# Bring-up test points: net -> (region x0, y0, x1, y1, preferred x, y). Charger nets sit
# inside their copper zone when there is room; the others are wired by the router.
TEST_POINTS = {
    "VBUS": (9.0, 58.0, 15.0, 64.0, 10.0, 61.5), "PMID": (4.5, 54.0, 12.0, 59.0, 6.0, 58.0),
    "SYS": (20.0, 54.0, 32.0, 59.0, 30.5, 55.5), "BAT": (22.0, 58.5, 34.0, 66.0, 29.5, 65.6),
    "SCL": (29.0, 10.5, 43.0, 16.0, 31.0, 11.5), "SDA": (29.0, 10.5, 43.0, 16.0, 33.5, 11.5),
    "3V3": (29.0, 10.5, 43.0, 16.0, 36.0, 11.5), "GND": (29.0, 10.5, 43.0, 16.0, 38.5, 11.5),
}
# three fiducials: region x0, y0, x1, y1 and the preferred point
FIDUCIALS = [(26.5, 11.5, 29.5, 15.5, 28.0, 12.5), (40.5, 9.5, 44.5, 14.0, 42.5, 11.5),
             (28.0, 66.0, 33.0, 72.0, 30.5, 70.5)]
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
    # BQ25798 bus on B.Cu: down beside the cluster (SCL_I, SDA_I, CE from the left), west in
    # nested steps to the lane x 32.0 / 32.5 / 33.0 and down it, then back left under the charger
    # (below its whole-GND area and the BATP run). SCL_I hops over SDA_I on F.Cu at x 25 so the
    # order matches the U1 stubs (SDA_I, SCL_I, CE from the top).
    ("SCL_I", [_cl, (_cl[0], 48.0), (32.0, 48.0), (32.0, 62.95), (25.2, 62.95)], 0.2, "B"),
    ("SCL_I", [(25.2, 62.95), (24.3, 64.2)]),
    ("SCL_I", [(24.3, 64.2), (20.4, 64.2), (18.95, 65.65), (18.95, 65.75)], 0.2, "B"),
    ("SCL_I", [(18.95, 65.75), (18.2, 65.6)]),
    ("SDA_I", [_sv, (_sv[0] + 0.7, _sv[1] + 0.7), (_sv[0] + 0.7, 48.7), (32.5, 48.7), (32.5, 63.6), (19.75, 63.6),
               (18.95, 64.4), (18.95, 64.8)], 0.2, "B"),
    ("SDA_I", [(18.95, 64.8), (18.2, 64.8)]),
    ("CE", [_cv, (_cv[0] + 0.6, _cv[1] + 0.6), (_cv[0] + 0.6, 49.4), (33.0, 49.4), (33.0, 64.8), (21.2, 64.8),
            (18.0, 68.0), (16.2, 67.98)], 0.2, "B"),
    # the charger's right corner: REGN over TS to R2, BATP on B.Cu above the bus to R8 (as v0.4)
    ("REGN", [(17.9, 62.18), (18.7, 62.18), (19.0, 62.48), (22.6, 62.48), (23.22, 63.1), (23.22, 64.8)], 0.2),
    ("TS", [(17.8, 62.58), (18.3, 63.08), (20.1, 63.08), (21.58, 64.56), (21.58, 64.8)], 0.2),
    ("BATP", [(17.92, 61.78), (18.62, 61.78), (18.9, 61.56)]),
    ("BATP", [(18.9, 61.56), (19.65, 62.25), (28.6, 62.25), (28.85, 62.0)], 0.2, "B"),
    ("BATP", [(28.85, 62.0), (28.78, 63.0)]),
    # BAT from the charger's BAT copper down to JP3 (bypass of the ship FET), as v0.4
    ("BAT", [(26.25, 60.64), (25.6, 61.3), (25.6, 61.78), (28.47, 64.65), (32.8, 64.65)], 0.6),
]
MCU_VIAS = [(n, _m(*q)) for n, q in MCU_LOCAL_VIAS] + [
    ("SCL_I", (25.2, 62.95)), ("SCL_I", (24.3, 64.2)), ("SCL_I", (18.95, 65.75)),
    ("SDA_I", (18.95, 64.8)), ("CE", (16.2, 67.98)), ("BATP", (18.9, 61.56)), ("BATP", (28.85, 62.0))]

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
    # C20 and C22 GND between the module rows get cut off by the traces there: own vias
    for ref in ("C20", "C22"):
        cg = _pad(board, ref, "2")
        # C20: below its pad (C21 sits right of it since v0.5), C22: right of its pad
        off = (0.9, 0) if ref == "C20" else (1.1, 0)
        at = pcbnew.VECTOR2I(cg.GetPosition().x + mm(off[0]), cg.GetPosition().y + mm(off[1]))
        _track(board, cg.GetPosition(), at, cg.GetNet(), 0.4)
        v = pcbnew.PCB_VIA(board)
        v.SetPosition(at); v.SetWidth(mm(0.6)); v.SetDrill(mm(0.3)); v.SetNet(cg.GetNet())
        v.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu); v.SetLocked(True)
        board.Add(v)
    # SYS to the Xiao: one 0.8 mm run from the right end of the SYS copper (the bulk caps close
    # its top side), diagonally up-left past the charger caps, up between the Xiao and the
    # MSPM0 and into the channel between the modules to the drain of the ideal-diode FET Q5
    sys_ = board.FindNet("SYS")
    vin = _pad(board, "Q5", "3").GetPosition()
    pts = [pt(32.6, 56.3), pt(32.6, 53.0), pt(32.6, 51.5), pt(28.0, 46.9), pt(26.5, 46.9), pt(26.5, 33.5), pt(24.0, 31.0), pcbnew.VECTOR2I(vin.x, mm(OY + 31.0)), vin]
    for a_, b_ in zip(pts, pts[1:]):
        _track(board, a_, b_, sys_, 0.8)
    # XIAO_5V from the Q5 source along the top of the channel (under the Wio pin row) and down
    # to the Xiao 5V pin; C20 joins it on the way
    v5 = board.FindNet("XIAO_5V")
    src, p5 = _pad(board, "Q5", "2").GetPosition(), _pad(board, "U3", "14").GetPosition()
    pts = [src, pcbnew.VECTOR2I(src.x, mm(OY + 27.7)), pcbnew.VECTOR2I(p5.x, mm(OY + 27.7)), p5]
    for a_, b_ in zip(pts, pts[1:]):
        _track(board, a_, b_, v5, 0.6)
    c20 = _pad(board, "C20", "1").GetPosition()
    _track(board, c20, pcbnew.VECTOR2I(c20.x, mm(OY + 27.7)), v5, 0.6)
    # heater feed (up to ~2.5 A), all on F.Cu: 1.2 mm from the SYS copper along y 56.3 and down
    # the right edge to J6 pin 1; the return from the Q4 drain to J6 pin 2
    run = [pt(31.3, 57.6), pt(32.6, 56.3), pt(46.2, 56.3), pt(47.5, 57.6), _pad(board, "J6", "1").GetPosition()]
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
    v1, v2 = pt(34.0, 60.3), pt(34.0, 61.5)
    _track(board, _pad(board, "Q2", "3").GetPosition(), v1, bat, 1.0)
    _track(board, v1, v2, bat, 1.0)
    jp = _pad(board, "JP3", "2").GetPosition()
    _track(board, jp, pcbnew.VECTOR2I(jp.x, v2.y), bat, 1.0)
    _track(board, pcbnew.VECTOR2I(jp.x, v2.y), v2, bat, 1.0)
    j41 = _pad(board, "J4", "1").GetPosition()
    pts = [v2, pt(34.4, 61.9), pt(34.4, 67.0), pcbnew.VECTOR2I(j41.x + mm(1.6), j41.y - mm(1.6)), j41]
    for i, (a_, b_) in enumerate(zip(pts, pts[1:])):
        # 1.0 mm, but 0.8 on the diagonal past the TS via
        _track(board, a_, b_, bat, 0.8 if i == 2 else 1.0)
    # BATT- from J4 pin 2 on B.Cu along the bottom (under J4 / J3) and up to the HJ8205
    bm = board.FindNet("BATT-")
    j42 = _pad(board, "J4", "2").GetPosition()
    q1 = _pad(board, "Q3", "1").GetPosition()
    up = (pcbnew.ToMM(q1.x) - OX - 1.2, 55.0)
    pts = [j42, pcbnew.VECTOR2I(j42.x, mm(OY + 71.3)), pt(up[0], 71.3), pt(*up)]
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
    kv = [pt(12.4, 66.9), pt(13.2, 66.9)]
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
    t.SetPosition(pt(39.5, 21.0)); t.SetLayer(pcbnew.F_SilkS)
    t.SetTextSize(pcbnew.VECTOR2I(mm(0.8), mm(0.8))); t.SetTextThickness(mm(0.12))
    board.Add(t)
    # SWD column on the bottom (top to bottom 3V3M NRST SWDIO SWCLK GND): a mirrored label
    # beside each pad on B.SilkS
    for txt, ref in (("SWD", None), ("3V3", "TP12"), ("RST", "TP15"), ("DIO", "TP11"), ("CLK", "TP14"),
                     ("GND", "TP13")):
        x, y, _ = PLACE[ref] if ref else (PLACE["TP12"][0], PLACE["TP12"][1] - 1.4, 0)
        t = pcbnew.PCB_TEXT(board)
        t.SetText(txt)
        t.SetPosition(pt(x + 2.0, y) if ref else pt(x, y)); t.SetLayer(pcbnew.B_SilkS); t.SetMirrored(True)
        t.SetTextSize(pcbnew.VECTOR2I(mm(0.8), mm(0.8))); t.SetTextThickness(mm(0.12))
        board.Add(t)
    # JP2 (CHEM: closed = LiFePO4) is named by function instead of its reference
    x, y, _ = PLACE["JP2"]
    board.FindFootprintByReference("JP2").Reference().SetVisible(False)
    label("LFP", pt(x + 2.1, y), 0)
    # heater LED references left of the parts (the hole and J6 are right of them)
    for ref in ("D6", "R29"):
        r = board.FindFootprintByReference(ref).Reference()
        r.SetPosition(pt(39.4, PLACE[ref][1])); r.SetTextAngleDegrees(0)
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
    # analog from the cluster vias to the shunt, divider, CHEM jumper and heater gate
    ("HEAT", _m(30.2, 41.2), ("R27", "1")), ("SH_P", _m(31.0, 41.9), ("R23", "2")),
    ("ADC_VBAT", _m(30.2, 42.6), ("R25", "2")), ("ADC_CHEM", _m(31.0, 43.2), ("JP2", "1")),
    ("SH_N", _m(31.6, 46.85), ("R24", "2")),
    # SWD on B.Cu to the pads of the column
    ("NRST", _m(24.85, 42.95), ("TP15", "1")), ("SWDIO", _m(31.2, 45.85), ("TP11", "1")),
    ("SWCLK", _m(30.4, 44.85), ("TP14", "1")),
    # Xiao bus from the MSPM0 stubs to the Xiao pins
    ("SCL", _m(26.95, 50.0), ("U3", "7")), ("SDA", _m(26.1, 49.05), ("U3", "8")),
    # 3V3M from the HT7533 output to the R13 via and the SWD pad
    ("3V3M", ("C26", "1"), _m(23.07, 42.4)), ("3V3M", ("C26", "1"), ("TP12", "1")),
    # NTC from J3 to the TS node at R2 / R3
    ("TS", ("J3", "1"), (21.58, 64.8)),
    # heater LED: D6 anode to R29, cathode to the J6 return, R29 to SYS at J6 pin 1
    ("LED_HEAT", ("D6", "2"), ("R29", "2")), ("HEAT_D", ("D6", "1"), ("J6", "2")),
    ("SYS", ("R29", "1"), ("J6", "1")),
    # ideal-diode mirror: thin sense lines (the power path is Q5 alone)
    ("SYS", ("Q6", "2"), (24.0, 31.0)), ("XIAO_5V", ("Q7", "2"), ("Q5", "2")),
    ("OR_G", ("Q7", "3"), ("Q5", "1")),
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
