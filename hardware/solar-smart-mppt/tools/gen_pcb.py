#!/usr/bin/env python3
"""Builds solar-smart-mppt.kicad_pcb (30 x 77 mm): placement, outline, net classes, GND pours.
Top: header row J6 SYS OUT | J5 I2C | J7 regulated out, ideal diode (Q5 + Q9/Q11) under J6, TPS63070 and its
jumpers under J7, LEDs. Solar input (J1, SS54, TVS) on the left edge next to the charger.
Then the charger part of xiao-solar-lite (charger cluster of ../xiao-solar/tools/charger.py,
turned), the MSPM0L1306 band and at the bottom the BMS it drives, the fuse and J4.

Run: python3 tools/gen_pcb.py, then python3 tools/run.py route (see README)
"""
import importlib.util, json, os, sys
import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
if HERE not in sys.path:
    sys.path.insert(0, HERE)
import gen_sch  # noqa: E402
_spec = importlib.util.spec_from_file_location(
    "xs_gen_pcb", os.path.join(ROOT, "..", "xiao-solar", "tools", "gen_pcb.py"))
base = importlib.util.module_from_spec(_spec)
SHARED = os.path.abspath(os.path.join(ROOT, "..", "xiao-solar", "tools"))
if SHARED not in sys.path:
    sys.path.append(SHARED)
_spec.loader.exec_module(base)
import charger  # noqa: E402
import dfm  # noqa: E402
mm, pt, OX, OY = base.mm, base.pt, base.OX, base.OY

PROJECT = "solar-smart-mppt"
PCB = os.path.join(ROOT, PROJECT + ".kicad_pcb")
W, H = 30.0, 77.5   # 30 mm: three boards across a 100 mm panel
DY = 0.0    # the xiao-solar-lite charger part sits where it is on lite, under the new top part
DA = 32.0   # MCU band (v0.2 coordinates of its test point regions)
DB = 13.0   # BMS, fuse and the bottom connectors: under the MCU band (QFN-24: 5 mm shorter than v0.2)
base.FPLIB_LOCAL = os.path.join(ROOT, "lib", "xiao_solar.pretty")

CHARGER_FRAME = charger.Frame(11.0, 32.8 + DY, 270)
PLACE = charger.place(CHARGER_FRAME, set(charger.PLACE))
LITE = {
    "J3": (2.1, 39.2, 0), "JP3": (7.1, 43.2, 90),
}
PLACE.update({k: (x, y + DY, r) for k, (x, y, r) in LITE.items()})
# C9 (PMID HF cap) is 0603 now (basic 50 V part): its GND pad stays on the pin 27 GND line
# (the PMID pad grows towards pin 29), C16 steps 0.25 mm out so the courtyards clear
# and 0.2 mm away from the pins (its wider pads would sit 0.03 mm from the STAT pad and 0.08 mm
# from SW1/SW2); see fix_charger_routes() for the PMID track
PLACE["C9"] = (PLACE["C9"][0] + 0.2, PLACE["C9"][1] - 0.29, PLACE["C9"][2])
PLACE["C16"] = (PLACE["C16"][0] + 0.5, PLACE["C16"][1], PLACE["C16"][2])
# TPS63070 block (layout after the TI example) sits under J7: v0.1 coordinates + this shift
TDX, TDY = 9.4, 3.9
def tps(x, y):
    return (x + TDX, y + TDY)
