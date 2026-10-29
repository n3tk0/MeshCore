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
W, H = 23.0, 53.0

UX, UY = 9.6, 31.5  # U1; the parts below are placed relative to it like on xiao-solar
REL = {
    "U1": (0, 0, 0), "L1": (0, -7.5, 0),
    "C9": (-2.6, -3.4, 180), "C16": (2.6, -3.4, 0),
    "C6": (-4.4, -7.0, 90), "C7": (-6.8, -7.0, 90),
    "C11": (4.4, -7.0, 90), "C12": (6.8, -7.0, 90), "C13": (9.2, -7.0, 90),
    "C4": (-6.4, -0.1, 90), "C10": (-4.6, 2.2, 180),
    "C17": (4.6, -1.2, 0), "C18": (7.4, -1.2, 180), "R1": (6.4, 0.6, 0), "C5": (6.4, 2.2, 0),
    "R2": (6.4, 3.8, 180), "R3": (6.4, 5.4, 0), "R4": (-0.5, 7.8, 0), "JP2": (-2.8, 7.8, 90),
    "TP2": (-2.4, 5.0, 0),
}
PLACE = {r: (UX + x, UY + y, a) for r, (x, y, a) in REL.items()}
PLACE.update({
    # XIAO + Wio stack on top, USB end at the board edge; only flat parts under it
    "U2": (11.5, 10.5, 0), "R11": (11.5, 13.5, 0), "R12": (11.5, 15.5, 0),
    # ship FET and BATP sense in the strip right of the BAT caps
    "Q2": (21.1, 30.5, 90), "R8": (21.1, 34.3, 90),
    # output diode at the SYS caps, cap right below the stack's 5V pin
    "D5": (21.6, 24.2, 90), "C20": (21.6, 20.0, 90),
    # below the charger: VBUS caps, output cap, STAT LED; then input diode and TVS
    "C1": (3.3, 43.4, 0), "C3": (7.3, 43.4, 0),
    "R6": (12.5, 43.4, 0), "D4": (15.7, 43.4, 0),
    "D1": (3.7, 46.9, 180), "D3": (11.5, 47.0, 0),
    # connectors along the bottom edge, pin 1 left; BAT at the right, under the ship FET
    "J1": (2.3, 50.5, 90), "J5": (7.8, 50.5, 90), "J4": (18.2, 50.5, 90),
})

NETCLASSES = {
    "Power": (0.6, 0.15, 0.8, 0.4, ["BATT+", "SOLAR_IN", "VOUT"]),
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
    # keep signal tracks away from the switch node: nothing between the L1 pads on F.Cu
    # (the SW1/SW2 tracks enter that gap only below the pads) and nothing under L1 on B.Cu
    lx, ly = PLACE["L1"][:2]
    for lset, (x0, y0, x1, y1) in ((pcbnew.F_Cu, (lx - 0.65, ly - 2.5, lx + 0.65, ly + 1.6)),
                                   (pcbnew.B_Cu, (lx - 2.6, ly - 2.3, lx + 2.6, ly + 2.3))):
        ka = pcbnew.ZONE(board)
        ka.SetIsRuleArea(True); ka.SetDoNotAllowCopperPour(False)
        ka.SetDoNotAllowTracks(True); ka.SetDoNotAllowVias(True); ka.SetDoNotAllowPads(False)
        ka.SetDoNotAllowFootprints(False)
        ka.SetLayer(lset)
        ol = ka.Outline(); ol.NewOutline()
        for (x, y) in [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]:
            ol.Append(mm(OX + x), mm(OY + y))
        board.Add(ka)
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
    for ref, s in (("J1", "SOL + -"), ("J4", "BAT + -"), ("J5", "SCL SDA 3V3 GND")):
        x, y, _ = PLACE[ref]
        text(s, x + 1.27 * (3 if ref == "J5" else 1), y + 1.75, 0.55)
    text("xiao-solar-lite v0.2", 11.5, 19.5, 0.7)
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
