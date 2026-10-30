#!/usr/bin/env python3
"""BQ25798 charger cluster shared by xiao-solar and xiao-solar-lite.

Laid out after the BQ25798 layout guide (SLUSDV2C 8.4.1), adapted to two layers with all
parts on the top side:
- SW1/SW2 run from their pins straight up to the inductor as short, wide copper (zones),
- the 100 nF PMID/SYS caps sit right next to the PMID/SYS pins, bulk caps in a row behind them,
- PMID, SYS, VBUS and BAT are copper zones, not autorouted tracks,
- GND pin 27 (between the SW pins), BTST1/2 and REGN leave through vias under the
  package (RQM has no exposed pad, the vias are tented), which keeps the pin rows free for
  the power copper.

Coordinates are mm relative to U1 with the inductor up (-y). A board places the cluster
with a frame (ux, uy, rot): rot is a KiCad rotation in degrees (0 or a multiple of 90).
"""
import math
import pcbnew

def mm(v):
    return pcbnew.FromMM(v)

# ref: (x, y, rotation)
PLACE = {
    "U1": (0, 0, 0), "L1": (0, -7.5, 0),
    # bootstrap caps between the SW copper and a via to the BTST pins
    "C4": (-2.9, -5.65, 90), "C5": (2.9, -5.65, 90),
    # 100 nF PMID / SYS caps at the pins, bulk caps in a row behind them
    "C9": (-4.4, -3.0, 180), "C16": (4.4, -3.0, 0),
    "C6": (-5.0, -7.0, 90), "C7": (-7.4, -7.0, 90), "C8": (-9.8, -7.0, 90),
    "C11": (4.4, -7.0, 90), "C12": (6.8, -7.0, 90), "C13": (9.2, -7.0, 90),
    "C14": (11.6, -7.0, 90), "C15": (14.0, -7.0, 90),
    # VBUS (left pins): 100 nF next to the pins, bulk further out
    "C3": (-3.75, -0.8, 180), "C1": (-7.6, -0.6, 180), "C2": (-7.6, 1.6, 180),
    # BAT caps at pins 22/23 with their GND pads facing each other around one via
    "C17": (5.5, -1.2, 0), "C18": (9.1, -1.2, 180),
    "C10": (-3.4, 3.6, 0), "R1": (6.4, 0.6, 0), "R2": (6.4, 3.8, 180), "R3": (6.4, 5.4, 0),
    "TP2": (-2.4, 5.0, 0), "R4": (-0.5, 7.8, 0), "JP2": (-2.8, 7.8, 90),
    "D4": (-11.0, -0.4, 270), "R6": (-11.0, 2.4, 90),
    "Q2": (13.6, -1.0, 0), "R8": (13.6, 2.0, 180),
}

# Power copper on F.Cu: net -> polygons. The fills keep their clearance to everything else,
# so the outlines may run over other nets' pads.
ZONES = {
    "SW1": [[(-0.62, -2.25), (-0.28, -2.25), (-0.28, -5.75), (-2.3, -5.75), (-2.3, -6.9),
             (-3.35, -6.9), (-3.35, -4.95), (-2.3, -4.95), (-2.3, -3.45), (-0.62, -3.45)]],
    "PMID": [[(-0.79, -2.6), (-0.79, -3.2), (-3.6, -3.2), (-3.6, -6.4), (-11.0, -6.4), (-11.0, -2.6)]],
    "VBUS": [[(-1.9, -1.4), (-6.9, -1.4), (-6.9, 2.6), (-1.45, 2.6), (-1.45, 1.5), (-1.9, 1.5)]],
    "BAT": [[(2.6, -0.3), (11.4, -0.3), (11.4, 0.35), (13.5, 0.35), (13.5, -2.0), (2.6, -2.0)]],
}
ZONES["SW2"] = [[(-x, y) for x, y in ZONES["SW1"][0]][::-1]]
ZONES["SYS"] = [[(-x, y) for x, y in ZONES["PMID"][0][:4]] + [(14.8, -6.4), (14.8, -2.6)]]

# Parts whose GND pads stay solid to the pours (power and HF return paths); the rest of
# the small parts get thermal spokes.
SOLID = {"U1", "C1", "C2", "C3", "C6", "C7", "C8", "C9", "C11", "C12", "C13", "C14", "C15",
         "C16", "C17", "C18"}

# Signal tracks stay out from between the L1 pads on F.Cu and from under L1 on B.Cu.
KEEPOUTS = [(pcbnew.F_Cu, (-0.65, -9.5, 0.65, -5.8)), (pcbnew.B_Cu, (-2.6, -9.8, 2.6, -5.2))]


