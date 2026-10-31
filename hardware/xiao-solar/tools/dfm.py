#!/usr/bin/env python3
"""Manufacturing and return-path passes shared by xiao-solar and xiao-solar-lite (v0.4):

- smd_courtyards / in_courtyard: no via inside the courtyard of an SMD part (U1 keeps its
  four tented vias under the package, see charger.py),
- free_spot: places test points and fiducials where nothing else is,
- add_fiducials: three 1 mm fiducials (2 mm mask opening) for JLC assembly,
- gnd_vias: GND stitching (4 mm grid inside, 3 mm fence along the edge), a GND via within
  1 mm of every capacitor's GND pad, one next to every signal via, and at least two vias in
  every F.Cu GND piece,
- teardrops: copper zones of the track's net where a track meets a via or pad,
- report: the numbers the v0.4 review asked for.

Coordinates passed in are board mm from the board's top-left corner (origin ox/oy).
"""
import math
import pcbnew

mm = pcbnew.FromMM

def vec(x, y):
    return pcbnew.VECTOR2I(int(x), int(y))

def dist(a, b):
    return math.hypot(a.x - b.x, a.y - b.y)

# ------------------------------------------------------------------ courtyards

def _is_smd(fp):
    pads = list(fp.Pads())
    return bool(pads) and all(p.GetAttribute() in (pcbnew.PAD_ATTRIB_SMD, pcbnew.PAD_ATTRIB_CONN) for p in pads)

def courtyard(fp):
    """F.CrtYd polygon of a footprint, or its pads' box grown by 0.25 mm when it has none."""
    fp.BuildCourtyardCaches()
    cy = fp.GetCourtyard(pcbnew.F_CrtYd)
    if cy.OutlineCount():
        return cy.CloneDropTriangulation()
    xs, ys = [], []
    for p in fp.Pads():
        b = p.GetBoundingBox()
        xs += [b.GetX(), b.GetRight()]; ys += [b.GetY(), b.GetBottom()]
    g = mm(0.25)
    out = pcbnew.SHAPE_POLY_SET()
    out.NewOutline()
    for x, y in ((min(xs) - g, min(ys) - g), (max(xs) + g, min(ys) - g), (max(xs) + g, max(ys) + g), (min(xs) - g, max(ys) + g)):
        out.Append(int(x), int(y))
    return out

def smd_courtyards(board, skip=("U1",)):
    return [(fp.GetReference(), courtyard(fp)) for fp in board.GetFootprints()
            if _is_smd(fp) and fp.GetReference() not in skip]

# A via may reach 0.2 mm into the 0.25 mm courtyard margin, never over the part itself.
VIA_CY = mm(0.1)

def in_courtyard(cys, p, r=VIA_CY):
    return any(cy.Collide(p, r) for _, cy in cys)

# ------------------------------------------------------------------ placement helpers

def free_spot(board, region, prefer, need, ox=100.0, oy=100.0, inside=None, net=None, step=0.1):
    """First point of the region (x0, y0, x1, y1 in board mm), nearest to `prefer`, whose
    surroundings are free: `need` = (pad/courtyard radius, other-net copper radius) in mm.
    `inside` is a SHAPE_POLY_SET the point must lie in. Returns board mm (x, y) or None."""
    r_cy, r_cu = mm(need[0]), mm(need[1])
    cys = [cy for fp in board.GetFootprints() for cy in [courtyard(fp)]]
    pads = [p for fp in board.GetFootprints() for p in fp.Pads()]
    tracks = [t for t in board.GetTracks()]
    edge = pcbnew.SHAPE_POLY_SET()
    board.GetBoardPolygonOutlines(edge)
    edge.Deflate(r_cy, 16)
    x0, y0, x1, y1 = region
    cands = []
    nx, ny = int((x1 - x0) / step) + 1, int((y1 - y0) / step) + 1
    for i in range(nx):
        for j in range(ny):
            x, y = x0 + i * step, y0 + j * step
            cands.append((math.hypot(x - prefer[0], y - prefer[1]), x, y))
    cands.sort()
    for _, x, y in cands:
        p = vec(mm(ox + x), mm(oy + y))
        if not edge.Contains(p):
            continue
        if inside is not None and not inside.Contains(p):
            continue
        if any(cy.Collide(p, r_cy) for cy in cys):
            continue
        if any(pd.GetEffectivePolygon().Collide(p, r_cy if pd.GetNetname() == net else r_cu) for pd in pads):
            continue
        if any(t.GetNetname() != net and t.GetEffectiveShape(pcbnew.F_Cu).Collide(p, r_cu) for t in tracks):
            continue
        return round(x, 2), round(y, 2)
    return None

