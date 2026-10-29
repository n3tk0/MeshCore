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

FPLIB_SYS = "/usr/share/kicad/footprints"
FPLIB_LOCAL = os.path.join(ROOT, "lib", "xiao_solar.pretty")
W, H = 48.0, 94.0          # board size, mm
OX, OY = 100.0, 100.0      # board origin on the sheet

def mm(v):
    return pcbnew.FromMM(v)

def pt(x, y):
    return pcbnew.VECTOR2I(mm(OX + x), mm(OY + y))

# ref: (x, y, rotation_deg). Coordinates are mm from the board's top-left corner.
# Columns: XIAO/Wio on the left (USB to the left edge), loads top right,
# charger + battery at the bottom, all connectors in one column at the right edge.
PLACE = {
    # modules, rotated so the USB / pin-1 end faces the left edge
    "U4": (11.5, 12.0, 90), "U3": (11.5, 35.0, 90),
    # between the modules: Xiao 5V feed and 3V3 decoupling
    "D5": (5.5, 23.5, 0), "C20": (9.8, 23.5, 0), "C21": (13.6, 23.5, 0), "C22": (16.8, 23.5, 0),
    # right column connectors (pin 1 at the given point, pins go down)
    "J5": (45.5, 3.5, 0), "J7": (45.5, 14.5, 0), "J8": (45.5, 20.5, 0), "J9": (45.5, 26.5, 0),
    "J4": (45.5, 60.0, 0), "J3": (45.5, 52.0, 0),
    "J1": (45.5, 70.0, 0), "J2": (45.5, 77.0, 0),
    # I2C / sensor, top right (away from the charger heat)
    # SHT40 sits on a tongue cut free by slots (SENSOR_TONGUE), away from the loads
    "U5": (26.0, 2.6, 0), "C23": (28.6, 2.6, 90), "R11": (33.0, 2.4, 0), "R12": (33.0, 4.4, 0),
    # PWM driver and outputs
    "U6": (29.0, 15.5, 180), "C24": (24.0, 19.7, 90),
    "R13": (35.5, 14.0, 0), "R14": (35.5, 15.8, 0), "Q4": (38.8, 15.5, 0), "D6": (41.8, 15.8, 90),
    "R15": (35.5, 20.0, 0), "R16": (35.5, 21.8, 0), "Q5": (38.8, 21.5, 0), "D7": (41.8, 21.8, 90),
    "R17": (35.5, 26.0, 0), "R18": (35.5, 27.8, 0), "Q6": (38.8, 27.5, 0), "D8": (41.8, 27.8, 90),
    "F1": (27.0, 23.5, 0), "F2": (27.0, 27.0, 0), "F3": (27.0, 30.5, 0),
    # load supply filter at the load end, PGND star point NT1 next to the bulk caps
    "FB1": (27.0, 35.0, 0), "C25": (31.0, 34.0, 90), "C26": (33.4, 34.0, 90), "C27": (35.6, 34.0, 90),
    "NT1": (33.4, 37.2, 0),
    "C28": (43.3, 18.5, 90), "C29": (43.3, 24.5, 90), "C30": (43.3, 30.5, 90),
    # battery / BMS (bottom right, next to J4)
    "R9": (35.5, 55.5, 0), "C19": (35.5, 58.0, 90),
    "U2": (39.0, 57.5, 0), "Q3": (39.5, 62.5, 0), "R10": (35.5, 61.5, 0),
    # ship FET and BATP sense next to the charger's BAT pins
    "Q2": (28.0, 62.0, 0), "R8": (28.0, 65.0, 180),
    # charger, laid out per the BQ25798 datasheet layout guide: inductor over SW1/SW2,
    # 100n PMID/SYS caps right at pins 29/25, bulk caps next to them, BAT caps at pins
    # 22/23, BTST/REGN/PROG/TS parts at their pins.
    "U1": (16.0, 63.0, 0), "L1": (16.0, 55.5, 0),
    "C9": (13.4, 59.6, 180), "C16": (18.6, 59.6, 0),
    "C6": (11.6, 56.0, 90), "C7": (9.2, 56.0, 90), "C8": (6.8, 56.0, 90),
    "C11": (20.4, 56.0, 90), "C12": (22.8, 56.0, 90), "C13": (25.2, 56.0, 90),
    "C14": (27.6, 56.0, 90), "C15": (30.0, 56.0, 90),
    "C3": (12.2, 61.9, 180), "C4": (9.6, 62.9, 90), "C10": (11.4, 65.2, 180),
    "C17": (20.6, 61.8, 0), "C18": (23.4, 61.8, 180), "R1": (22.4, 63.6, 0), "C5": (22.4, 65.2, 0),
    "R2": (22.4, 66.8, 180), "R3": (22.4, 68.4, 0), "R4": (16.2, 67.2, 90), "JP2": (16.2, 70.2, 0),
    "R6": (6.0, 65.5, 0), "D4": (6.0, 68.0, 0), "R7": (24.0, 71.5, 0),
    "TP1": (27.5, 68.5, 0), "TP2": (30.0, 68.5, 0),
    # inputs (bottom right, next to J1/J2)
    "D1": (37.5, 71.3, 180), "D2": (37.5, 78.3, 180), "D3": (29.5, 75.0, 90),
    "C1": (9.0, 74.5, 90), "C2": (11.5, 74.5, 90), "C3": (14.0, 74.5, 90),
}
# Everything except the SHT40 tongue moves down by DY to leave the top corners free for
# the mounting holes: 2.8 mm (M2/M2.5) with 6 mm clear for the head, 3.5 mm from the edges.
DY = 6.0
PLACE = {r: (x, y if r in ("U5", "C23") else y + DY, a) for r, (x, y, a) in PLACE.items()}
PLACE.update({"H1": (3.5, 3.5, 0), "H2": (W - 3.5, 3.5, 0), "H3": (3.5, H - 3.5, 0), "H4": (W - 3.5, H - 3.5, 0)})

