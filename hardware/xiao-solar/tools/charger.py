#!/usr/bin/env python3
"""BQ25798 charger cluster shared by xiao-solar and xiao-solar-lite.

Laid out after the BQ25798 layout guide (SLUSDV2C 8.4.1), adapted to two layers with all
parts on the top side:
- the 0402 100 nF PMID-GND and SYS-GND caps sit right on top of pins 29/27/25 (layout
  guide priority 1), GND pin 27 runs up through both cap GND pads to two GND vias,
- so SW1/SW2 leave their pins inwards to three vias each under the package (priority 2) and
  run on B.Cu as short copper to two vias at each inductor pad,
- BTST1/2 and REGN also drop to vias under the package (RQM has no exposed pad; the vias
  are tented), the bootstrap caps sit next to the inductor pads,
- PMID, SYS, VBUS and BAT are copper zones, not autorouted tracks.

Coordinates are mm relative to U1 with the inductor up (-y). A board places the cluster
with a frame (ux, uy, rot): rot is a KiCad rotation in degrees (0 or a multiple of 90).
"""
import math
import pcbnew

def mm(v):
    return pcbnew.FromMM(v)

# ref: (x, y, rotation)
PLACE = {
    "U1": (0, 0, 0), "L1": (0, -8.0, 0),
    # bootstrap caps beside the inductor pads, fed from a via each
    "C4": (-3.6, -5.65, 90), "C5": (3.6, -5.65, 90),
    # 0402 100 nF on top of the pins: C9 across PMID(29)-GND(27), C16 across SYS(25)-GND(27)
    "C9": (-0.48, -2.75, 0), "C16": (0.48, -3.75, 180),
    # bulk caps in a row behind them, clear of the bootstrap caps' courtyards
    "C6": (-5.5, -7.0, 90), "C7": (-7.9, -7.0, 90), "C8": (-10.3, -7.0, 90),
    "C11": (5.35, -7.0, 90), "C12": (7.75, -7.0, 90), "C13": (10.15, -7.0, 90),
    "C14": (12.55, -7.0, 90), "C15": (14.95, -7.0, 90),
    # VBUS (left pins): 100 nF next to the pins, bulk further out
    "C3": (-3.75, -0.8, 180), "C1": (-7.6, -0.6, 180), "C2": (-7.6, 1.75, 180),
    # BAT caps at pins 22/23 with their GND pads facing each other around one via
    "C17": (5.5, -1.2, 0), "C18": (9.1, -1.2, 180),
    "C10": (-3.4, 3.6, 0), "R1": (6.4, 0.6, 0), "R2": (6.4, 3.8, 180), "R3": (6.4, 5.4, 0),
    "TP2": (-1.3, 5.6, 0), "R4": (-0.5, 7.8, 0), "JP2": (-3.6, 7.65, 90),
    "D4": (-11.0, -0.8, 270), "R6": (-11.0, 2.4, 90),
    "Q2": (13.6, -1.0, 0), "R8": (13.6, 2.0, 180),
}