def add_fiducials(board, regions, load_fp, ox=100.0, oy=100.0):
    """One fiducial per region (x0, y0, x1, y1, prefer_x, prefer_y): 1 mm copper, 0.65 mm
    clearance, 2 mm mask opening; kept off the BOM and the placement file."""
    placed = []
    for k, (x0, y0, x1, y1, px, py) in enumerate(regions):
        at = free_spot(board, (x0, y0, x1, y1), (px, py), (1.6, 1.6), ox, oy)
        if at is None:
            raise SystemExit("no room for fiducial %d in %s" % (k + 1, (x0, y0, x1, y1)))
        fp = load_fp("Fiducial:Fiducial_1mm_Mask2mm")
        fp.SetReference("FID%d" % (k + 1)); fp.SetValue("Fiducial")
        fp.SetPosition(vec(mm(ox + at[0]), mm(oy + at[1])))
        fp.SetAttributes(fp.GetAttributes() | pcbnew.FP_EXCLUDE_FROM_POS_FILES | pcbnew.FP_EXCLUDE_FROM_BOM | pcbnew.FP_BOARD_ONLY)
        fp.Reference().SetVisible(False); fp.Value().SetVisible(False)
        # the library's 0.5 mm clearance puts the pour right on the edge of the 2 mm mask
        # opening (JLC DFM: "solder mask opening exposing trace"); 0.65 mm keeps it under mask
        for pad in fp.Pads():
            pad.SetLocalClearance(mm(0.65))
        board.Add(fp)
        placed.append(at)
    return placed

# ------------------------------------------------------------------ GND vias

