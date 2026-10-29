#!/usr/bin/env python3
"""Builds xiao-solar-lite.kicad_pcb: placement, outline, net classes, GND pours.
The charger cluster around U1 keeps the same relative placement and hand routes as the
xiao-solar board (laid out per the BQ25798 layout guide), only moved and trimmed.

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
_spec.loader.exec_module(base)
mm, pt, OX, OY = base.mm, base.pt, base.OX, base.OY

PROJECT = "xiao-solar-lite"
PCB = os.path.join(ROOT, PROJECT + ".kicad_pcb")
W, H = 30.5, 28.5

UX, UY = 13.0, 12.0  # U1; the parts below are placed relative to it like on xiao-solar
REL = {
    "U1": (0, 0, 0), "L1": (0, -7.5, 0),
    "C9": (-2.6, -3.4, 180), "C16": (2.6, -3.4, 0),
    "C6": (-4.4, -7.0, 90), "C7": (-6.8, -7.0, 90),
    "C11": (4.4, -7.0, 90), "C12": (6.8, -7.0, 90), "C13": (9.2, -7.0, 90),
    "C4": (-6.4, -0.1, 90), "C10": (-4.6, 2.2, 180),
    "C17": (4.6, -1.2, 0), "C18": (7.4, -1.2, 180), "R1": (6.4, 0.6, 0), "C5": (6.4, 2.2, 0),
    "R2": (6.4, 3.8, 180), "R3": (6.4, 5.4, 0), "R4": (-0.5, 7.8, 0), "JP2": (-2.8, 7.8, 90),
    "TP2": (-2.4, 5.0, 0), "R6": (-10.0, 2.5, 0), "D4": (-10.0, 5.0, 0),
    "Q2": (12.0, -1.0, 0), "R8": (12.0, 2.0, 180),
}
PLACE = {r: (UX + x, UY + y, a) for r, (x, y, a) in REL.items()}
PLACE.update({
    # STAT LED up the left edge, clear of the input parts
    "R6": (3.0, 10.5, 0), "D4": (3.0, 13.0, 0),
    # VBUS caps at the VBUS pins (left side of U1), TVS and input diode below them
    "C3": (8.4, 16.6, 90), "C1": (6.0, 17.6, 90), "D3": (3.3, 18.6, 90), "D1": (6.0, 23.2, 180),
    # output diode + cap next to J3, I2C pull-ups next to J5
    "C20": (15.5, 20.0, 0), "D5": (15.0, 23.0, 0), "R11": (24.5, 3.0, 0), "R12": (24.5, 4.8, 0),
    # connectors: power along the bottom edge (pin 1 left), I2C + CE down the right edge
    "J1": (2.5, 26.8, 90), "J3": (11.0, 26.8, 90), "J4": (19.5, 26.8, 90),
    "J5": (28.8, 8.0, 0),
})

NETCLASSES = {
    "Power": (0.6, 0.2, 0.8, 0.4, ["BATT+", "SOLAR_IN", "VOUT"]),
    "PowerQFN": (0.25, 0.13, 0.8, 0.4, ["VBUS", "PMID", "SYS", "BAT", "SW1", "SW2"]),
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
    base.preroute_charger(board)
    corners = [(0, 0), (W, 0), (W, H), (0, H)]
    for i in range(4):
        s = pcbnew.PCB_SHAPE(board)
        s.SetShape(pcbnew.SHAPE_T_SEGMENT)
        s.SetStart(pt(*corners[i])); s.SetEnd(pt(*corners[(i + 1) % 4]))
        s.SetLayer(pcbnew.Edge_Cuts); s.SetWidth(mm(0.1))
        board.Add(s)
    for layer in (pcbnew.F_Cu, pcbnew.B_Cu):
        z = pcbnew.ZONE(board)
        z.SetLayer(layer); z.SetNet(net("GND"))
        z.SetLocalClearance(mm(0.2)); z.SetMinThickness(mm(0.2))
        z.SetPadConnection(pcbnew.ZONE_CONNECTION_THT_THERMAL)
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
    for ref, s in (("J1", "SOLAR + -"), ("J4", "BAT + -"), ("J3", "OUT + -")):
        x, y, _ = PLACE[ref]
        text(s, x + 1.27, y - 1.9)
    x, y, _ = PLACE["J5"]
    text("SCL SDA 3V3 GND CE", x - 1.9, y + 5.08, angle=90)
    text("xiao-solar-lite v0.1", 9.0, 0.9, 0.7)
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