_TPS = {
    "U6": (10.6, 11.4, 0), "L2": (10.6, 7.4, 0),
    "C31": (8.0, 11.7, 270), "C32": (5.95, 8.0, 90), "C33": (5.95, 11.7, 270),
    "C35": (13.75, 11.55, 270), "C36": (14.6, 7.4, 90), "C37": (16.6, 7.4, 90), "C38": (18.6, 7.4, 90),
    "C34": (10.85, 13.8, 270), "R33": (15.0, 14.1, 0), "R34": (15.0, 15.7, 0), "R35": (18.1, 14.1, 0),
    "R30": (4.65, 14.9, 270), "R32": (12.7, 14.3, 90),
}
PLACE.update({k: tps(x, y) + (r,) for k, (x, y, r) in _TPS.items()})
PLACE.update({
    # one header row on top: J6 SYS OUT (+ -), M2 hole, J5 host I2C (SCL SDA NC GND), J7 U (+ -):
    # the regulated and the unregulated output on the two sides of the I2C header
    "J6": (2.4, 3.0, 90), "H1": (9.3, 3.0, 0), "J5": (13.7, 3.0, 90), "J7": (25.3, 3.0, 90),
    # ideal diode under J6: Q5 source straight up to J6 pin 1, its PNP mirror Q9 + Q11 (two MMBT3906 side by side) + R43/R44 under it
    # EN pull-down next to JP5 (left of C31 it would box the C31 GND pad in)
    "R31": (26.9, 20.6, 0),
    "Q5": (2.6, 6.7, 0), "Q9": (2.6, 10.3, 0), "Q11": (6.6, 10.3, 0),
    "R43": (5.6, 13.0, 0), "R44": (5.6, 14.3, 0), "C20": (2.8, 13.6, 0),
    # EN / VSEL jumpers of J7 in its column, right under the TPS63070 next to its EN/VSEL pins
    # (above it are the inductor and the VOUT caps); VREG test point next to J7
    "JP5": (27.0, 22.4, 0), "JP6": (22.8, 22.4, 0), "TP14": (22.8, 6.3, 0),
    # LEDs under the hole, next to the I2C header
    "D6": (9.6, 7.4, 90), "R16": (9.6, 10.1, 90), "D7": (11.6, 7.4, 90), "R17": (11.6, 10.1, 90),
    # solar input on the left edge next to the charger's VBUS copper: J1, SS54 (D1), TVS (D3)
    "D1": (4.3, 17.2, 180), "J1": (1.85, 21.3, 0), "D3": (5.4, 25.0, 270),
    # MSPM0L1306 (VQFN-24) turned 180, pins grouped by direction (see MCU_STUBS): I2C, CE, LED,
    # J7 EN up; SH_N, SWD, VBM left; CHEM, VBAT, SH_P, OD, VCORE, OC_G down; BAL2/BAL1, VDD, NRST right
    "U4": (13.5, 53.6, 180),
    # C28 / C29 between BAL1 (over them) and NRST (under C28); C39 right of the VCORE pin
    "C28": (17.6, 53.85, 0), "C29": (20.0, 54.6, 90), "C39": (15.6, 57.3, 0),
    "R42": (18.0, 55.9, 0), "C40": (18.0, 56.9, 0),
    "R13": (10.2, 49.4, 0), "R14": (10.2, 50.4, 0),
    # shunt filter next to the shunt RS1: C24 across SH_P / SH_N, Kelvin R21 / R22, bias R23 / R24;
    # VBAT and VBM dividers and JP4 under the MCU, HT7533 left
    "C24": (13.25, 67.2, 270), "R24": (10.85, 68.2, 90), "R23": (14.6, 65.4, 270), "R22": (11.9, 68.2, 90), "R21": (14.6, 67.6, 90), "NT1": (12.6, 69.9, 0),
    "R25": (9.2, 65.7, 90), "R26": (10.4, 65.7, 270), "C26": (11.6, 65.7, 270),
    "R27": (7.2, 60.6, 90), "R28": (8.4, 60.6, 270), "C27": (9.6, 60.6, 270), "JP4": (9.6, 62.9, 0),
    "U5": (3.6, 62.2, 0), "C21": (2.1, 56.3, 90), "C22": (5.4, 56.3, 0),
    # SWD pads on the bottom side (B.Cu, B_SIDE), one row at 2.54 mm under the MCU band:
    # 3V3M, GND, SWDIO, SWCLK, NRST (a 5-pin header or pogo pins from below)
    "TP12": (2.2, 59.2, 0), "TP13": (4.74, 59.2, 0), "TP11": (7.28, 59.2, 0),
    "TP16": (9.82, 59.2, 0), "TP17": (12.36, 59.2, 0),
    # BMS bottom right: Q3 in BATT-, shunt, its Kelvin taps, OD driver
    "Q3": (23.60, 56.2 + DB, 0), "RS1": (15.00, 57.6 + DB, 180), 
    "Q6": (18.50, 51.4 + DB, 0), "R18": (14.6, 48.8 + DB, 0), "R19": (22.30, 52.9 + DB, 90), "R20": (26.00, 55.4 + DB, 90),
    # 2S balancing in the free strip right of the SYS caps and the MCU (Q8 next to R41)
    "R37": (24.20, 40.2 + DY, 90), "R41": (24.20, 45.0 + DY, 90), "Q7": (25.20, 49.4 + DB, 0), "Q10": (26.2, 52.7, 0),
    "R38": (24.30, 52.6 + DB, 90), "R39": (23.2, 51.4, 0), "Q8": (25.4, 49.1, 180), "R40": (28.0, 49.1, 270),
    # J4 BATT+ BM BATT- along the bottom edge (pin 1 left), its fuse F1 next to BATT+, M2 hole
    "J4": (22.4, 62.0 + DB, 90), "F1": (18.0, 62.0 + DB, 0), "H2": (4.0, 61.5 + DB, 0),
})
# test points on the bottom side (flipped footprints): the SWD row
B_SIDE = {"TP11", "TP12", "TP13", "TP16", "TP17"}
# Bring-up test points: net -> (region x0, y0, x1, y1, preferred x, y)
TEST_POINTS = {
    "VBUS": (8.0, 25.0 + DY, 13.0, 32.0 + DY, 9.5, 27.0 + DY), "PMID": (13.0, 21.0 + DY, 17.6, 33.0 + DY, 15.0, 23.0 + DY),
    "SYS": (13.0, 33.0 + DY, 17.6, 49.0 + DY, 15.0, 47.5 + DY), "BAT": (10.0, 35.0 + DY, 13.5, 47.0 + DY, 11.8, 45.5 + DY),
    "GND": (0.8, 48.5, 22.2, 70.0, 2.2, 67.0), "3V3M": (0.8, 48.5, 22.2, 61.0, 19.6, 51.4),
    "SCL_I": (0.8, 47.5, 22.2, 61.0, 7.6, 49.0), "SDA_I": (0.8, 47.5, 22.2, 61.0, 7.6, 51.0),
    "VREG": (21.8, 5.3, 23.8, 7.6, 22.8, 6.3),
}
FIDUCIALS = [
    (0.8, 27.5 + DY, 6.3, 30.5 + DY, 2.5, 29.0 + DY),
    (9.0, 12.0, 13.4, 20.0, 11.2, 17.5),
    (0.8, 47.0 + DB, 22.2, 60.0 + DB, 6.5, 68.5),
]
# B.Cu stays plain GND (no autorouted B.Cu tracks) where long B.Cu runs would otherwise
# cut a GND pocket loose: under the BQ25798 GND via, and left of the ATtiny
# and the whole TPS63070 block: solid GND under the switcher, nothing else crosses it on B.Cu
TPS_KEEP = (13.2, 8.6, 29.6, 16.8)   # down to just under the IC: its control pins may use B.Cu
B_CLEAR = [(6.9, 31.2, 9.6, 37.2), TPS_KEEP]
CHARGER_B_CLEAR = (-4.5, -10.4, 4.5, 1.0)