class _Placer:
    def __init__(self, board, free, gnd="GND", skip_cy=("U1",)):
        self.board = board
        self.free = free
        self.gnd = board.FindNet(gnd)
        self.cys = smd_courtyards(board, skip_cy)
        self.fills = {}
        for layer in (pcbnew.F_Cu, pcbnew.B_Cu):
            ps = pcbnew.SHAPE_POLY_SET()
            for z in board.Zones():
                if not z.GetIsRuleArea() and z.GetNetCode() == self.gnd.GetNetCode() and z.IsOnLayer(layer):
                    ps.Append(z.GetFilledPolysList(layer).CloneDropTriangulation())
            ps.Deflate(mm(0.31), 16)
            self.fills[layer] = ps
        self.vias = [t for t in board.GetTracks() if t.GetClass() == "PCB_VIA"]
        self.holes = [(p.GetPosition(), max(p.GetDrillSize().x, p.GetDrillSize().y) // 2)
                      for fp in board.GetFootprints() for p in fp.Pads()
                      if p.GetAttribute() in (pcbnew.PAD_ATTRIB_PTH, pcbnew.PAD_ATTRIB_NPTH)]
        self.added = []

    def gnd_vias(self):
        return [v for v in self.vias if v.GetNetCode() == self.gnd.GetNetCode()]

    def ok(self, p, min_via=mm(1.0), front=True):
        if not self.fills[pcbnew.B_Cu].Contains(p):
            return False
        if front and not self.fills[pcbnew.F_Cu].Contains(p):
            return False
        if any(dist(v.GetPosition(), p) < min_via for v in self.vias):
            return False
        if any(dist(c, p) < r + mm(0.3 + 0.25) for c, r in self.holes):
            return False
        if in_courtyard(self.cys, p):
            return False
        return self.free(self.board, p)

    def add(self, p):
        v = pcbnew.PCB_VIA(self.board)
        v.SetPosition(p); v.SetWidth(mm(0.6)); v.SetDrill(mm(0.3)); v.SetNet(self.gnd)
        v.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
        self.board.Add(v)
        self.vias.append(v); self.added.append(v)
        return v

def _spiral(rings=(0.4, 0.8, 1.2), n=8):
    out = [(0, 0)]
    for r in rings:
        out += [(r * math.cos(2 * math.pi * k / n), r * math.sin(2 * math.pi * k / n)) for k in range(n)]
    return out

def stitch(board, free, ox, oy, w, h, pitch=4.0, fence=3.0, fence_in=1.0):
    """Edge fence every `fence` mm, `fence_in` mm inside the outline, then a `pitch` grid
    inside wherever no GND via is near the grid point already."""
    pl = _Placer(board, free)
    outline = pcbnew.SHAPE_POLY_SET()
    board.GetBoardPolygonOutlines(outline)
    outline.Deflate(mm(fence_in), 16)
    nf = 0
    for i in range(outline.OutlineCount()):
        ol = outline.Outline(i)
        pts = [ol.CPoint(k) for k in range(ol.PointCount())]
        pts.append(pts[0])
        carry = 0.0
        for a, b in zip(pts, pts[1:]):
            L = pcbnew.ToMM(dist(a, b))
            s = carry
            while s < L:
                p = vec(a.x + (b.x - a.x) * s / L, a.y + (b.y - a.y) * s / L)
                if not any(dist(v.GetPosition(), p) < mm(1.5) for v in pl.gnd_vias()):
                    for dx, dy in _spiral((0.3, 0.6)):
                        q = vec(p.x + mm(dx), p.y + mm(dy))
                        if pl.ok(q):
                            pl.add(q); nf += 1
                            break
                s += fence
            carry = s - L
    ng = 0
    y = pitch / 2
    while y < h:
        x = pitch / 2
        while x < w:
            p = vec(mm(ox + x), mm(oy + y))
            if not any(dist(v.GetPosition(), p) < mm(pitch * 0.5) for v in pl.gnd_vias()):
                for dx, dy in _spiral():
                    q = vec(p.x + mm(dx), p.y + mm(dy))
                    if pl.ok(q):
                        pl.add(q); ng += 1
                        break
            x += pitch
        y += pitch
    print("stitching: %d fence + %d grid vias" % (nf, ng))
    return nf + ng

def _seg_clear(board, a, b, width, net, layer, clearance=mm(0.2)):
    seg = pcbnew.SHAPE_SEGMENT(a, b, width)
    for t in board.GetTracks():
        if t.GetNetCode() != net and t.IsOnLayer(layer) and t.GetEffectiveShape(layer).Collide(seg, clearance):
            return False
    for fp in board.GetFootprints():
        for p in fp.Pads():
            if p.GetNetCode() != net and p.IsOnLayer(layer) and p.GetEffectiveShape(layer).Collide(seg, clearance):
                return False
    for z in board.Zones():
        if z.IsOnLayer(layer) and z.GetNetCode() != net:
            if z.GetIsRuleArea():
                if z.GetDoNotAllowTracks() and z.Outline().Collide(seg, 0):
                    return False
            elif z.GetFilledPolysList(layer).Collide(seg, clearance):
                return False
    return True

def cap_vias(board, free, near=1.0, gap=0.6):
    """A GND via within `near` mm of every capacitor GND pad (pad edge to via centre),
    linked to the pad by a 0.4 mm track where the pour alone does not reach it."""
    pl = _Placer(board, free)
    gnd = pl.gnd.GetNetCode()
    added = linked = 0
    missing = []
    for fp in board.GetFootprints():
        if not fp.GetReference().startswith("C"):
            continue
        for pad in fp.Pads():
            if pad.GetNetCode() != gnd or pad.GetAttribute() != pcbnew.PAD_ATTRIB_SMD:
                continue
            poly = pad.GetEffectivePolygon()
            if any(poly.Collide(v.GetPosition(), mm(near)) for v in pl.gnd_vias()):
                continue
            c = pad.GetPosition()
            bb = pad.GetBoundingBox(); bb.Inflate(mm(near))
            cands = []
            for i in range(0, int(pcbnew.ToMM(bb.GetWidth()) / 0.1) + 1):
                for j in range(0, int(pcbnew.ToMM(bb.GetHeight()) / 0.1) + 1):
                    p = vec(bb.GetX() + mm(0.1 * i), bb.GetY() + mm(0.1 * j))
                    if poly.Collide(p, mm(near)) and not poly.Collide(p, mm(gap)):
                        cands.append((dist(p, c), p))
            cands.sort(key=lambda t: t[0])
            for _, p in cands:
                if not pl.ok(p, front=False):
                    continue
                if pl.fills[pcbnew.F_Cu].Contains(p):
                    pl.add(p); added += 1
                    break
                if _seg_clear(board, c, p, mm(0.4), gnd, pcbnew.F_Cu):
                    pl.add(p); added += 1; linked += 1
                    t = pcbnew.PCB_TRACK(board)
                    t.SetStart(c); t.SetEnd(p); t.SetWidth(mm(0.4)); t.SetLayer(pcbnew.F_Cu); t.SetNet(pl.gnd)
                    board.Add(t)
                    break
            else:
                missing.append(fp.GetReference())
    print("capacitor GND vias: %d (%d with a link track)%s" % (added, linked, ", no room: " + " ".join(missing) if missing else ""))
    return added

def signal_return_vias(board, free, signal_nets, near=1.5):
    """A GND via within `near` mm of every via of a signal net (the return current changes
    layer next to the signal)."""
    pl = _Placer(board, free)
    added = 0
    missing = []
    for v in list(pl.vias):
        if v.GetNetname() not in signal_nets:
            continue
        c = v.GetPosition()
        if any(dist(g.GetPosition(), c) <= mm(near) for g in pl.gnd_vias()):
            continue
        for d, k in [(d, k) for d in (1.0, 1.15, 1.3, 1.5) for k in range(16)]:
            a = 2 * math.pi * k / 16
            p = vec(c.x + mm(d) * math.cos(a), c.y + mm(d) * math.sin(a))
            if pl.ok(p, min_via=mm(0.95)):
                pl.add(p); added += 1
                break
        else:
            missing.append("%s@%.1f,%.1f" % (v.GetNetname(), pcbnew.ToMM(c.x) - 100, pcbnew.ToMM(c.y) - 100))
    print("signal return vias: %d%s" % (added, ", no room: " + " ".join(missing) if missing else ""))
    return added

def fragment_vias(board, free, want=2):
    """Every F.Cu GND piece gets `want` anchors (GND vias or THT GND pads)."""
    pl = _Placer(board, free)
    gnd = pl.gnd.GetNetCode()
    z = [z for z in board.Zones() if not z.GetIsRuleArea() and z.GetNetCode() == gnd and z.IsOnLayer(pcbnew.F_Cu)][0]
    front = z.GetFilledPolysList(pcbnew.F_Cu)
    tht = [p.GetPosition() for fp in board.GetFootprints() for p in fp.Pads()
           if p.GetNetCode() == gnd and p.GetAttribute() == pcbnew.PAD_ATTRIB_PTH]
    added, short = 0, []
    for i in range(front.OutlineCount()):
        ol = front.Outline(i)
        anchors = [v.GetPosition() for v in pl.gnd_vias() if ol.PointInside(v.GetPosition())]
        anchors += [p for p in tht if ol.PointInside(p)]
        if len(anchors) >= want:
            continue
        frag = pcbnew.SHAPE_POLY_SET(); frag.AddOutline(ol)
        frag.Deflate(mm(0.31), 16)
        bb = ol.BBox()
        y = bb.GetY()
        while y < bb.GetBottom() and len(anchors) < want:
            x = bb.GetX()
            while x < bb.GetRight() and len(anchors) < want:
                p = vec(x, y)
                if frag.Contains(p) and all(dist(a, p) >= mm(1.5) for a in anchors) and pl.ok(p, front=False):
                    pl.add(p); anchors.append(p); added += 1
                x += mm(0.25)
            y += mm(0.25)
        if len(anchors) < want:
            short.append("%.1f,%.1f(%d)" % (pcbnew.ToMM(bb.Centre().x) - 100, pcbnew.ToMM(bb.Centre().y) - 100, len(anchors)))
    print("fragment vias: %d%s" % (added, ", still under %d: %s" % (want, " ".join(short)) if short else ""))
    return added, short

# ------------------------------------------------------------------ teardrops

TEARDROP = "teardrop"

def remove_teardrops(board):
    for z in list(board.Zones()):
        if z.GetZoneName() == TEARDROP:
            board.Delete(z)

def teardrops(board, skip_refs=("U1",), skip_nets=("GND",), max_len=1.0):
    """Tapered copper (a zone of the track's net) where a track leaves a via or a pad:
    from 80 % of the pad/via width at its edge down to the track width over up to 1 mm."""
    remove_teardrops(board)
    vias = {}
    for t in board.GetTracks():
        if t.GetClass() == "PCB_VIA":
            vias[(t.GetPosition().x, t.GetPosition().y)] = t
    pads = [p for fp in board.GetFootprints() if fp.GetReference() not in skip_refs for p in fp.Pads()
            if p.GetNetCode() > 0]
    n = 0
    for t in list(board.GetTracks()):
        if t.GetClass() != "PCB_TRACK" or t.GetNetname() in skip_nets:
            continue
        layer, w = t.GetLayer(), t.GetWidth()
        for end, other in ((t.GetStart(), t.GetEnd()), (t.GetEnd(), t.GetStart())):
            L = dist(end, other)
            if L < mm(0.3):
                continue
            dx, dy = (other.x - end.x) / L, (other.y - end.y) / L
            v = vias.get((end.x, end.y))
            if v is not None:
                c, R, perp = end, v.GetWidth() / 2, v.GetWidth()
            else:
                pad = next((p for p in pads if p.GetNetCode() == t.GetNetCode() and p.IsOnLayer(layer)
                            and p.GetEffectivePolygon().Contains(end)), None)
                if pad is None or pad.GetEffectivePolygon().Contains(other):
                    continue
                c = pad.GetPosition()
                if pad.GetShape() == pcbnew.PAD_SHAPE_CIRCLE:
                    R = perp = pad.GetSize().x / 2
                    perp = pad.GetSize().x
                else:
                    bb = pad.GetBoundingBox()
                    hw, hh = bb.GetWidth() / 2, bb.GetHeight() / 2
                    R = min(hw / abs(dx) if abs(dx) > 1e-6 else 1e18, hh / abs(dy) if abs(dy) > 1e-6 else 1e18)
                    perp = 2 * (hh if abs(dx) > abs(dy) else hw)
                    # the track has to leave the pad along its axis for a clean teardrop
                    if min(abs(dx), abs(dy)) > 0.15:
                        continue
                if min(pad.GetSize().x, pad.GetSize().y) < mm(0.5):
                    continue
                L = dist(c, other)
                dx, dy = (other.x - c.x) / L, (other.y - c.y) / L
            hgt = 0.8 * perp
            if hgt < 1.25 * w:
                continue
            tl = min(mm(max_len), L - R - mm(0.1))
            if tl < mm(0.25):
                continue
            nx, ny = -dy, dx
            def P(along, side):
                return (c.x + dx * along + nx * side, c.y + dy * along + ny * side)
            pts = [P(R * 0.4, hgt / 2), P(R, hgt / 2), P(R + tl, w / 2), P(R + tl, -w / 2), P(R, -hgt / 2), P(R * 0.4, -hgt / 2)]
            z = pcbnew.ZONE(board)
            z.SetLayer(layer); z.SetNet(t.GetNet()); z.SetZoneName(TEARDROP)
            z.SetAssignedPriority(3)
            z.SetLocalClearance(mm(0.2)); z.SetMinThickness(mm(0.1))
            z.SetPadConnection(pcbnew.ZONE_CONNECTION_FULL)
            z.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_ALWAYS)
            ol = z.Outline(); ol.NewOutline()
            for x, y in pts:
                ol.Append(int(x), int(y))
            board.Add(z)
            n += 1
    print("teardrops:", n)
    return n

