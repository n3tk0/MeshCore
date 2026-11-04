#!/usr/bin/env python3
"""Builds solar-smart-mppt.kicad_pcb (27 x 83 mm): placement, outline, net classes, GND pours.
Top: outputs, TPS63070, LTC4412, LEDs. Then the charger part of xiao-solar-lite (charger
cluster of ../xiao-solar/tools/charger.py, turned), then the ATtiny3226 band and at the
bottom the BMS it drives (most ATtiny pins go to the BMS), input diodes and connectors.

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
W, H = 27.0, 83.0   # 4 mm right strip: routing channel for the long ATtiny lines
DY = 0.0    # the xiao-solar-lite charger part sits where it is on lite, under the new top part
DA = 32.0   # ATtiny band: right under the charger, next to the BMS it drives
DB = 18.0   # BMS, input diodes and the bottom connectors: under the ATtiny band
base.FPLIB_LOCAL = os.path.join(ROOT, "lib", "xiao_solar.pretty")

CHARGER_FRAME = charger.Frame(11.0, 32.8 + DY, 270)
PLACE = charger.place(CHARGER_FRAME, set(charger.PLACE))
LITE = {
    "J3": (2.1, 39.2, 0), "JP3": (7.1, 43.2, 90),
    "D3": (4.8, 50.5, 180), "D1": (4.5, 54.8, 180),
}
PLACE.update({k: (x, y + (DB if k in ("D3", "D1") else DY), r) for k, (x, y, r) in LITE.items()})
PLACE.update({
    # one header row on top: J6 OUT (+ -), J7 U (+ -), J5 host I2C (SCL SDA 3V3 GND)
    "J6": (2.6, 3.0, 90), "J7": (7.68, 3.0, 90), "J5": (12.76, 3.0, 90),
    # LTC4412 ideal diode under J6: Q5 source straight up to J6 pin 1
    "Q5": (2.6, 6.7, 0), "U7": (2.4, 10.5, 0), "C20": (2.4, 13.6, 0),
    # TPS63070 under J7: inductor above the IC (TI layout example), VIN caps left, VOUT caps right
    "U6": (10.6, 11.4, 0), "L2": (10.6, 7.4, 0),
    "C31": (8.0, 11.7, 270), "C32": (6.1, 8.0, 90), "C33": (6.1, 11.7, 270),
    "C35": (13.6, 11.4, 270), "C36": (15.1, 7.4, 90), "C37": (17.1, 7.4, 90), "C38": (19.1, 7.4, 90),
    "C34": (10.85, 13.8, 270), "R33": (15.0, 13.7, 0), "R34": (15.0, 15.4, 0), "R35": (18.1, 13.7, 0),
    "R30": (4.65, 14.9, 270), "R31": (8.0, 14.3, 90), "R32": (12.7, 13.9, 90),
    # EN / VSEL jumpers of J7 under the TPS63070 VIN caps
    "JP5": (2.75, 17.4, 0), "JP6": (6.6, 17.2, 0),
    # LEDs top right, visible next to the I2C header
    "D6": (21.25, 6.9, 90), "R16": (21.25, 9.6, 90), "D7": (21.25, 12.9, 90), "R17": (19.8, 11.7, 90),
    # ATtiny3226 (SOIC-20W) upright: pins 1-10 on the left (VDD, ADC, BMS), 11-20 on the right
    "U4": (13.4, 24.0 + DA, 270),
    "C28": (6.0, 18.4 + DA, 0), "C29": (6.0, 19.4 + DA, 0),
    # shunt sense: SH_N (pin 4) pair above the SH_P (pin 5) pair, the SH pads all on the right
    "R24": (6.0, 21.9 + DA, 0), "C25": (6.0, 22.9 + DA, 180), "R23": (6.0, 24.0 + DA, 0), "C24": (6.0, 25.0 + DA, 180),
    "R25": (1.6, 20.6 + DA, 90), "R26": (2.8, 20.6 + DA, 90), "C26": (4.0, 20.6 + DA, 90),
    "R27": (1.6, 24.6 + DA, 90), "R28": (2.8, 24.6 + DA, 90), "C27": (4.0, 24.6 + DA, 90),
    "U5": (3.6, 30.6 + DA, 0), "C21": (2.2, 27.3 + DA, 0), "C22": (5.6, 27.3 + DA, 0),
    "R13": (20.4, 18.8 + DA, 0), "R14": (20.4, 19.8 + DA, 0), "TP15": (19.6, 32.2 + DA, 0),
    "JP4": (20.9, 24.0 + DA, 90), "R29": (20.4, 27.0 + DA, 180), "R36": (20.4, 28.0 + DA, 0), "C30": (20.4, 29.0 + DA, 0),
    "TP11": (13.0, 32.2 + DA, 0), "TP12": (15.2, 32.2 + DA, 0), "TP13": (17.4, 32.2 + DA, 0),
    # BMS bottom right: Q3 in BATT-, shunt, its Kelvin taps, OD driver
    "Q3": (19.6, 56.2 + DB, 0), "RS1": (14.0, 57.6 + DB, 180), "R21": (16.95, 56.0 + DB, 90), "R22": (11.0, 56.0 + DB, 90),
    "Q6": (16.5, 51.4 + DB, 0), "R18": (14.6, 48.8 + DB, 0), "R19": (19.3, 52.9 + DB, 90), "R20": (22.0, 55.4 + DB, 90),
    # 2S balancing in the free strip right of the SYS caps
    "R37": (21.2, 40.2 + DY, 90), "R41": (21.2, 45.0 + DY, 90), "Q7": (21.2, 49.4 + DB, 0),
    "R38": (20.3, 52.6 + DB, 90), "R39": (21.6, 52.6 + DB, 90), "Q8": (12.0, 50.6 + DB, 0), "R40": (9.6, 50.0 + DB, 90),
    # connectors along the bottom edge (pin 1 left): J1 SOLAR, J4 BATT+ BM BATT-
    "J1": (2.1, 62.0 + DB, 90), "J4": (15.4, 62.0 + DB, 90),
})
# Bring-up test points: net -> (region x0, y0, x1, y1, preferred x, y)
TEST_POINTS = {
    "VBUS": (8.0, 25.0 + DY, 13.0, 32.0 + DY, 9.5, 27.0 + DY), "PMID": (13.0, 21.0 + DY, 17.6, 33.0 + DY, 15.0, 23.0 + DY),
    "SYS": (13.0, 33.0 + DY, 17.6, 49.0 + DY, 15.0, 47.5 + DY), "BAT": (10.0, 35.0 + DY, 13.5, 47.0 + DY, 11.8, 45.5 + DY),
    "GND": (0.8, 15.0 + DA, 22.2, 34.0 + DA, 7.0, 32.6 + DA), "3V3M": (0.8, 15.0 + DA, 22.2, 34.0 + DA, 9.2, 32.6 + DA),
    "SCL_I": (0.8, 15.0 + DA, 22.2, 34.0 + DA, 20.6, 16.6 + DA), "SDA_I": (0.8, 15.0 + DA, 22.2, 34.0 + DA, 18.4, 16.6 + DA),
    "VREG": (11.0, 4.6, 22.2, 11.0, 20.8, 6.0),
}
FIDUCIALS = [
    (0.8, 20.0 + DY, 12.0, 38.0 + DY, 3.5, 26.5 + DY),
    (0.8, 4.5, 22.2, 19.5, 21.0, 18.5),
    (0.8, 47.0 + DB, 22.2, 60.0 + DB, 9.6, 53.7 + DB),
]
B_CLEAR = []
CHARGER_B_CLEAR = (-4.5, -10.4, 4.5, 1.0)

NETCLASSES = {
    "Power": (0.6, 0.15, 0.8, 0.4, ["BATT+", "BATT-", "BATT_S", "FS_D", "SOLAR_IN", "VOUT", "VBUS", "SYS",
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
        charger._track(board, pt(*a_), pt(*b_), g, 0.3)
    for v in TPS_GND_VIAS:
        charger._via(board, pt(*v), g)
    # VAUX (3) straight down into its cap
    charger._track(board, pt(10.85, 12.36), pt(10.85, 13.32), net("VAUX"), 0.25)
    for fp in board.GetFootprints():
        if fp.GetReference() in ("U6", "L2", "C31", "C32", "C33", "C35", "C36", "C37", "C38", "C34"):
            fp.SetZoneConnection(pcbnew.ZONE_CONNECTION_FULL)

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
    # VBUS from the input diodes up into the VBUS copper: one wide B.Cu run, two vias at
    # each end (the autorouter made three single-via layer changes of it)
    vbus = net("VBUS")
    top, bot = [pt(8.75, 30.0 + DY), pt(8.75, 30.8 + DY)], [pt(8.75, 49.2 + DB), pt(8.75, 50.1 + DB)]
    run = top + [pt(6.7, 30.8 + DY), pt(6.7, 49.2 + DB)] + bot
    for a_, b_ in zip(run, run[1:]):
        charger._track(board, a_, b_, vbus, 0.8, pcbnew.B_Cu)
    d3 = charger._pad(board, "D3", "1").GetPosition()
    d1 = charger._pad(board, "D1", "1").GetPosition()
    for v in top + bot:
        charger._via(board, v, vbus)
    for v in bot:
        charger._track(board, v, d3, vbus, 0.8)
    charger._track(board, d3, d1, vbus, 0.8)
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
    def text(s, x, y, size=0.8, angle=0):
        t = pcbnew.PCB_TEXT(board)
        t.SetText(s); t.SetPosition(pt(x, y)); t.SetLayer(pcbnew.F_SilkS)
        t.SetTextSize(pcbnew.VECTOR2I(mm(size), mm(size))); t.SetTextThickness(mm(0.12))
        t.SetTextAngleDegrees(angle)
        board.Add(t)
    # one label per pin, next to the pin
    for ref, names, y in (("J1", ("SOLAR",), H - 0.9), ("J4", ("BAT+", "BM", "BAT-"), H - 0.9),
                          ("J6", ("OUT",), 0.9), ("J7", ("U",), 0.9), ("J5", ("SCL", "SDA", "3V3", "GND"), 0.9)):
        p1 = charger._pad(board, ref, "1").GetPosition()
        x1 = pcbnew.ToMM(p1.x) - OX
        if len(names) == 1:
            text(names[0], x1 + 1.27, y)
        else:
            for i, s_ in enumerate(names):
                text(s_, x1 + 2.54 * i, y, 0.7)
    text("NTC", 2.1, 37.3 + DY)
    text("solar-smart-mppt v0.1", 11.5, 22.2 + DY, 0.7)
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
SYS_RUN = ((17.05, 38.15), (5.8, 9.0))   # kept out from under the TPS63070 (its small signals)
# the long ATtiny signals (across the charger or the BMS) go in first the same way, locked;
# Freerouting closes them badly or not at all on this narrow board
PRE_ROUTES = [("SYS", ("Q5", "3"), (5.8, 9.0)), ("SYS", ("R30", "1"), ("C33", "1")),
              ("SDA_E", ("U4", "10"), ("J5", "2")), ("SCL_E", ("U4", "11"), ("J5", "1")),
              ("LED", ("U4", "15"), ("D7", "1")), ("REG_CTL", ("U4", "19"), ("JP5", "3")),
              # TPS63070 control pins: 0.5 mm pitch, fanned out before anything else gets there
              ("REG_EN", ("U6", "14"), ("R31", "1")), ("VSEL", ("U6", "15"), ("JP6", "2")),
              ("FB", ("U6", "5"), ("R34", "1")), ("FB2", ("U6", "6"), ("R35", "2")),
              ("LOGIC_HI", ("U6", "1"), ("R30", "2")),
              ("REG_EN", ("R31", "1"), ("JP5", "2")), ("VSEL", ("JP6", "2"), ("R32", "1")),
              ("LOGIC_HI", ("R30", "2"), ("JP6", "1")), ("LOGIC_HI", ("JP6", "1"), ("JP5", "1")),
              ("FB", ("R34", "1"), ("R33", "2")), ("FB", ("R33", "2"), ("R35", "1"))]
FINE = {"REG_EN", "VSEL", "FB", "FB2", "LOGIC_HI"}

if __name__ == "__main__":
    write_project()
    b = build()
    b.Save(PCB)
    import patch2
    b = pcbnew.LoadBoard(PCB)
    print(patch2.route_net(b, "SYS", SYS_RUN, width=0.8, lock=True, via_w=0.8, layer_cost=(1.6, 1.0),
                           keepout=[(6.8, 11.2, 14.0, 19.0)]))
    def pad_mm(ref, num):
        if isinstance(ref, float):
            return (ref, num)
        p = charger._pad(b, ref, num).GetPosition()
        return (pcbnew.ToMM(p.x) - OX, pcbnew.ToMM(p.y) - OY)
    for net_, a_, b_ in PRE_ROUTES:
        try:
            fine = net_ in FINE or (net_ == "SYS" and a_[0] == "R30")
            print(patch2.route_net(b, net_, (pad_mm(*a_), pad_mm(*b_)), lock=True,
                                   layer_cost=(1.0, 1.5) if fine else (1.3, 1.0),
                                   width=0.2 if fine else 0.25, clear=0.13 if fine else 0.2))
        except RuntimeError as e:
            print(e)
    b.Save(PCB)
    write_project()
    print("wrote", PCB)