NETCLASSES = {
    "Power": (0.6, 0.15, 0.8, 0.4, ["BATT+", "BATT_J", "BATT-", "BATT_S", "FS_D", "SOLAR_IN", "VOUT", "VBUS", "SYS",
                                    "BAT", "PMID", "VREG"]),
    "PowerQFN": (0.25, 0.13, 0.8, 0.4, ["SW1", "SW2", "SWA", "SWB"]),
    "Supply": (0.3, 0.13, 0.6, 0.3, ["3V3M", "GND"]),
}

# TPS63070 power copper on F.Cu (TI layout guide 11.1): SW nodes as short wide copper to
# the inductor, VIN and VOUT copper over the pins and the HF caps; PGND (10) and GND (4)
# meet under the package and drop to two vias (one ground node at the IC).
TPS_ZONES = {
    "SWA": [(8.0, 6.15), (9.5, 6.15), (9.5, 8.65), (10.25, 9.4), (10.25, 11.0), (9.95, 11.0),
            (9.95, 9.6), (9.3, 8.95), (8.0, 8.95)],
    "SWB": [(13.2, 6.15), (11.7, 6.15), (11.7, 8.65), (10.95, 9.4), (10.95, 11.0), (11.25, 11.0),
            (11.25, 9.6), (11.9, 8.95), (13.2, 8.95)],
    "SYS": [(5.3, 8.4), (6.9, 8.4), (6.9, 9.6), (8.95, 9.6), (9.5, 10.1), (9.5, 11.1), (5.3, 11.1)],
    "VREG": [(11.62, 11.12), (14.4, 11.12), (14.4, 9.3), (19.9, 9.3), (19.9, 7.8), (14.2, 7.8),
             (13.6, 9.15), (11.62, 9.55)],
}
TPS_GND = [((10.6, 11.45), (11.35, 11.45)), ((11.35, 11.45), (11.35, 12.2)), ((11.35, 12.2), (11.35, 12.7)), ((11.35, 12.7), (11.8, 13.15)),
           ((11.8, 13.15), (11.8, 14.0)), ((10.85, 14.28), (11.8, 14.0))]
TPS_GND_VIAS = [(11.8, 13.15), (11.8, 14.0)]