# SHT40 tongue: x0, y0, x1, y1 (mm). 1 mm slots on the left, right and below it,
# with a 3.4 mm neck in the middle of the bottom slot for SDA/SCL/3V3/GND.
SENSOR_TONGUE = (23.0, 0.0, 30.0, 6.0)
NECK = (24.8, 28.2)
_tx0, _, _tx1, _ty1 = SENSOR_TONGUE
OUTLINE = [(0, 0), (_tx0 - 1, 0), (_tx0 - 1, _ty1 + 1), (NECK[0], _ty1 + 1), (NECK[0], _ty1),
           (_tx0, _ty1), (_tx0, 0), (_tx1, 0), (_tx1, _ty1), (NECK[1], _ty1), (NECK[1], _ty1 + 1),
           (_tx1 + 1, _ty1 + 1), (_tx1 + 1, 0), (W, 0), (W, H), (0, H)]

NETCLASSES = {
    # name: (track mm, clearance mm, via dia, via drill, nets)
    # Power: load and battery paths, 2 A.
    "Power": (0.8, 0.2, 0.8, 0.4, ["BATT+", "BATT-", "FS_D", "SOLAR_IN", "VIN2_IN", "VLOAD", "PGND",
                                    "OUT1+", "OUT1-", "OUT2+", "OUT2-", "OUT3+", "OUT3-", "XIAO_5V"]),
    # Charger nets that end on the 0.4 mm pitch QFN: routed at 0.3 mm, widened by
    # tools/route.py away from U1.
    "PowerQFN": (0.25, 0.13, 0.8, 0.4, ["VBUS", "PMID", "SYS", "BAT", "SW1", "SW2"]),
    "Supply": (0.4, 0.15, 0.6, 0.3, ["3V3", "GND"]),
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
    board.Add(t)

def preroute(board):
    """Hand routes on the 0.4 mm pitch QFN that the autorouter cannot do:
    GND pin 27 sits between SW1 and SW2 (0.45 mm pitch, no room for a via), so it runs
    under the package (RQM has no exposed pad) to the GND pins 10/11 on the other side;
    the doubled VBUS (2/3) and BAT (22/23) pins are tied together."""
    p27, p11 = _pad(board, "U1", "27"), _pad(board, "U1", "11")
    gnd = p27.GetNet()
    a, b = p27.GetPosition(), p11.GetPosition()
    pts = [a, pcbnew.VECTOR2I(a.x, b.y - mm(1.3)), pcbnew.VECTOR2I(b.x, b.y - mm(0.7)), b]
    for s_, e_ in zip(pts, pts[1:]):
        _track(board, s_, e_, gnd, 0.2)
    # GND pins 10/11 tied and dropped to a via right below the package
    p10 = _pad(board, "U1", "10")
    _track(board, p10.GetPosition(), b, gnd, 0.2)
    vg = pcbnew.VECTOR2I(p10.GetPosition().x, p10.GetPosition().y + mm(1.45))
    _track(board, p10.GetPosition(), vg, gnd, 0.25)
    v = pcbnew.PCB_VIA(board)
    v.SetPosition(vg); v.SetWidth(mm(0.6)); v.SetDrill(mm(0.3)); v.SetNet(gnd)
    v.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
    board.Add(v)
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
    # BAT: pins 22/23 straight out to C17, then over the caps to C18 (the GND pads sit
    # between them so the 22 µF stays on one short loop)
    bat = _pad(board, "U1", "22")
    c17, c18 = _pad(board, "C17", "1"), _pad(board, "C18", "1")
    _track(board, bat.GetPosition(), pcbnew.VECTOR2I(c17.GetPosition().x, bat.GetPosition().y), bat.GetNet(), 0.25)
    up = mm(1.2)
    a = pcbnew.VECTOR2I(c17.GetPosition().x, c17.GetPosition().y - up)
    b = pcbnew.VECTOR2I(c18.GetPosition().x, c18.GetPosition().y - up)
    for s, e in ((c17.GetPosition(), a), (a, b), (b, c18.GetPosition())):
        _track(board, s, e, bat.GetNet(), 0.5)
    # BATP (pin 18) is boxed in by the side-pin parts: short stub to a via, B.Cu to R8
    p18 = _pad(board, "U1", "18")
    v18 = pcbnew.VECTOR2I(p18.GetPosition().x + mm(1.0), p18.GetPosition().y)
    _track(board, p18.GetPosition(), v18, p18.GetNet(), 0.2)
    v = pcbnew.PCB_VIA(board)
    v.SetPosition(v18); v.SetWidth(mm(0.6)); v.SetDrill(mm(0.3)); v.SetNet(p18.GetNet())
    v.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
    board.Add(v)
    # PROG (pin 20) to R1 above the BATP via
    p20, r1 = _pad(board, "U1", "20"), _pad(board, "R1", "1")
    pts = [p20.GetPosition(), pcbnew.VECTOR2I(r1.GetPosition().x, p20.GetPosition().y), r1.GetPosition()]
    for s_, e_ in zip(pts, pts[1:]):
        _track(board, s_, e_, p20.GetNet(), 0.2)
    # PCA9685 VDD (28) and A0 (1, tied high) sit at the same end of the two pin rows
    u6_1, u6_28 = _pad(board, "U6", "1"), _pad(board, "U6", "28")
    yb = u6_1.GetPosition().y + mm(0.9)
    pts = [u6_28.GetPosition(), pcbnew.VECTOR2I(u6_28.GetPosition().x, yb),
           pcbnew.VECTOR2I(u6_1.GetPosition().x, yb), u6_1.GetPosition()]
    c24 = _pad(board, "C24", "1")
    pts = [c24.GetPosition(), pcbnew.VECTOR2I(c24.GetPosition().x, yb)] + pts
    for s, e in zip(pts, pts[1:]):
        _track(board, s, e, u6_1.GetNet(), 0.3)
    for a, b in (("2", "3"), ("22", "23")):
        pa, pb = _pad(board, "U1", a), _pad(board, "U1", b)
        _track(board, pa.GetPosition(), pb.GetPosition(), pa.GetNet(), 0.2)

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
        fp = load_fp(p["fp"])
        fp.SetReference(ref)
        fp.SetValue(p["value"])
        fp.SetFPIDAsString(p["fp"])
        x, y, rot = PLACE[ref]
        fp.SetPosition(pt(x, y))
        fp.SetOrientationDegrees(rot)
        if p["lcsc"]:
            fp.SetField("LCSC", p["lcsc"]) if hasattr(fp, "SetField") else None
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
    preroute(board)
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
        z.SetPadConnection(pcbnew.ZONE_CONNECTION_THT_THERMAL)
        z.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_ALWAYS)
        z.SetThermalReliefGap(mm(0.3)); z.SetThermalReliefSpokeWidth(mm(0.4))
        ol = z.Outline()
        ol.NewOutline()
        for (x, y) in [(0.3, 0.3), (W - 0.3, 0.3), (W - 0.3, H - 0.3), (0.3, H - 0.3)]:
            ol.Append(mm(OX + x), mm(OY + y))
        board.Add(z)
    # silkscreen labels for the connectors
    labels = {"J5": "SCL SDA 3V3 GND", "J7": "OUT1 heat", "J8": "OUT2 fan", "J9": "OUT3",
              "J4": "BAT +/-", "J3": "NTC", "J1": "SOLAR +/-", "J2": "VIN2 +/-"}
    for ref, txt in labels.items():
        x, y, _ = PLACE[ref]
        t = pcbnew.PCB_TEXT(board)
        t.SetText(txt)
        t.SetPosition(pt(x - 2.4, y + 1.27 * (3 if ref == "J5" else 1)))
        t.SetLayer(pcbnew.F_SilkS)
        t.SetTextSize(pcbnew.VECTOR2I(mm(0.6), mm(0.6))); t.SetTextThickness(mm(0.1))
        t.SetTextAngleDegrees(90)
        board.Add(t)
    t = pcbnew.PCB_TEXT(board)
    t.SetText("xiao-solar v0.1  n3tk0/MeshCore")
    t.SetPosition(pt(14.0, H - 1.5)); t.SetLayer(pcbnew.F_SilkS)
    t.SetTextSize(pcbnew.VECTOR2I(mm(0.8), mm(0.8))); t.SetTextThickness(mm(0.12))
    board.Add(t)
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

if __name__ == "__main__":
    write_project()
    b = build()
    out = os.path.join(ROOT, "xiao-solar.kicad_pcb")
    b.Save(out)
    print("wrote", out)
