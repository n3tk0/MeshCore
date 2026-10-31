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
FPLIB_LOCAL = os.path.join(ROOT, "lib", "xiao_solar.pretty")
W, H = 48.0, 71.5          # board size, mm
OX, OY = 100.0, 100.0      # board origin on the sheet

def mm(v):
    return pcbnew.FromMM(v)

def pt(x, y):
    return pcbnew.VECTOR2I(mm(OX + x), mm(OY + y))

# ref: (x, y, rotation_deg). Coordinates are mm from the board's top-left corner.
# Columns: XIAO/Wio on the left (USB to the left edge), inputs, BMS and INA219 on the right,
# charger below the modules, all connectors in one column at the right edge.
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
    # INA219 on the battery lead: RS1 between J4 and Q2, U7 Kelvin-sensing its pads
    "RS1": (36.0, 44.0, 180), "U7": (39.5, 48.0, 0), "C24": (41.5, 51.0, 0),
}
# charger cluster (tools/charger.py), U1 at its position before the DY shift below
PLACE.update(charger.place(charger.Frame(16.0, 55.0), set(charger.PLACE)))
# Everything except the SHT40 tongue moves down by DY to leave the top corners free for
# the mounting holes: 2.8 mm (M2/M2.5) with 6 mm clear for the head, 3.5 mm from the edges.
DY = 6.0
CHARGER_FRAME = charger.Frame(16.0, 55.0 + DY)
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

# Bring-up test points: net -> (region x0, y0, x1, y1, preferred x, y). Charger nets sit
# inside their copper zone when there is room; the others are wired by the router.
TEST_POINTS = {
    "VBUS": (9.0, 58.0, 15.0, 64.0, 10.0, 61.5), "PMID": (4.5, 54.0, 12.0, 59.0, 6.0, 58.0),
    "SYS": (20.0, 54.0, 32.0, 59.0, 30.5, 55.5), "BAT": (18.0, 58.5, 30.0, 62.0, 22.0, 61.5),
    "SCL": (35.0, 6.0, 43.0, 14.0, 37.5, 7.5), "SDA": (35.0, 6.0, 43.0, 14.0, 39.5, 7.5),
    "3V3": (35.0, 6.0, 43.0, 14.0, 37.5, 10.0), "GND": (35.0, 6.0, 43.0, 14.0, 39.5, 10.0),
    "BATS": (31.0, 46.0, 42.0, 64.0, 34.0, 52.0),
}
# three fiducials: region x0, y0, x1, y1 and the preferred point
FIDUCIALS = [(6.5, 1.0, 21.0, 6.0, 8.0, 2.5), (32.0, 1.0, 41.0, 6.0, 40.0, 2.5), (6.5, H - 6.0, 26.0, H - 1.0, 8.0, H - 2.0)]
# B.Cu stays whole GND under the Wio (U4) as well as under the charger (charger.B_CLEAR)
# (between its two pin rows only: the Xiao-Wio links need B.Cu at the rows)
B_CLEAR = [(0.7, 11.8, 22.3, 23.8)]

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
    "Power": (0.6, 0.2, 0.8, 0.4, ["BATT+", "BATT-", "BATS", "FS_D", "SOLAR_IN", "VIN2_IN", "XIAO_5V",
                                    "VBUS", "SYS", "BAT", "PMID"]),
    # SW nodes are copper zones at the charger (tools/charger.py); nothing else routes them.
    "PowerQFN": (0.25, 0.13, 0.8, 0.4, ["SW1", "SW2"]),
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
    t.SetLocked(True)  # exported as fixed wiring, so the router keeps it
    board.Add(t)

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
    # INA219 Kelvin sense: thin runs from the inner ends of the RS1 pads to IN+ / IN-
    # (the 0.65 mm pitch pins cannot take the 0.6 mm power class)
    rs_p, rs_n = _pad(board, "RS1", "1").GetPosition(), _pad(board, "RS1", "2").GetPosition()
    inp, inn = _pad(board, "U7", "1").GetPosition(), _pad(board, "U7", "2").GetPosition()
    for net, pts in (("BATT+", [pcbnew.VECTOR2I(rs_p.x, rs_p.y + mm(0.6)), pcbnew.VECTOR2I(rs_p.x, inp.y), inp]),
                     ("BATS", [pcbnew.VECTOR2I(rs_n.x + mm(0.25), rs_n.y + mm(0.6)),
                               pcbnew.VECTOR2I(rs_n.x + mm(0.25), inn.y), inn])):
        for a_, b_ in zip(pts, pts[1:]):
            _track(board, a_, b_, board.FindNet(net), 0.25)
    # SYS to the Xiao: one 0.8 mm run from the right end of the SYS copper (the bulk caps
    # close its top side) up past RS1, across to the channel between the modules and the
    # right column and along it to the LM66100 input
    sys_ = board.FindNet("SYS")
    vin = _pad(board, "D5", "1").GetPosition()
    pts = [pt(31.3, 57.6), pt(32.6, 56.3), pt(32.6, 39.8), pt(23.8, 31.0), pcbnew.VECTOR2I(vin.x, mm(OY + 31.0))]
    for a_, b_ in zip(pts, pts[1:]):
        _track(board, a_, b_, sys_, 0.8)
    _track(board, pts[-1], vin, sys_, 0.3)  # the SC-70 pins are 0.65 mm apart

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
        if ref.startswith("H"):
            fp.Reference().SetVisible(False)
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
    dfm.add_fiducials(board, FIDUCIALS, load_fp)
    plus_marks(board, ["J1", "J2", "J4"], dx=-2.2, dy=0.0)
    # silkscreen labels for the connectors, beside pin 2 so the "+" keeps pin 1
    labels = {"J5": "GND 3V3 SDA SCL", "J4": "BAT", "J3": "NTC", "J1": "SOLAR", "J2": "VIN2"}
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
    t.SetText("xiao-solar v0.4  n3tk0/MeshCore")
    t.SetPosition(pt(26.0, H - 1.5)); t.SetLayer(pcbnew.F_SilkS)
    t.SetTextSize(pcbnew.VECTOR2I(mm(0.8), mm(0.8))); t.SetTextThickness(mm(0.12))
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

if __name__ == "__main__":
    write_project()
    b = build()
    out = os.path.join(ROOT, "xiao-solar.kicad_pcb")
    b.Save(out)
    print("wrote", out)