def tps_copper(board, net):
    for name, poly in TPS_ZONES.items():
        poly = [tps(x, y) for x, y in poly]
        z = pcbnew.ZONE(board)
        z.SetLayer(pcbnew.F_Cu); z.SetNet(net(name)); z.SetAssignedPriority(2)
        z.SetLocalClearance(mm(0.2)); z.SetMinThickness(mm(0.2))
        z.SetPadConnection(pcbnew.ZONE_CONNECTION_FULL)
        z.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_ALWAYS)
        ol = z.Outline(); ol.NewOutline()
        for x, y in poly:
            ol.Append(mm(OX + x), mm(OY + y))
        board.Add(z)
    g = net("GND")
    for a_, b_ in TPS_GND:
        charger._track(board, pt(*tps(*a_)), pt(*tps(*b_)), g, 0.3)
    for v in TPS_GND_VIAS:
        charger._via(board, pt(*tps(*v)), g)
    # VAUX (3) straight down into its cap
    charger._track(board, pt(*tps(10.85, 12.36)), pt(*tps(10.85, 13.32)), net("VAUX"), 0.25)
    for fp in board.GetFootprints():
        if fp.GetReference() in ("U6", "L2", "C31", "C32", "C33", "C35", "C36", "C37", "C38", "C34"):
            fp.SetZoneConnection(pcbnew.ZONE_CONNECTION_FULL)