class Frame:
    def __init__(self, ux, uy, rot=0, ox=100.0, oy=100.0):
        self.ux, self.uy, self.rot, self.ox, self.oy = ux, uy, rot, ox, oy
        a = math.radians(rot)
        self.c, self.s = round(math.cos(a)), round(math.sin(a))

    def vec(self, x, y):
        """Rotates a relative vector the way KiCad rotates footprint coordinates."""
        return (x * self.c + y * self.s, -x * self.s + y * self.c)

    def xy(self, x, y):
        dx, dy = self.vec(x, y)
        return (self.ux + dx, self.uy + dy)

    def pt(self, x, y):
        bx, by = self.xy(x, y)
        return pcbnew.VECTOR2I(mm(self.ox + bx), mm(self.oy + by))

    def off(self, p, x, y):
        """Point at a relative offset from a board point."""
        dx, dy = self.vec(x, y)
        return pcbnew.VECTOR2I(p.x + mm(dx), p.y + mm(dy))


def place(frame, refs):
    """Board placement (x, y, rot) for the cluster parts that exist on this board."""
    out = {}
    for ref, (x, y, a) in PLACE.items():
        if ref in refs:
            bx, by = frame.xy(x, y)
            out[ref] = (bx, by, (a + frame.rot) % 360)
    return out


def _pad(board, ref, num):
    for pad in board.FindFootprintByReference(ref).Pads():
        if pad.GetNumber() == num:
            return pad
    raise KeyError(ref + "." + num)

def _track(board, a, b, net, width, layer=pcbnew.F_Cu):
    t = pcbnew.PCB_TRACK(board)
    t.SetStart(a); t.SetEnd(b); t.SetWidth(mm(width)); t.SetLayer(layer); t.SetNet(net)
    t.SetLocked(True)  # exported as fixed wiring, so the router keeps it
    board.Add(t)

def _path(board, pts, net, width):
    for a, b in zip(pts, pts[1:]):
        _track(board, a, b, net, width)

def _via(board, at, net, dia=0.6, drill=0.3):
    v = pcbnew.PCB_VIA(board)
    v.SetPosition(at); v.SetWidth(mm(dia)); v.SetDrill(mm(drill)); v.SetNet(net)
    v.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
    v.SetLocked(True)
    board.Add(v)


def add_zones(board, frame, priority=1):
    """Power zones (above the GND pours) and the L1 keepouts."""
    for net, polys in ZONES.items():
        n = board.FindNet(net)
        for poly in polys:
            z = pcbnew.ZONE(board)
            z.SetLayer(pcbnew.F_Cu); z.SetNet(n)
            z.SetAssignedPriority(priority)
            z.SetLocalClearance(mm(0.2)); z.SetMinThickness(mm(0.2))
            z.SetPadConnection(pcbnew.ZONE_CONNECTION_FULL)
            z.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_ALWAYS)
            ol = z.Outline(); ol.NewOutline()
            for x, y in poly:
                p = frame.pt(x, y)
                ol.Append(p.x, p.y)
            board.Add(z)
    for layer, (x0, y0, x1, y1) in KEEPOUTS:
        ka = pcbnew.ZONE(board)
        ka.SetIsRuleArea(True); ka.SetDoNotAllowCopperPour(False)
        ka.SetDoNotAllowTracks(True); ka.SetDoNotAllowVias(True); ka.SetDoNotAllowPads(False)
        ka.SetDoNotAllowFootprints(False)
        ka.SetLayer(layer)
        ol = ka.Outline(); ol.NewOutline()
        for x, y in [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]:
            p = frame.pt(x, y)
            ol.Append(p.x, p.y)
        board.Add(ka)
    for fp in board.GetFootprints():
        if fp.GetReference() in SOLID:
            fp.SetZoneConnection(pcbnew.ZONE_CONNECTION_FULL)