# ------------------------------------------------------------------ report

def report(board, ox=100.0, oy=100.0, top=8):
    gnd = board.FindNet("GND").GetNetCode()
    vias = [t for t in board.GetTracks() if t.GetClass() == "PCB_VIA"]
    print("vias: %d (GND %d)" % (len(vias), sum(v.GetNetCode() == gnd for v in vias)))
    cys = smd_courtyards(board)
    bad = ["%s:%s" % (ref, v.GetNetname()) for v in vias for ref, cy in cys if cy.Collide(v.GetPosition(), VIA_CY)]
    u1 = courtyard(board.FindFootprintByReference("U1"))
    n_u1 = sum(u1.Collide(v.GetPosition(), VIA_CY) for v in vias)
    print("vias in SMD courtyards: %d %s (+%d tented under U1)" % (len(bad), " ".join(bad), n_u1))
    lens = {}
    for t in board.GetTracks():
        if t.GetClass() == "PCB_TRACK":
            lens.setdefault((t.GetLayer(), t.GetNetname()), 0.0)
            lens[(t.GetLayer(), t.GetNetname())] += pcbnew.ToMM(t.GetLength())
    for layer, name in ((pcbnew.F_Cu, "F.Cu"), (pcbnew.B_Cu, "B.Cu")):
        print("%s tracks: %.0f mm" % (name, sum(v for (l, _), v in lens.items() if l == layer)))
    longest = sorted(((v, n) for (l, n), v in lens.items() if l == pcbnew.B_Cu), reverse=True)[:top]
    print("B.Cu per net: " + ", ".join("%s %.1f" % (n, v) for v, n in longest))
    for z in board.Zones():
        if not z.GetIsRuleArea() and z.GetNetCode() == gnd:
            layer = z.GetLayer()
            fl = z.GetFilledPolysList(layer)
            pieces = []
            for i in range(fl.OutlineCount()):
                ol = fl.Outline(i)
                pieces.append(sum(ol.PointInside(v.GetPosition()) for v in vias if v.GetNetCode() == gnd))
            print("GND %s: %d pieces, via counts %s" % (board.GetLayerName(layer), len(pieces), sorted(pieces)[:12]))
    edge = board.GetBoardEdgesBoundingBox()
    close = []
    for fp in board.GetFootprints():
        if not _is_smd(fp) or fp.GetReference().startswith("FID"):
            continue
        bb = courtyard(fp).BBox()
        d = min(bb.GetX() - edge.GetX(), edge.GetRight() - bb.GetRight(), bb.GetY() - edge.GetY(), edge.GetBottom() - bb.GetBottom())
        if d < mm(1.0):
            close.append("%s %.2f" % (fp.GetReference(), pcbnew.ToMM(d)))
    print("SMD courtyards under 1 mm from the edge: %s" % (", ".join(close) or "none"))

