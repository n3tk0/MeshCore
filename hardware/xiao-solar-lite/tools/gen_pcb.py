#!/usr/bin/env python3
"""Builds xiao-solar-lite.kicad_pcb: placement, outline, net classes, GND pours.
The charger cluster is the one of the xiao-solar board (tools/charger.py there), turned.

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
# The main board's pcb generator moved its origin (TOP) and stretches y (ys()); this board uses
# plain coordinates from (100, 100), so neutralise that in the shared module.
base.OY, base.TOP, base.ys = 100.0, 0.0, (lambda y: y)
base.place_test_points.__defaults__ = (base.OX, 100.0)
import charger  # noqa: E402
import dfm  # noqa: E402
mm, pt, OX, OY = base.mm, base.pt, base.OX, base.OY

PROJECT = "xiao-solar-lite"
PCB = os.path.join(ROOT, PROJECT + ".kicad_pcb")
W, H = 23.0, 64.1
# v0.5: every SMD pad >= 3 mm (copper to copper) from every THT pad. The charger cluster and all
# that sits below it moved down by DY (C8 vs XIAO pin 8), the connector row by DY + DJ (Q3 / D1).
DY, DJ = 0.4, 1.7

# Charger cluster (../xiao-solar/tools/charger.py) turned by 270 degrees so its long side
# runs down the board: inductor to the right, CE/I2C pins to the left, PMID end under the
# stack, SYS/BAT end towards the connectors.
CHARGER_FRAME = charger.Frame(11.0, 32.8 + DY, 270)
PLACE = charger.place(CHARGER_FRAME, set(charger.PLACE))
PLACE.update({
    # XIAO + Wio stack on top, USB end at the board edge; only flat parts under it
    "U3": (11.5, 10.5, 0), "R11": (11.5, 17.8, 0), "R12": (11.5, 19.6, 0),
    # discrete ideal diode (as on the xiao-solar main board) in the window between the header
    # rows (SMD pads >= 3 mm from the pins): one column of SOT-23 - Q7 (mirror, VOUT side),
    # Q5 (AO3401A: source VOUT up, drain SYS down, gate to Q7's collector), Q6 (mirror, SYS) -
    # with R30 / R31 beside them and C20 (VOUT) at the top next to U3 pin 14
    "C20": (11.5, 3.0, 0), "Q7": (11.5, 6.0, 270), "Q5": (11.5, 10.2, 270), "Q6": (11.5, 14.2, 90),
    "R30": (9.0, 14.2, 90), "R31": (13.9, 8.5, 90),
    # optional NTC header on the left edge right beside the R2/R3 TS divider
    "J3": (2.1, 39.2 + DY, 0),
    # R2 / R3 (TS divider) 0.9 mm right of the cluster's place: >= 3 mm from the J3 pins
    "R2": (PLACE["R2"][0] + 0.9, PLACE["R2"][1], PLACE["R2"][2]),
    "R3": (PLACE["R3"][0] + 0.9, PLACE["R3"][1], PLACE["R3"][2]),
    # JP3 bridges the ship FET Q2 (closed = no ship FET, for LiFePO4)
    "JP3": (7.1, 43.2 + DY, 90),
    # input diode and TVS on the bottom-left above J1
    "D3": (4.8, 50.5 + DY, 180), "D1": (4.5, 54.8 + DY, 180),
    # DW01A + HJ8205 BMS on the bottom-right between Q2 and J4
    "U2": (16.0, 50.8 + DY, 0), "Q3": (16.0, 55.0 + DY, 0),
    "R9": (11.5, 50.2 + DY, 0), "C19": (11.5, 52.4 + DY, 0), "R10": (11.5, 54.6 + DY, 180),
    # connectors along the bottom edge (pin 1 left)
    "J1": (2.1, 59.0 + DY + DJ, 90), "J5": (7.4, 59.0 + DY + DJ, 90), "J4": (17.9, 59.0 + DY + DJ, 90),
})
# Bring-up test points: net -> (region x0, y0, x1, y1, preferred x, y)
TEST_POINTS = {
    "VBUS": (8.0, 25.0, 13.0, 32.0, 9.5, 27.0), "PMID": (13.0, 21.0, 17.6, 33.0, 15.0, 23.0),
    "SYS": (13.0, 33.0, 17.6, 49.0, 15.0, 47.5), "BAT": (10.0, 35.0, 13.5, 47.0, 11.8, 45.5),
    "GND": (0.8, 21.3, 22.2, 25.0, 3.0, 22.5), "3V3": (0.8, 21.3, 22.2, 25.0, 5.0, 22.5),
    "SCL": (0.8, 21.3, 22.2, 25.0, 7.0, 22.5), "SDA": (0.8, 21.3, 22.2, 25.0, 9.0, 22.5),
}
FIDUCIALS = [
    (0.8, 20.0, 12.0, 38.0, 3.5, 26.5),
    (15.0, 49.0, 22.2, 60.0, 20.8, 54.0),
    (0.8, 40.0, 22.2, 60.0, 20.8, 58.0),
]
B_CLEAR = []
# B.Cu stays whole GND under U1, L1 and the BTST caps only (charger frame, mm): with the whole
# charger kept free (charger.B_CLEAR) the board is too narrow and Freerouting did not finish.
CHARGER_B_CLEAR = (-4.5, -10.4, 4.5, 1.0)

NETCLASSES = {
    "Power": (0.6, 0.15, 0.8, 0.4, ["BATT+", "BATT-", "FS_D", "SOLAR_IN", "VOUT", "VBUS", "SYS", "BAT", "PMID"]),
    # SW nodes are copper zones at the charger; nothing else routes them.
    "PowerQFN": (0.25, 0.13, 0.8, 0.4, ["SW1", "SW2"]),
    "Supply": (0.4, 0.13, 0.6, 0.3, ["3V3", "GND"]),
}

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
        if ref in ("J1", "J3", "J4", "J5"):
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
    top, bot = [pt(8.75, 30.0 + DY), pt(8.75, 30.8 + DY)], [pt(8.75, 49.2 + DY), pt(8.75, 50.1 + DY)]
    run = top + [pt(6.7, 30.8 + DY), pt(6.7, 49.2 + DY)] + bot
    for a_, b_ in zip(run, run[1:]):
        charger._track(board, a_, b_, vbus, 0.8, pcbnew.B_Cu)
    d3 = charger._pad(board, "D3", "1").GetPosition()
    d1 = charger._pad(board, "D1", "1").GetPosition()
    for v in top + bot:
        charger._via(board, v, vbus)
    for v in bot:
        charger._track(board, v, d3, vbus, 0.8)
    charger._track(board, d3, d1, vbus, 0.8)
    # BMS DW_OC / DW_CS and CE up to U3 pin 1
    for n_, lay, pts_, w_ in (
        ("DW_OC", pcbnew.F_Cu, [(16.95, 51.95 + DY), (16.95, 52.3 + DY), (19.0, 52.3 + DY), (19.0, 53.85 + DY), (16.95, 53.85 + DY)], 0.22),
        ("DW_CS", pcbnew.F_Cu, [(16.0, 51.95 + DY), (16.0, 52.75 + DY), (18.0, 52.95 + DY)], 0.22),
        ("DW_CS", pcbnew.B_Cu, [(18.0, 52.95 + DY), (14.85, 56.1 + DY), (13.5, 56.1 + DY)], 0.25),
        ("DW_CS", pcbnew.F_Cu, [(13.5, 56.1 + DY), (12.325, 54.925 + DY), (12.325, 54.6 + DY)], 0.25),
        ("CE", pcbnew.B_Cu, [(4.30, 33.0 + DY), (3.6, 32.3 + DY), (3.6, 25.0), (1.6, 23.0), (1.6, 16.0)], 0.25),
        ("CE", pcbnew.F_Cu, [(1.6, 16.0), (1.6, 3.88), (2.6, 2.88), (3.88, 2.88)], 0.25),
    ):
        for a_, b_ in zip(pts_, pts_[1:]):
            charger._track(board, pt(*a_), pt(*b_), net(n_), w_, lay)
    for n_, vx, vy in (("DW_CS", 18.0, 52.95 + DY), ("DW_CS", 13.5, 56.1 + DY), ("CE", 4.30, 33.0 + DY), ("CE", 1.6, 16.0)):
        charger._via(board, pt(vx, vy), net(n_))
    # Ideal diode power path (as on the main board): SYS 0.8 mm from the end of the SYS copper up
    # the right edge (past L1 and the PMID caps), west below U3 pin 8 and up beside the header
    # pins to the drain of Q5; VOUT 0.6 mm from the source of Q5 over Q7 to C20 and east above
    # the stack to U3 pin 14; the gate Q7 collector -> Q5 stays a short thin track
    def ppos(ref, num):
        p_ = charger._pad(board, ref, num).GetPosition()
        return pcbnew.ToMM(p_.x) - OX, pcbnew.ToMM(p_.y) - 100.0
    sysn, vout, org = net("SYS"), net("VOUT"), net("OR_G")
    q5d, q5s, q5g = ppos("Q5", "3"), ppos("Q5", "2"), ppos("Q5", "1")
    q7s, q7c, c20 = ppos("Q7", "2"), ppos("Q7", "3"), ppos("C20", "1")
    u14 = ppos("U3", "14")
    sys_run = [(20.0, 41.7 + DY + 0.45), (21.9, 41.7 + DY + 0.45), (21.9, 20.6), (16.9, 20.6), (16.9, q5d[1]), q5d]
    for a_, b_ in zip(sys_run, sys_run[1:]):
        charger._track(board, pt(*a_), pt(*b_), sysn, 0.8, pcbnew.F_Cu)
    vout_run = [q5s, q7s, c20, (c20[0], 1.7), (u14[0] - 1.5, 1.7), (u14[0] - 0.5, u14[1]), u14]
    for a_, b_ in zip(vout_run, vout_run[1:]):
        charger._track(board, pt(*a_), pt(*b_), vout, 0.6, pcbnew.F_Cu)
    # the mirror: all hand routed, thin tracks; OR_B crosses to B.Cu to pass the gate / R31 side
    q7b, q6b1, q6b3, q6e = ppos("Q7", "1"), ppos("Q6", "1"), ppos("Q6", "3"), ppos("Q6", "2")
    r31, r30 = ppos("R31", "1"), ppos("R30", "1")
    for run_, net_, w_ in (
        ([q7c, (q7c[0], q5g[1] - 0.7), (q5g[0], q5g[1] - 0.7), q5g, (13.0, r31[1]), r31], org, 0.25),
        ([q6e, (16.9, q6e[1])], sysn, 0.25),
        ([q7b, (13.1, q7b[1] + 1.0)], net("OR_B"), 0.25),
        ([(13.1, q6b3[1] - 0.05), q6b3, (q6b1[0], q6b3[1]), q6b1, (r30[0], r30[1]), ], net("OR_B"), 0.25),
    ):
        for a_, b_ in zip(run_, run_[1:]):
            charger._track(board, pt(*a_), pt(*b_), net_, w_, pcbnew.F_Cu)
    orb = net("OR_B")
    charger._track(board, pt(13.1, q7b[1] + 1.0), pt(13.1, q6b3[1] - 0.05), orb, 0.25, pcbnew.B_Cu)
    charger._via(board, pt(13.1, q7b[1] + 1.0), orb)
    charger._via(board, pt(13.1, q6b3[1] - 0.05), orb)
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
    # SYS copper out between the C12/C13 GND pads to D5
    z = pcbnew.ZONE(board)
    z.SetLayer(pcbnew.F_Cu); z.SetNet(net("SYS")); z.SetAssignedPriority(1)
    z.SetLocalClearance(mm(0.2)); z.SetMinThickness(mm(0.2))
    z.SetPadConnection(pcbnew.ZONE_CONNECTION_FULL)
    z.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_ALWAYS)
    ol = z.Outline(); ol.NewOutline()
    for (x, y) in [(17.0, 41.3 + DY), (20.65, 41.3 + DY), (20.65, 42.2 + DY), (17.0, 42.2 + DY)]:
        ol.Append(mm(OX + x), mm(OY + y))
    board.Add(z)
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
    # no tracks or vias within 1.4 mm of a fiducial (its 2 mm mask opening), Freerouting included
    for fp_ in board.GetFootprints():
        if fp_.GetReference().startswith("FID"):
            c_ = fp_.GetPosition()
            ka = pcbnew.ZONE(board)
            ka.SetIsRuleArea(True); ka.SetDoNotAllowCopperPour(False)
            ka.SetDoNotAllowTracks(True); ka.SetDoNotAllowVias(False); ka.SetDoNotAllowPads(False)
            ka.SetDoNotAllowFootprints(False)
            ka.SetLayerSet(pcbnew.LSET.AllCuMask())
            ol = ka.Outline(); ol.NewOutline()
            for dx_, dy_ in ((-1.4, -1.4), (1.4, -1.4), (1.4, 1.4), (-1.4, 1.4)):
                ol.Append(c_.x + mm(dx_), c_.y + mm(dy_))
            board.Add(ka)
    base.plus_marks(board, ["J1", "J4"], dx=0.0, dy=-1.6)
    charger.pin1_dot(board, CHARGER_FRAME)
    def text(s, x, y, size=0.8, angle=0):
        t = pcbnew.PCB_TEXT(board)
        t.SetText(s); t.SetPosition(pt(x, y)); t.SetLayer(pcbnew.F_SilkS)
        t.SetTextSize(pcbnew.VECTOR2I(mm(size), mm(size))); t.SetTextThickness(mm(0.12))
        t.SetTextAngleDegrees(angle)
        board.Add(t)
    # one label per pin, under the pin
    for ref, names in (("J1", ("SOLAR",)), ("J4", ("BAT",)), ("J5", ("SCL", "SDA", "3V3", "GND"))):
        p1 = charger._pad(board, ref, "1").GetPosition()
        x1 = pcbnew.ToMM(p1.x) - OX
        if len(names) == 1:
            text(names[0], x1 + 1.27, H - 0.9)
        else:
            for i, s in enumerate(names):
                text(s, x1 + 2.54 * i, H - 0.9)
    text("NTC", 2.1, 37.3 + DY)
    text("xiao-solar-lite v0.5", 11.5, 20.65, 0.6)
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

if __name__ == "__main__":
    write_project()
    b = build()
    b.Save(PCB)
    print("wrote", PCB)
