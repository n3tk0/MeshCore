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
import charger  # noqa: E402
mm, pt, OX, OY = base.mm, base.pt, base.OX, base.OY

PROJECT = "xiao-solar-lite"
PCB = os.path.join(ROOT, PROJECT + ".kicad_pcb")
W, H = 23.0, 57.0

# Charger cluster (../xiao-solar/tools/charger.py) turned by 270 degrees so its long side
# runs down the board: inductor to the right, CE/I2C pins to the left, PMID end under the
# stack, SYS/BAT end towards the connectors.
CHARGER_FRAME = charger.Frame(11.0, 32.8, 270)
PLACE = charger.place(CHARGER_FRAME, set(charger.PLACE))
PLACE.update({
    # XIAO + Wio stack on top, USB end at the board edge; only flat parts under it
    "U2": (11.5, 10.5, 0), "R11": (11.5, 13.5, 0), "R12": (11.5, 15.5, 0),
    # output diode + cap right of the SYS caps, fed from the SYS copper between C12 and C13
    "D5": (21.4, 42.45, 90), "C20": (21.4, 46.6, 270),
    # input diode and TVS below the charger, connectors along the bottom edge (pin 1 left)
    "D1": (3.7, 50.6, 180), "D3": (11.8, 50.7, 0),
    "J1": (2.3, 54.5, 90), "J5": (7.8, 54.5, 90), "J4": (18.2, 54.5, 90),
})

NETCLASSES = {
    "Power": (0.6, 0.15, 0.8, 0.4, ["BATT+", "SOLAR_IN", "VOUT", "VBUS", "SYS", "BAT", "PMID"]),
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
    ds.m_CopperEdgeClearance = mm(0.3)
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
    for p in gen_sch.parts:
        ref = p["ref"]
        if ref.startswith("#"):
            continue
        if ref not in PLACE:
            missing.append(ref)
            continue
        fp = base.load_fp(p["fp"])
        fp.SetReference(ref); fp.SetValue(p["value"]); fp.SetFPIDAsString(p["fp"])
        x, y, rot = PLACE[ref]
        fp.SetPosition(pt(x, y)); fp.SetOrientationDegrees(rot)
        if not p["lcsc"]:
            fp.SetAttributes(fp.GetAttributes() | pcbnew.FP_EXCLUDE_FROM_POS_FILES)
        for pad in fp.Pads():
            n = p["nets"].get(pad.GetNumber())
            if n:
                pad.SetNet(net(n))
        fp.Reference().SetTextSize(pcbnew.VECTOR2I(mm(0.8), mm(0.8)))
        fp.Reference().SetTextThickness(mm(0.12))
        fp.Value().SetVisible(False)
        board.Add(fp)
    if missing:
        raise SystemExit("not placed: " + " ".join(missing))
    charger.preroute(board, CHARGER_FRAME)
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
        ka.SetLayerSet(pcbnew.LSET(pcbnew.F_Cu).AddLayer(pcbnew.B_Cu))
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
    for (x, y) in [(17.0, 40.35), (21.9, 40.35), (21.9, 41.25), (17.0, 41.25)]:
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
    def text(s, x, y, size=0.6, angle=0):
        t = pcbnew.PCB_TEXT(board)
        t.SetText(s); t.SetPosition(pt(x, y)); t.SetLayer(pcbnew.F_SilkS)
        t.SetTextSize(pcbnew.VECTOR2I(mm(size), mm(size))); t.SetTextThickness(mm(0.1))
        t.SetTextAngleDegrees(angle)
        board.Add(t)
    for ref, s in (("J1", "SOL + -"), ("J4", "BAT + -"), ("J5", "SCL SDA 3V3 GND")):
        x, y, _ = PLACE[ref]
        text(s, x + 1.27 * (3 if ref == "J5" else 1), y + 1.75, 0.55)
    text("xiao-solar-lite v0.3", 11.5, 19.5, 0.7)
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
    open(os.path.join(ROOT, PROJECT + ".kicad_pro"), "w").write(json.dumps(pro, indent=2) + "\n")

if __name__ == "__main__":
    write_project()
    b = build()
    b.Save(PCB)
    print("wrote", PCB)