# ---- silkscreen -------------------------------------------------------------------------------

def _shape_geom(s, grow):
    from shapely.geometry import LineString, Point, box
    w = pcbnew.ToMM(s.GetWidth()) / 2 + grow
    t = s.GetShape()
    if t == pcbnew.SHAPE_T_CIRCLE:
        c, r = s.GetCenter(), pcbnew.ToMM(s.GetRadius())
        p = Point(pcbnew.ToMM(c.x), pcbnew.ToMM(c.y))
        return p.buffer(r + w) if s.IsFilled() else p.buffer(r + w).difference(p.buffer(max(r - w, 0)))
    if t == pcbnew.SHAPE_T_SEGMENT:
        a, b = s.GetStart(), s.GetEnd()
        return LineString([(pcbnew.ToMM(a.x), pcbnew.ToMM(a.y)), (pcbnew.ToMM(b.x), pcbnew.ToMM(b.y))]).buffer(w)
    bb = s.GetBoundingBox()
    return box(pcbnew.ToMM(bb.GetX()), pcbnew.ToMM(bb.GetY()), pcbnew.ToMM(bb.GetRight()),
               pcbnew.ToMM(bb.GetBottom())).buffer(grow)

def _box(bb, grow=0.0):
    from shapely.geometry import box
    return box(pcbnew.ToMM(bb.GetX()) - grow, pcbnew.ToMM(bb.GetY()) - grow,
               pcbnew.ToMM(bb.GetRight()) + grow, pcbnew.ToMM(bb.GetBottom()) + grow)