def fix_charger_routes(board):
    """The shared charger pre-routes assume the 0402 C9: PMID from pin 29 goes straight out first,
    clear of the STAT pad, then into the 0603 pad; the REGN B.Cu stub ends where the hand route
    turns down (x 9.5)."""
    tr = [t for t in board.GetTracks() if t.GetClass() == "PCB_TRACK"]
    p29 = charger._pad(board, "U1", "29").GetPosition()
    c9 = charger._pad(board, "C9", "1").GetPosition()
    for t in tr:
        ends = {(t.GetStart().x, t.GetStart().y), (t.GetEnd().x, t.GetEnd().y)}
        if t.GetNetname() == "PMID" and ends == {(p29.x, p29.y), (c9.x, c9.y)}:
            board.Remove(t)
            mid = pcbnew.VECTOR2I(c9.x - mm(0.35), p29.y)
            charger._path(board, [p29, mid, c9], charger._pad(board, "U1", "29").GetNet(), 0.22)
        if t.GetNetname() == "REGN" and t.GetLayer() == pcbnew.B_Cu:
            a, b_ = (pcbnew.ToMM(v) - o for v, o in ((t.GetStart().x, OX), (t.GetEnd().x, OX)))
            if abs(min(a, b_) - 8.5) < 0.01 and abs(t.GetStart().y - t.GetEnd().y) < 10:
                if a < b_:
                    t.SetStart(pt(9.5, pcbnew.ToMM(t.GetStart().y) - OY))
                else:
                    t.SetEnd(pt(9.5, pcbnew.ToMM(t.GetEnd().y) - OY))

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
        fp = base.load_fp(p["fp"])
        fp.SetReference(ref); fp.SetValue(p["value"]); fp.SetFPIDAsString(p["fp"])
        x, y, rot = PLACE.get(ref, (0, 0, 0))
        fp.SetPosition(pt(x, y)); fp.SetOrientationDegrees(rot)
        if p["lcsc"]:
            fp.SetField("LCSC", p["lcsc"]) if hasattr(fp, "SetField") else None
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
        if ref in B_SIDE:
            fp.Reference().SetVisible(False)   # the B.Cu silk labels name them
        if ref.startswith("TP") and ref not in PLACE:
            fp.Reference().SetVisible(False)
            tps[ref] = (fp, p["nets"]["1"])
            continue
        if ref in ("J1", "J3", "J4", "J5", "J6", "J7"):
            # the header outlines touch the edge or each other; the labels say it
            for g in list(fp.GraphicalItems()):
                if g.GetLayer() == pcbnew.F_SilkS and isinstance(g, pcbnew.PCB_SHAPE):
                    g.SetLayer(pcbnew.F_Fab)
        board.Add(fp)
    if missing:
        raise SystemExit("not placed: " + " ".join(missing))
    charger.preroute(board, CHARGER_FRAME)
    fix_charger_routes(board)
    corners = [(0, 0), (W, 0), (W, H), (0, H)]
    for i in range(4):
        s = pcbnew.PCB_SHAPE(board)
        s.SetShape(pcbnew.SHAPE_T_SEGMENT)
        s.SetStart(pt(*corners[i])); s.SetEnd(pt(*corners[(i + 1) % 4]))
        s.SetLayer(pcbnew.Edge_Cuts); s.SetWidth(mm(0.1))
        board.Add(s)
    # 0.4 mm strip along the edges: Freerouting only keeps its track clearance to the outline
    e = 0.4
    for (x0, y0, x1, y1) in ((0, 0, W, e), (0, H - e, W, H), (0, 0, e, H), (W - e, 0, W, H)):
        ka = pcbnew.ZONE(board)
        ka.SetIsRuleArea(True); ka.SetDoNotAllowCopperPour(False)
        ka.SetDoNotAllowTracks(True); ka.SetDoNotAllowVias(True); ka.SetDoNotAllowPads(False)
        ka.SetDoNotAllowFootprints(False)
        ka.SetLayerSet(pcbnew.LSET.AllCuMask())
        ol = ka.Outline(); ol.NewOutline()
        for (x, y) in [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]:
            ol.Append(mm(OX + x), mm(OY + y))
        board.Add(ka)
    charger.add_zones(board, CHARGER_FRAME)
    tps_copper(board, net)
    for layer in (pcbnew.F_Cu, pcbnew.B_Cu):
        z = pcbnew.ZONE(board)
        z.SetLayer(layer); z.SetNet(net("GND"))
        z.SetLocalClearance(mm(0.2)); z.SetMinThickness(mm(0.2))
        z.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL)
        z.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_ALWAYS)
        z.SetThermalReliefGap(mm(0.3)); z.SetThermalReliefSpokeWidth(mm(0.4))
        ol = z.Outline(); ol.NewOutline()
        for (x, y) in [(0.3, 0.3), (W - 0.3, 0.3), (W - 0.3, H - 0.3), (0.3, H - 0.3)]:
            ol.Append(mm(OX + x), mm(OY + y))
        board.Add(z)
    base.place_test_points(board, tps, CHARGER_FRAME, TEST_POINTS)
    for ref in sorted(B_SIDE):
        fp = board.FindFootprintByReference(ref)
        fp.Flip(fp.GetPosition(), pcbnew.FLIP_DIRECTION_LEFT_RIGHT)
    dfm.add_fiducials(board, FIDUCIALS, base.load_fp)
    # the fiducials want 0.65 mm clearance (pad local rule) that Freerouting does not see:
    # a small no-track/no-via area on F.Cu around each one
    for fp in board.GetFootprints():
        if fp.GetReference().startswith("FID"):
            c = fp.GetPosition()
            ka = pcbnew.ZONE(board)
            ka.SetIsRuleArea(True); ka.SetDoNotAllowCopperPour(False)
            ka.SetDoNotAllowTracks(True); ka.SetDoNotAllowVias(True); ka.SetDoNotAllowPads(False)
            ka.SetDoNotAllowFootprints(False)
            ka.SetLayer(pcbnew.F_Cu)
            ol = ka.Outline(); ol.NewOutline()
            r = mm(1.35)
            for dx, dy in ((-r, -r), (r, -r), (r, r), (-r, r)):
                ol.Append(c.x + dx, c.y + dy)
            board.Add(ka)
    base.plus_marks(board, ["J1", "J4"], dx=0.0, dy=-1.6)
    base.plus_marks(board, ["J6", "J7"], dx=0.0, dy=1.9)
    charger.pin1_dot(board, CHARGER_FRAME)
    def text(s, x, y, size=0.8, angle=0, layer=pcbnew.F_SilkS):
        t = pcbnew.PCB_TEXT(board)
        t.SetText(s); t.SetPosition(pt(x, y)); t.SetLayer(layer)
        if layer == pcbnew.B_SilkS:
            t.SetMirrored(True)
        t.SetTextSize(pcbnew.VECTOR2I(mm(size), mm(size))); t.SetTextThickness(mm(0.12))
        t.SetTextAngleDegrees(angle)
        board.Add(t)
    # one label per pin, next to the pin
    for ref, names, y in (("J4", ("BAT+", "BM", "BAT-"), H - 0.9),
                          ("J6", ("SYS",), 0.9), ("J7", ("3V3/5V",), 0.9), ("J5", ("SCL", "SDA", "NC", "GND"), 0.9)):
        p1 = charger._pad(board, ref, "1").GetPosition()
        x1 = pcbnew.ToMM(p1.x) - OX
        if len(names) == 1:
            text(names[0], x1 + 1.27, y)
        else:
            for i, s_ in enumerate(names):
                text(s_, x1 + 2.54 * i, y, 0.7)
    text("NTC", 2.1, 37.3 + DY)
    text("SOLAR", 1.85, 26.6 + DY, 0.6)
    # jumper settings next to the pads (bridge = solder blob)
    jp = lambda ref, num: pcbnew.ToMM(charger._pad(board, ref, num).GetPosition().x) - OX
    text("ON", (jp("JP5", "1") + jp("JP5", "2")) / 2, 24.2, 0.6)
    text("MCU", (jp("JP5", "2") + jp("JP5", "3")) / 2, 24.2, 0.6)
    text("J7 5V", jp("JP6", "1") + 0.65, 24.2, 0.6)
    y4 = lambda num: pcbnew.ToMM(charger._pad(board, "JP4", num).GetPosition().y) - OY
    text("LFP", 27.2, (y4("1") + y4("2")) / 2, 0.6)
    text("LTO", 27.2, (y4("2") + y4("3")) / 2, 0.6)
    # SWD pads: a 5-pin header (SWDIO, 3V3M, GND, SWCLK, NRST)
    for ref, s_ in (("TP11", "IO"), ("TP16", "CLK"), ("TP12", "3V3"), ("TP13", "GND"), ("TP17", "RST")):
        text(s_, jp(ref, "1"), PLACE[ref][1] + 1.3, 0.6, layer=pcbnew.B_SilkS)
    text("SWD", 7.28, PLACE["TP11"][1] - 1.3, 0.6, layer=pcbnew.B_SilkS)
    text("solar-smart-mppt v0.3", 26.0, 28.0 + DY, 0.7, 90)
    hidden = dfm.place_silk(board)
    print("silk: hidden refs", " ".join(hidden) or "none")
    return board

