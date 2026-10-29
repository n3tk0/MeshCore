#!/usr/bin/env python3
"""Builds xiao-solar.kicad_pcb from the part list in gen_sch.py: footprints, nets,
placement, board outline, net classes and GND zones. Routing is done afterwards
by tools/route.sh (Freerouting), so this file only owns placement.

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
W, H = 48.0, 86.0          # board size, mm
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
    "J6": (45.5, 33.5, 0), "J4": (45.5, 43.5, 0), "J3": (45.5, 51.0, 0),
    "J1": (45.5, 70.0, 0), "J2": (45.5, 77.0, 0),
    # I2C / sensor, top right (away from the charger heat)
    "U5": (26.5, 3.0, 0), "C23": (29.5, 3.0, 90), "R11": (33.0, 2.4, 0), "R12": (33.0, 4.4, 0),
    # PWM driver and outputs
    "U6": (29.0, 12.5, 0), "C24": (33.3, 8.0, 90),
    "R13": (34.0, 14.0, 0), "R14": (34.0, 15.8, 0), "Q4": (37.6, 15.5, 0), "D6": (40.8, 15.8, 90),
    "R15": (34.0, 20.0, 0), "R16": (34.0, 21.8, 0), "Q5": (37.6, 21.5, 0), "D7": (40.8, 21.8, 90),
    "R17": (34.0, 26.0, 0), "R18": (34.0, 27.8, 0), "Q6": (37.6, 27.5, 0), "D8": (40.8, 27.8, 90),
    "F1": (27.0, 21.0, 0), "F2": (27.0, 24.5, 0), "F3": (27.0, 28.0, 0),
    "JP1": (27.5, 33.5, 0), "C25": (27.5, 37.0, 0),
    "Q7": (38.5, 35.0, 90), "R19": (34.0, 33.5, 0), "R20": (34.0, 36.5, 0),
    # battery / BMS (bottom right, next to J4)
    "Q2": (39.5, 44.5, 0), "R8": (36.0, 42.2, 0), "R9": (36.0, 47.5, 0), "C19": (32.5, 47.5, 90),
    "U2": (31.5, 43.5, 0), "Q3": (38.5, 49.5, 0), "R10": (34.5, 51.5, 0),
    "R2": (39.5, 54.5, 0), "R3": (39.5, 56.5, 0),
    # charger, power side of the QFN faces up towards the inductor
    "U1": (16.0, 63.0, 0), "L1": (16.0, 55.5, 0),
    "C4": (11.0, 58.0, 0), "C5": (21.0, 58.0, 0),
    "C6": (9.0, 52.5, 90), "C7": (6.5, 52.5, 90), "C8": (4.0, 52.5, 90), "C9": (12.0, 61.0, 90),
    "C11": (23.5, 52.5, 90), "C12": (26.0, 52.5, 90), "C13": (28.5, 52.5, 90), "C14": (23.5, 57.0, 90),
    "C15": (26.0, 57.0, 90), "C16": (20.0, 61.0, 90), "C17": (28.5, 57.0, 90), "C18": (31.0, 57.0, 90),
    "C10": (11.0, 67.5, 0), "R1": (14.0, 69.5, 0), "R4": (20.5, 67.0, 0), "Q1": (24.0, 67.5, 0),
    "R5": (24.0, 71.0, 0), "R6": (6.0, 65.5, 0), "D4": (6.0, 68.0, 0), "R7": (20.5, 69.5, 0),
    "TP1": (27.5, 67.0, 0), "TP2": (30.0, 67.0, 0),
    # inputs (bottom right, next to J1/J2)
    "D1": (37.5, 71.3, 180), "D2": (37.5, 78.3, 180), "D3": (29.5, 75.0, 90),
    "C1": (9.0, 74.5, 90), "C2": (11.5, 74.5, 90), "C3": (14.0, 74.5, 90),
    "H1": (4.0, 82.5, 0), "H2": (24.0, 82.5, 0),
}

NETCLASSES = {
    # name: (track mm, clearance mm, via dia, via drill, nets)
    "Power": (0.8, 0.2, 0.8, 0.4, ["VBUS", "PMID", "SYS", "BAT", "BATT+", "BATT-", "SW1", "SW2",
                                    "SOLAR_IN", "VIN2_IN", "VLOAD", "LOAD_IN", "LOAD_IN_RAW", "FS_D",
                                    "OUT1+", "OUT1-", "OUT2+", "OUT2-", "OUT3+", "OUT3-", "XIAO_5V"]),
    "Supply": (0.4, 0.2, 0.6, 0.3, ["3V3", "REGN", "BTST1", "BTST2", "GND"]),
}

def load_fp(fpid):
    lib, name = fpid.split(":")
    path = FPLIB_LOCAL if lib == "xiao_solar" else os.path.join(FPLIB_SYS, lib + ".pretty")
    fp = pcbnew.FootprintLoad(path, name)
    if fp is None:
        raise KeyError(fpid)
    return fp

def build():
    board = pcbnew.BOARD()
    ds = board.GetDesignSettings()
    ds.SetCopperLayerCount(2)
    ds.m_TrackMinWidth = mm(0.15)
    ds.m_ViasMinSize = mm(0.6)
    ds.m_MinThroughDrill = mm(0.3)
    ds.m_MinClearance = mm(0.15)
    ds.m_HoleClearance = mm(0.25)
    ds.m_CopperEdgeClearance = mm(0.3)
    nc = ds.m_NetSettings.m_DefaultNetClass
    nc.SetTrackWidth(mm(0.25)); nc.SetClearance(mm(0.15)); nc.SetViaDiameter(mm(0.6)); nc.SetViaDrill(mm(0.3))
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
    # outline
    corners = [(0, 0), (W, 0), (W, H), (0, H)]
    for i in range(4):
        s = pcbnew.PCB_SHAPE(board)
        s.SetShape(pcbnew.SHAPE_T_SEGMENT)
        s.SetStart(pt(*corners[i])); s.SetEnd(pt(*corners[(i + 1) % 4]))
        s.SetLayer(pcbnew.Edge_Cuts); s.SetWidth(mm(0.1))
        board.Add(s)
    # GND pour on both layers
    for layer in (pcbnew.F_Cu, pcbnew.B_Cu):
        z = pcbnew.ZONE(board)
        z.SetLayer(layer)
        z.SetNet(net("GND"))
        z.SetLocalClearance(mm(0.25))
        z.SetMinThickness(mm(0.2))
        z.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL)
        z.SetThermalReliefGap(mm(0.3)); z.SetThermalReliefSpokeWidth(mm(0.4))
        ol = z.Outline()
        ol.NewOutline()
        for (x, y) in [(0.3, 0.3), (W - 0.3, 0.3), (W - 0.3, H - 0.3), (0.3, H - 0.3)]:
            ol.Append(mm(OX + x), mm(OY + y))
        board.Add(z)
    # silkscreen labels for the connectors
    labels = {"J5": "SCL SDA 3V3 GND", "J7": "OUT1 heat", "J8": "OUT2 fan", "J9": "OUT3",
              "J6": "LOAD_IN 12V", "J4": "BAT +/-", "J3": "NTC", "J1": "SOLAR +/-", "J2": "VIN2 +/-"}
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
    t.SetPosition(pt(14.0, 85.0)); t.SetLayer(pcbnew.F_SilkS)
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