def place_silk(board, size=0.8, thick=0.12, gap=0.12, edge=0.4, max_move=4.0):
    """Move every F.Cu reference and every board silk text clear of pads, other silk and each other.
    References are set to `size`; one that finds no free spot within max_move is hidden (it stays on
    F.Fab). Returns the hidden references."""
    from shapely.geometry import box
    from shapely.ops import unary_union
    from shapely.strtree import STRtree
    obst = []
    for fp in board.GetFootprints():
        for pad in fp.Pads():
            if pad.IsOnLayer(pcbnew.F_Mask):
                obst.append(_box(pad.GetBoundingBox(), 0.1 + gap))
        for g in fp.GraphicalItems():
            if g.GetLayer() == pcbnew.F_SilkS and isinstance(g, pcbnew.PCB_SHAPE):
                obst.append(_shape_geom(g, gap))
    for d in board.GetDrawings():
        if d.GetLayer() == pcbnew.F_SilkS and isinstance(d, pcbnew.PCB_SHAPE):
            obst.append(_shape_geom(d, gap))
    for t in board.GetTracks():
        # tented, but text over a via reads badly
        if t.GetClass() == "PCB_VIA":
            obst.append(_box(t.GetBoundingBox(), gap))
    for d in board.GetDrawings():
        if d.GetLayer() == pcbnew.Edge_Cuts and isinstance(d, pcbnew.PCB_SHAPE):
            obst.append(_shape_geom(d, edge))
    eb = board.GetBoardEdgesBoundingBox()
    inner = _box(eb, -edge)
    placed = []

    def clear(g):
        if not inner.contains(g):
            return False
        return not any(g.intersects(obst[i]) for i in tree.query(g)) and not any(g.intersects(p) for p in placed)

    def try_at(t, x, y, ang):
        t.SetTextAngleDegrees(ang)
        t.SetPosition(pcbnew.VECTOR2I(mm(x), mm(y)))
        return _box(t.GetBoundingBox())

    items = [(None, d) for d in board.GetDrawings()
             if d.GetLayer() == pcbnew.F_SilkS and isinstance(d, pcbnew.PCB_TEXT)]
    refs = []
    for fp in board.GetFootprints():
        r = fp.Reference()
        if r.GetLayer() != pcbnew.F_SilkS or not r.IsVisible() or fp.GetReference().startswith(("FID", "NT")):
            continue
        r.SetTextSize(pcbnew.VECTOR2I(mm(size), mm(size))); r.SetTextThickness(mm(thick))
        r.SetKeepUpright(True)
        refs.append((fp, r))
    # crowded parts first: fewer options
    refs.sort(key=lambda fr: courtyard(fr[0]).BBox().GetArea())
    tree = STRtree(obst)
    hidden = []
    for fp, t in items + refs:
        p0 = t.GetPosition(); a0 = t.GetTextAngleDegrees()
        x0, y0 = pcbnew.ToMM(p0.x), pcbnew.ToMM(p0.y)
        cands = [(x0, y0, a0)]
        if fp is not None:
            cb = courtyard(fp).BBox()
            cx, cy = pcbnew.ToMM(cb.Centre().x), pcbnew.ToMM(cb.Centre().y)
            hw, hh = pcbnew.ToMM(cb.GetWidth()) / 2, pcbnew.ToMM(cb.GetHeight()) / 2
            th = size / 2 + 0.05
            for d in (0.0, 0.3, 0.6, 1.0, 1.5, 2.0):
                cands += [(cx, cy - hh - th - d, 0), (cx, cy + hh + th + d, 0),
                          (cx - hw - th - d, cy, 90), (cx + hw + th + d, cy, 90),
                          (cx - hw - th - d - size, cy, 0), (cx + hw + th + d + size, cy, 0)]
            x0, y0 = cx, cy
        for r in (0.3, 0.6, 1.0, 1.5, 2.0, 2.5, 3.0, 3.5, 4.0):
            for k in range(16):
                ang = 2 * math.pi * k / 16
                for a in ((a0,) if fp is None else (0, 90)):
                    cands.append((x0 + r * math.cos(ang), y0 + r * math.sin(ang), a))
        ok = None
        for x, y, a in cands:
            if math.hypot(x - x0, y - y0) > max_move + 3:
                continue
            g = try_at(t, x, y, a)
            if clear(g):
                ok = g
                break
        if ok is None:
            try_at(t, pcbnew.ToMM(p0.x), pcbnew.ToMM(p0.y), a0)
            if fp is not None:
                t.SetVisible(False)
                hidden.append(fp.GetReference())
            else:
                placed.append(_box(t.GetBoundingBox()))
        else:
            placed.append(ok)
    return hidden