def write_project():
    def cls(name, tw, cl, vd, vdr):
        return {"name": name, "track_width": tw, "clearance": cl, "via_diameter": vd, "via_drill": vdr,
                "microvia_diameter": 0.3, "microvia_drill": 0.1, "diff_pair_width": 0.2,
                "diff_pair_gap": 0.25, "diff_pair_via_gap": 0.25, "wire_width": 6, "bus_width": 12,
                "line_style": 0, "pcb_color": "rgba(0, 0, 0, 0.000)", "schematic_color": "rgba(0, 0, 0, 0.000)"}
    classes = [cls("Default", 0.25, 0.13, 0.6, 0.3)]
    patterns = []
    for name, (tw, cl, vd, vdr, nets) in NETCLASSES.items():
        classes.append(cls(name, tw, cl, vd, vdr))
        patterns += [{"netclass": name, "pattern": n} for n in nets]
    pro = {"meta": {"filename": PROJECT + ".kicad_pro", "version": 1},
           "board": {"design_settings": {"rules": {"min_clearance": 0.127, "min_track_width": 0.15,
                                                     "min_via_diameter": 0.6, "min_through_hole_diameter": 0.3,
                                                     "min_copper_edge_clearance": 0.3, "min_hole_clearance": 0.25}}},
           "net_settings": {"classes": classes, "meta": {"version": 3}, "netclass_patterns": patterns},
           "schematic": {}, "sheets": [[gen_sch.ROOT_UUID, ""]]}
    open(os.path.join(ROOT, PROJECT + ".kicad_pro"), "w", encoding="utf-8").write(json.dumps(pro, indent=2) + "\n")

# SYS from the charger's SYS copper up to the TPS63070 VIN copper: routed before the
# autorouter (which leaves it open) as one locked wide run, board mm
SYS_RUN = ((17.05, 38.15), tps(5.8, 9.0))   # kept out from under the TPS63070 (its small signals)
# the long ATtiny signals (across the charger or the BMS) go in first the same way, locked;
# Freerouting closes them badly or not at all on this narrow board
PRE_ROUTES = [("SYS", ("Q5", "3"), tps(5.8, 9.0)), ("SYS", ("R30", "1"), ("C33", "1")),
              # MSPM0: the fan-out itself is MCU_STUBS (below); these finish the far ends
              ("OC_G", (16.1, 56.25), ("Q3", "4")),
              ("OD_CTL", (16.6, 58.2), ("Q6", "1")), ("BAL1", (21.8, 52.85), ("Q7", "1")),
              ("CE", (9.0, 51.3), ("U1", "13")), ("REGN", ("C10", "1"), (10.55, 31.6)),
              ("VREG", ("TP14", "1"), ("C36", "1")),
              ("BAL2", ("R39", "1"), ("Q10", "1")),
              ("SYS", ("C21", "1"), ("U5", "2")),
              ("SDA_I", ("U4", "12"), ("R13", "2")), ("SCL_I", ("U4", "11"), ("R14", "2")),
              ("SDA_E", ("U4", "9"), ("J5", "2")), ("SCL_E", ("U4", "10"), ("J5", "1")),
              ("LED", ("U4", "8"), ("D7", "1")), ("REG_CTL", ("U4", "7"), ("JP5", "3")),
              ("VREG", ("R33", "1"), ("C35", "1"))]