# Power copper on F.Cu: net -> polygons. The fills keep their clearance to everything else,
# so the outlines may run over other nets' pads.
ZONES = {
    "PMID": [[(-0.68, -2.35), (-0.68, -3.3), (-4.45, -3.3), (-4.45, -6.4), (-11.5, -6.4), (-11.5, -2.35)]],
    "SYS": [[(0.68, -2.35), (0.68, -4.1), (1.3, -4.1), (1.3, -3.3), (4.45, -3.3), (4.45, -6.4), (15.8, -6.4),
             (15.8, -2.6), (4.45, -2.6), (4.45, -2.35)]],
    # SW1 on top only around the inductor pad, the bootstrap cap and the two vias down
    "SW1": [[(-1.1, -4.85), (-2.7, -4.85), (-2.7, -6.1), (-4.0, -6.1), (-4.0, -7.0), (-1.1, -7.0)]],
    "VBUS": [[(-1.9, -1.4), (-6.9, -1.4), (-6.9, 2.6), (-1.45, 2.6), (-1.45, 1.5), (-1.9, 1.5)]],
    "BAT": [[(2.6, -0.3), (11.4, -0.3), (11.4, 0.35), (13.5, 0.35), (13.5, -2.0), (2.6, -2.0)]],
}
ZONES["SW2"] = [[(-x, y) for x, y in ZONES["SW1"][0]][::-1]]
# SW1/SW2 on B.Cu: from the three vias under the package up to the two vias at the inductor
B_ZONES = {"SW1": [[(-0.15, 1.2), (-0.15, -2.3), (-1.0, -4.6), (-1.0, -5.55), (-2.7, -5.55), (-2.7, -4.9),
                    (-1.45, -3.2), (-1.45, -0.75), (-0.7, -0.75), (-0.7, 1.2)]]}
B_ZONES["SW2"] = [[(-x, y) for x, y in B_ZONES["SW1"][0]][::-1]]
SW_VIAS = {"SW1": [(-0.4, -0.75), (-0.4, 0.05), (-0.4, 0.85), (-1.5, -5.2), (-2.3, -5.2)]}
SW_VIAS["SW2"] = [(-x, y) for x, y in SW_VIAS["SW1"]]

# Parts whose GND pads stay solid to the pours (power and HF return paths); the rest of
# the small parts get thermal spokes.
SOLID = {"U1", "C1", "C2", "C3", "C6", "C7", "C8", "C9", "C11", "C12", "C13", "C14", "C15",
         "C16", "C17", "C18"}

# Signal tracks stay out from between the L1 pads on F.Cu and from under L1 on B.Cu.
KEEPOUTS = [(pcbnew.F_Cu, (-0.85, -9.25, 0.85, -6.75)), (pcbnew.B_Cu, (-2.85, -10.35, 2.85, -5.7))]

# B.Cu stays a whole GND plane here (no autorouted copper): the charger core, rel. to U1
B_CLEAR = (-11.5, -10.4, 15.8, 1.0)


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


def zone_poly(frame, net, inset=0.0):
    """The top-layer copper polygon(s) of a charger net on the board, shrunk by `inset` mm."""
    ps = pcbnew.SHAPE_POLY_SET()
    for poly in ZONES.get(net, []):
        ps.NewOutline()
        for x, y in poly:
            p = frame.pt(x, y)
            ps.Append(p.x, p.y)
    if inset:
        ps.Deflate(mm(inset), 16)
    return ps

def pin1_dot(board, frame):
    """A clear pin-1 dot next to the BQ25798's pin-1 corner (the footprint's own is tiny)."""
    u1 = board.FindFootprintByReference("U1")
    c = frame.pt(-2.45, -2.4)
    for g in list(u1.GraphicalItems()):
        # the footprint's own 0.26 mm dot: replaced by ours, at its place
        if g.GetLayer() == pcbnew.F_SilkS and isinstance(g, pcbnew.PCB_SHAPE) and g.GetShape() == pcbnew.SHAPE_T_CIRCLE:
            c = g.GetCenter()
            g.SetLayer(pcbnew.F_Fab)
    s = pcbnew.PCB_SHAPE(board)
    s.SetShape(pcbnew.SHAPE_T_CIRCLE)
    s.SetCenter(c); s.SetEnd(pcbnew.VECTOR2I(c.x + mm(0.12), c.y))
    s.SetWidth(mm(0.24)); s.SetFilled(True); s.SetLayer(pcbnew.F_SilkS)
    board.Add(s)