def preroute(board, frame):
    """Hand routes at the 0.4 mm pitch QFN; the power copper itself comes from add_zones."""
    U = lambda n: _pad(board, "U1", n)
    gnd = U("27").GetNet()
    # GND pin 27 (between SW1 and SW2) and the pin rows' inner signals drop to vias under
    # the package: GND at the centre, BTST1/REGN on the left, BTST2 on the right.
    # (each leaves its pad straight inwards first, clear of the neighbouring pads)
    for num, inner, via, w in (("27", None, (0.0, -0.6), 0.22), ("4", (-1.45, -0.42), (-0.95, -0.4), 0.2),
                               ("5", (-1.45, -0.02), (-1.0, 0.5), 0.2), ("19", (1.45, 0.38), (1.0, -0.2), 0.2)):
        pad = U(num)
        at = frame.pt(*via)
        pts = [pad.GetPosition()] + ([frame.pt(*inner)] if inner else []) + [at]
        _path(board, pts, pad.GetNet(), w)
        _via(board, at, pad.GetNet())
    # BTST1/2 on B.Cu from those vias to a via just above the bootstrap cap, clear of the
    # PMID/SYS strips (the autorouter would drop its via into them)
    for num, cap, x in (("4", "C4", -2.9), ("19", "C5", 2.9)):
        net = U(num).GetNet()
        start = frame.pt(-0.95 if x < 0 else 1.0, -0.4 if x < 0 else -0.2)
        at = frame.pt(x, -3.9)
        _track(board, start, at, net, 0.3, pcbnew.B_Cu)
        _via(board, at, net)
        _track(board, at, _pad(board, cap, "1").GetPosition(), net, 0.3)
    # BAT pins 22/23 together straight to the first BAT cap (a zone there would be too thin)
    p22, p23 = U("22").GetPosition(), U("23").GetPosition()
    mid = pcbnew.VECTOR2I((p22.x + p23.x) // 2, (p22.y + p23.y) // 2)
    bat = U("22").GetNet()
    _track(board, mid, frame.off(mid, 2.6, 0), bat, 0.6)
    # the C17/C18 GND pads and their via split the BAT copper: the two halves join on B.Cu
    # (two vias in the pin track, two right of C18, 0.8 mm past the GND via's clearance)
    a, b = frame.off(mid, 0.8, 0), frame.off(mid, 1.6, 0)
    pts = [a, b, frame.off(mid, 2.35, 0.82), frame.pt(10.3, -0.2), frame.pt(11.05, -0.9), frame.pt(11.05, -1.6)]
    for p, q in zip(pts, pts[1:]):
        _track(board, p, q, bat, 0.8, pcbnew.B_Cu)
    for at in (a, b, frame.pt(11.05, -0.9), frame.pt(11.05, -1.6)):
        _via(board, at, bat)
    # GND pins 10/11 tied and dropped to a via right below the package
    p10, p11 = U("10"), U("11")
    _track(board, p10.GetPosition(), p11.GetPosition(), gnd, 0.2)
    vg = frame.off(p10.GetPosition(), 0, 1.45)
    _track(board, p10.GetPosition(), vg, gnd, 0.25)
    _via(board, vg, gnd)
    # STAT (corner pin 1) out left above the VBUS copper to the LED; SDRV (corner pin 24)
    # out right above the BAT copper to the ship FET gate
    stat, d4 = U("1"), _pad(board, "D4", "1")
    _path(board, [stat.GetPosition(), frame.pt(-2.2, -2.2), frame.pt(-11.0, -2.2), d4.GetPosition()],
          stat.GetNet(), 0.2)
    sdrv, q2g = U("24"), _pad(board, "Q2", "1")
    q = q2g.GetPosition()
    qx, qy = frame.vec(1, 0)
    corner = frame.pt(2.2, -2.2)
    # run along the rel x axis, then straight to the gate pad
    if frame.c:
        mid = pcbnew.VECTOR2I(q.x, corner.y)
    else:
        mid = pcbnew.VECTOR2I(corner.x, q.y)
    _path(board, [sdrv.GetPosition(), corner, mid, q], sdrv.GetNet(), 0.2)
    # 100 nF caps and the BAT caps: GND pads straight to a via
    for ref, via in (("C9", (-6.2, -3.0)), ("C16", (6.2, -3.0)), ("C3", (-4.525, 0.3))):
        pad = _pad(board, ref, "2")
        at = frame.pt(*via)
        _track(board, pad.GetPosition(), at, pad.GetNet(), 0.4)
        _via(board, at, pad.GetNet())
    at = frame.pt(7.3, -1.2)
    for ref in ("C17", "C18"):
        pad = _pad(board, ref, "2")
        _track(board, pad.GetPosition(), at, pad.GetNet(), 0.5)
    _via(board, at, gnd)
    # QON (12) down past the GND via to TP2, CE (13) to R4
    p12, p13, tp2, r4 = U("12"), U("13"), _pad(board, "TP2", "1"), _pad(board, "R4", "2")
    a12 = p12.GetPosition()
    _path(board, [a12, frame.off(a12, 0, 2.3), frame.off(a12, -1.2, 3.1), tp2.GetPosition()], p12.GetNet(), 0.2)
    a13 = p13.GetPosition()
    _path(board, [a13, frame.off(a13, 0, 5.1), r4.GetPosition()], p13.GetNet(), 0.2)
    # SCL (14) / SDA (15) fan out down-right, clear of CE and the TS corner pad
    for num, pts_ in (("14", [(0, 1.12), (1.6, 2.72)]), ("15", [(0, 0.72), (1.2, 1.92)])):
        pad = U(num)
        _path(board, [pad.GetPosition()] + [frame.off(pad.GetPosition(), dx, dy) for dx, dy in pts_],
              pad.GetNet(), 0.2)
    # PROG (20) straight out to R1
    p20, r1 = U("20"), _pad(board, "R1", "1")
    a20, ar = p20.GetPosition(), r1.GetPosition()
    if frame.c:
        mid = pcbnew.VECTOR2I(ar.x, a20.y)
    else:
        mid = pcbnew.VECTOR2I(a20.x, ar.y)
    _path(board, [a20, mid, ar], p20.GetNet(), 0.2)
    # PMID (29) and SYS (25) up into their copper: the strip between the SW necks and the
    # pin corners is too narrow for the fill alone
    for num, x in (("29", -0.9), ("25", 0.9)):
        pad = U(num)
        _track(board, pad.GetPosition(), frame.pt(x, -2.9), pad.GetNet(), 0.22)
    # the doubled VBUS (2/3) and BAT (22/23) pins
    for a, b in (("2", "3"), ("22", "23")):
        pa, pb = U(a), U(b)
        _track(board, pa.GetPosition(), pb.GetPosition(), pa.GetNet(), 0.2)