# MSPM0 fan-out by hand (0.2 mm, F.Cu, locked), board mm. Left side nested (the upper pin goes
# out further): CE up, SH_N to a via (B.Cu down to the shunt filter), SWDIO / SWCLK to the
# TP row, VBM to its divider under the TP row. Bottom: CHEM, VBAT and SH_P straight down to
# JP4, the VBAT divider and C24; OD_CTL under C39; VCORE to C39; OC_G to a via. Right: BAL2 /
# BAL1 over C28 / C29 to the balancer strip, 3V3M to C28, NRST under C28 to R42.
MCU_STUBS = [
    ("CE", [(11.51, 52.35), (9.0, 52.35), (9.0, 51.3)]),
    ("SH_N", [(11.51, 52.85), (8.2, 52.85)]),
    ("SWDIO", [(11.51, 53.35), (9.0, 53.35), (9.0, 57.4), (8.0, 57.4)]),
    ("SWDIO", [(8.0, 57.4), (7.28, 58.12), (7.28, 59.2)], "B"),
    ("SWCLK", [(11.51, 53.85), (9.5, 53.85), (9.5, 58.3)]),
    ("SWCLK", [(9.5, 58.3), (9.82, 58.62), (9.82, 59.2)], "B"),
    ("ADC_VBM", [(11.51, 54.35), (10.3, 54.35), (10.3, 59.7), (9.88, 60.12), (7.2, 60.12)]),
    ("ADC_CHEM", [(12.25, 55.59), (12.25, 61.7), (9.6, 61.7), (9.6, 62.9)]),
    ("ADC_VBAT", [(12.75, 55.59), (12.75, 65.22), (9.2, 65.22)]),
    ("SH_P", [(13.25, 55.59), (13.25, 66.72), (14.6, 66.72)]),
    ("SH_P", [(14.6, 65.88), (14.6, 67.12)]),
    ("SH_N", [(13.25, 68.5), (13.25, 67.68), (10.85, 67.68)]),
    # SH_N on B.Cu from its via at the left side of U4 down under SH_P (a pair from y 62)
    ("SH_N", [(8.2, 52.85), (11.1, 55.75), (11.1, 60.2), (13.25, 62.35), (13.25, 68.5)], "B"),
    ("SH_K", [(11.9, 68.68), (11.9, 69.7), (12.1, 69.9)]),
    ("GND", [(10.4, 66.18), (11.6, 66.18)]),
    ("GND", [(18.08, 53.85), (20.0, 53.85)]),
    ("OD_CTL", [(13.75, 55.59), (13.75, 58.2), (16.6, 58.2)]),
    ("VCORE", [(14.25, 55.59), (14.25, 56.7), (14.82, 57.27), (14.82, 57.3)]),
    ("OC_G", [(14.75, 55.59), (14.75, 55.95), (15.05, 56.25), (16.1, 56.25)]),
    ("GND", [(15.49, 53.35), (14.6, 53.35)]),
    ("3V3M", [(15.49, 53.85), (17.12, 53.85)]),
    ("NRST", [(15.49, 54.35), (16.3, 54.35), (16.9, 54.95), (18.51, 54.95), (18.51, 55.9)]),
    ("BAL1", [(15.49, 52.85), (21.8, 52.85)]),
    ("BAL2", [(15.49, 52.35), (22.72, 52.35), (22.72, 51.4)]),
]
# + 4 GND vias in the exposed pad of U4 (its only way to the B.Cu ground plane)
MCU_VIAS = [("SH_N", (8.2, 52.85)), ("SH_N", (13.25, 68.5)), ("OC_G", (16.1, 56.25)),
            ("SWDIO", (8.0, 57.4)), ("SWCLK", (9.5, 58.3)),
            ("GND", (12.8, 53.1)), ("GND", (13.7, 53.1)), ("GND", (12.8, 54.1)), ("GND", (13.7, 54.1))]
def add_mcu_stubs(board):
    for st in MCU_STUBS:
        name, pts = st[0], st[1]
        layer = pcbnew.B_Cu if len(st) > 2 and st[2] == "B" else pcbnew.F_Cu
        n = board.FindNet(name)
        for p0, p1 in zip(pts, pts[1:]):
            t = pcbnew.PCB_TRACK(board); t.SetStart(pt(*p0)); t.SetEnd(pt(*p1)); t.SetWidth(mm(0.2))
            t.SetLayer(layer); t.SetNet(n); t.SetLocked(True); board.Add(t)
    for name, at in MCU_VIAS:
        v = pcbnew.PCB_VIA(board); v.SetPosition(pt(*at)); v.SetWidth(mm(0.6)); v.SetDrill(mm(0.3))
        v.SetNet(board.FindNet(name)); v.SetLocked(True)
        if name == "GND":   # in the EP of U4: tented from below so no solder runs out
            v.SetBackTentingMode(pcbnew.TENTING_MODE_TENTED)
        board.Add(v)