def add_zones(board, frame, priority=1):
    """Power zones (above the GND pours) and the L1 keepouts."""
    for layer, net, poly in [(l, n, p) for l, zs in ((pcbnew.F_Cu, ZONES), (pcbnew.B_Cu, B_ZONES))
                             for n, ps in zs.items() for p in ps]:
        z = pcbnew.ZONE(board)
        z.SetLayer(layer); z.SetNet(board.FindNet(net))
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
    # SW1 (28) / SW2 (26) leave their pins inwards (0.22 mm between the pins, then 0.5 mm)
    # to three vias each under the package; on B.Cu the SW copper (B_ZONES) takes them up to
    # two vias next to each inductor pad, where the top SW copper (ZONES) meets the pad.
    for net, x in (("SW1", -0.45), ("SW2", 0.45)):
        pad = U("28" if net == "SW1" else "26")
        n = pad.GetNet()
        col = [frame.pt(*v) for v in SW_VIAS[net][:3]]
        _track(board, pad.GetPosition(), frame.pt(x, -0.95), n, 0.22)
        _path(board, [frame.pt(x, -0.95), col[0], col[-1]], n, 0.5)
        for v in SW_VIAS[net]:
            _via(board, frame.pt(*v), n)
    # GND pin 27 runs up through the GND pads of C9 and C16 to two vias above them, so the
    # 100 nF loops PMID-C9-GND(27) and SYS-C16-GND(27) close on the top layer
    p27 = U("27").GetPosition()
    _track(board, p27, frame.pt(0, -2.44), gnd, 0.22)
    _path(board, [frame.pt(0, -2.44), frame.pt(0, -4.55), frame.pt(0, -5.35)], gnd, 0.5)
    for v in ((0, -4.55), (0, -5.35)):
        _via(board, frame.pt(*v), gnd)
    # PMID (29) and SYS (25) straight up into their 100 nF caps (and the zones there)
    _track(board, U("29").GetPosition(), _pad(board, "C9", "1").GetPosition(), U("29").GetNet(), 0.22)
    p25, c16 = U("25").GetPosition(), _pad(board, "C16", "1").GetPosition()
    _path(board, [p25, frame.pt(0.9, -2.44), c16], U("25").GetNet(), 0.25)
    # BTST1 (4), REGN (5) and BTST2 (19) leave their pads inwards to vias under the package
    for num, inner, via in (("4", (-1.45, -0.42), (-1.2, -0.6)), ("5", (-1.45, -0.02), (-1.2, 0.45)),
                            ("19", (1.45, 0.42), (1.2, 0.45))):
        pad = U(num)
        at = frame.pt(*via)
        pts = [pad.GetPosition()] + ([frame.pt(*inner)] if inner else []) + [at]
        _path(board, pts, pad.GetNet(), 0.2)
        _via(board, at, pad.GetNet())
    # REGN on B.Cu out below the package, where the router picks it up
    _track(board, frame.pt(-1.2, 0.45), frame.pt(-1.2, 2.5), U("5").GetNet(), 0.3, pcbnew.B_Cu)
    # BTST1/2 on B.Cu outside the SW copper to a via just below the bootstrap cap
    for num, cap, sx, pts in (("4", "C4", -1, [(-1.2, -0.6), (-1.9, -0.6), (-1.9, -2.6), (-3.6, -3.85)]),
                              ("19", "C5", 1, [(1.2, 0.45), (1.9, -0.25), (1.9, -2.6), (3.6, -3.85)])):
        net = U(num).GetNet()
        for p, q in zip(pts, pts[1:]):
            _track(board, frame.pt(*p), frame.pt(*q), net, 0.3, pcbnew.B_Cu)
        at = frame.pt(*pts[-1])
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
    # VBUS 100 nF and the BAT caps: GND pads straight to a via
    for ref, via in (("C3", (-4.525, 0.3)),):
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
    # the doubled VBUS (2/3) and BAT (22/23) pins
    for a, b in (("2", "3"), ("22", "23")):
        pa, pb = U(a), U(b)
        _track(board, pa.GetPosition(), pb.GetPosition(), pa.GetNet(), 0.2)