# hand routes kept from v0.2 (TPS63070 control pins with the R31 move, VSEL / FB2 B.Cu hops,
# the C31 / C35 GND vias, REGN / TS at the BQ25798): fixed wiring, board mm
HAND_ROUTES = os.path.join(HERE, "hand_routes.json")
def add_hand_routes(board):
    for o in json.load(open(HAND_ROUTES)):
        n = board.FindNet(o["net"])
        if "via" in o:
            at = pt(*o["via"])
            if any(x.GetClass() == "PCB_VIA" and (x.GetPosition().x, x.GetPosition().y) == (at.x, at.y)
                   for x in board.GetTracks()):
                continue
            v = pcbnew.PCB_VIA(board); v.SetPosition(pt(*o["via"])); v.SetWidth(mm(o["w"]))
            v.SetDrill(mm(0.4 if o["w"] >= 0.8 else 0.3)); v.SetNet(n); v.SetLocked(True); board.Add(v)
        else:
            a_, b_ = pt(*o["a"]), pt(*o["b"])
            want = {(a_.x, a_.y), (b_.x, b_.y)}
            if any(x.GetClass() == "PCB_TRACK" and x.GetNetname() == o["net"]
                   and {(x.GetStart().x, x.GetStart().y), (x.GetEnd().x, x.GetEnd().y)} == want
                   for x in board.GetTracks()):
                continue
            t = pcbnew.PCB_TRACK(board); t.SetStart(pt(*o["a"])); t.SetEnd(pt(*o["b"])); t.SetWidth(mm(o["w"]))
            t.SetLayer(pcbnew.F_Cu if o["layer"] == "F" else pcbnew.B_Cu); t.SetNet(n); t.SetLocked(True); board.Add(t)
GND_TIES = {"R32", "C31", "C35", "R3", "JP4"}
FINE = {"REG_EN", "VSEL", "FB", "FB2", "LOGIC_HI", "REGN", "TS"}

if __name__ == "__main__":
    write_project()
    b = build()
    b.Save(PCB)
    import patch2
    b = pcbnew.LoadBoard(PCB)
    # the pre-routes keep B.Cu free where Freerouting has to (B_CLEAR + the charger core)
    import route
    KB = [(x0 - OX, y0 - OY, x1 - OX, y1 - OY) for x0, y0, x1, y1 in route.b_clear(b)]
    def pad_mm(ref, num):
        if isinstance(ref, float):
            return (ref, num)
        p = charger._pad(b, ref, num).GetPosition()
        return (pcbnew.ToMM(p.x) - OX, pcbnew.ToMM(p.y) - OY)
    print(patch2.route_net(b, "SYS", SYS_RUN, width=0.8, lock=True, via_w=0.8, layer_cost=(1.6, 1.0),
                           keepout=[tps(6.8, 11.2) + tps(14.0, 19.0)], keepout_b=KB))
    # solar input: SS54 cathode -> TVS -> the charger's VBUS copper, short and on F.Cu
    for a_, b_ in ((("D1", "1"), ("D3", "1")), (("D3", "1"), (9.8, 28.5))):
        print(patch2.route_net(b, "VBUS", (pad_mm(*a_), pad_mm(*b_)), width=0.8, lock=True,
                               via_w=0.8, layer_cost=(1.0, 8.0)))
    print(patch2.route_net(b, "SOLAR_IN", (pad_mm("J1", "1"), pad_mm("D1", "2")), width=0.8, lock=True,
                           via_w=0.8, layer_cost=(1.0, 8.0)))
    add_hand_routes(b)
    add_mcu_stubs(b)
    for net_, a_, b_ in PRE_ROUTES:
        try:
            fine = net_ in FINE or (net_ == "SYS" and a_[0] == "R30")
            mcu = "U4" in (a_[0], b_[0]) or isinstance(a_[0], float) or net_ == "BAL2" or (net_ == "SYS" and a_[0] == "C21")
            print(patch2.route_net(b, net_, (pad_mm(*a_), pad_mm(*b_)), lock=True,
                                   layer_cost=(1.0, 1.0) if mcu else (1.0, 1.5) if fine else (1.3, 1.0),
                                   width=0.2 if fine or mcu else 0.25,
                                   clear=0.15 if mcu else 0.145 if fine else 0.2,
                                   keepout_b=KB))
        except RuntimeError as e:
            print(e)
    # GND pads of the small parts get their own tie via now, while there is room: after
    # routing, the pour leaves them as islands between the tracks (tools/gndfix.py notes)
    ref_pt = (27.0, 30.0)   # plain B.Cu GND (right strip next to the charger)
    for fp in list(b.GetFootprints()):
        ref = fp.GetReference()
        for p in fp.Pads():
            if p.GetNetname() != "GND" or p.GetAttribute() != pcbnew.PAD_ATTRIB_SMD:
                continue
            x, y = pad_mm(ref, p.GetNumber())
            if ref[0] not in "RCUQ" and ref not in GND_TIES:
                continue
            try:
                patch2.route_net(b, "GND", ((x, y), ref_pt), width=0.3, lock=True, heur=False,
                                 start_layers=(0,), window=2.0, via_in_small_cy=True, target_layers=(1,), start_pad_only=True)
            except RuntimeError as e:
                print("GND tie %s.%s: %s" % (ref, p.GetNumber(), e))
    b.Save(PCB)
    write_project()
    print("wrote", PCB)
