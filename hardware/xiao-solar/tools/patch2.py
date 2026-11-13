#!/usr/bin/env python3
"""Two-layer grid A* for the connections Freerouting leaves open on this board.

Run: python3 tools/patch2.py NET X1 Y1 X2 Y2 [WIDTH]   (board mm, both ends on NET copper)   (repeat per net, then `tools/run.py refill`)

Connects the copper island under (X1,Y1) to the one under (X2,Y2). Obstacles are all other-net copper
on each layer (pads, tracks, vias, non-GND zones; the GND pours are refilled afterwards)
grown by clearance + half width. Vias (0.6/0.3) only where both layers are free, not on a
pad and not in an SMD courtyard (the SOIC body of U4 is allowed: tented vias under it).
"""
import heapq, math, os, sys
import pcbnew
import numpy as np
import shapely
from shapely.geometry import Polygon, Point
from shapely.ops import unary_union

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gen_pcb  # noqa: E402

PCB = gen_pcb.PCB
STEP = 0.1
CLEAR = 0.2
VIA_R = 0.3
VIA_COST = 12.0
LAYERS = (pcbnew.F_Cu, pcbnew.B_Cu)
OX, OY = gen_pcb.OX, gen_pcb.OY

def mm(v):
    return pcbnew.FromMM(float(v))

def polys(ps):
    out = []
    for i in range(ps.OutlineCount()):
        ol = ps.Outline(i)
        pts = [(pcbnew.ToMM(ol.CPoint(k).x) - OX, pcbnew.ToMM(ol.CPoint(k).y) - OY) for k in range(ol.PointCount())]
        if len(pts) < 3:
            continue
        holes = []
        for h in range(ps.HoleCount(i)):
            hl = ps.Hole(i, h)
            holes.append([(pcbnew.ToMM(hl.CPoint(k).x) - OX, pcbnew.ToMM(hl.CPoint(k).y) - OY) for k in range(hl.PointCount())])
        out.append(Polygon(pts, holes).buffer(0))
    return out

def item_poly(it, layer):
    ps = pcbnew.SHAPE_POLY_SET()
    it.TransformShapeToPolygon(ps, layer, 0, mm(0.005), pcbnew.ERROR_OUTSIDE)
    return polys(ps)

def main():
    net = sys.argv[1]
    width = float(sys.argv[6]) if len(sys.argv) > 6 else 0.25
    ends = ((float(sys.argv[2]), float(sys.argv[3])), (float(sys.argv[4]), float(sys.argv[5])))
    board = pcbnew.LoadBoard(PCB)
    print(route_net(board, net, ends, width))
    board.Save(PCB)

def route_net(board, net, ends, width=0.25, lock=False, via_w=0.6, layer_cost=(1.0, 1.0), heur=True, start_layers=(0, 1), window=None, clear=CLEAR, keepout=(), via_in_small_cy=False, target_layers=(0, 1), start_pad_only=False, keepout_b=()):
    """keepout: (x0, y0, x1, y1) boxes in board mm this route must stay out of, both layers;
    keepout_b: the same for B.Cu only (no B.Cu track and no via in them)."""
    """Routes one connection on `board` (see the module doc); returns a one-line summary."""
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    board.BuildConnectivity()
    nc = board.FindNet(net).GetNetCode()
    obst = {l: [] for l in LAYERS}
    own = {l: [] for l in LAYERS}       # (geom, item) of this net
    pads_all = []
    for t in board.GetTracks():
        for l in LAYERS:
            if t.IsOnLayer(l):
                for g in item_poly(t, l):
                    (own[l].append((g, t)) if t.GetNetCode() == nc else obst[l].append(g))
    for fp in board.GetFootprints():
        for p in fp.Pads():
            for l in LAYERS:
                if p.IsOnLayer(l):
                    for g in item_poly(p, l):
                        pads_all.append(g)
                        (own[l].append((g, p)) if p.GetNetCode() == nc else obst[l].append(g))
            if p.GetAttribute() in (pcbnew.PAD_ATTRIB_PTH, pcbnew.PAD_ATTRIB_NPTH):
                pos = p.GetPosition()
                r = pcbnew.ToMM(max(p.GetDrillSize().x, p.GetDrillSize().y)) / 2
                pads_all.append(Point(pcbnew.ToMM(pos.x) - OX, pcbnew.ToMM(pos.y) - OY).buffer(r))
    for z in board.Zones():
        for l in LAYERS:
            if not z.IsOnLayer(l):
                continue
            if z.GetIsRuleArea():
                if z.GetDoNotAllowTracks():
                    obst[l] += polys(z.Outline())
                continue
            if z.GetNetCode() == nc:
                own[l] += [(g, z) for g in polys(z.GetFilledPolysList(l))]
                continue
            if z.GetNetname() == "GND":
                continue
            obst[l] += polys(z.GetFilledPolysList(l))
    cys = []
    for fp in board.GetFootprints():
        small = "0402" in fp.GetFPIDAsString() or "0603" in fp.GetFPIDAsString()
        if fp.GetAttributes() & pcbnew.FP_SMD and fp.GetReference() != "U4" and not (small and via_in_small_cy):
            cys += polys(fp.GetCourtyard(pcbnew.F_CrtYd))
    conn = board.GetConnectivity()
    W, H = gen_pcb.W, gen_pcb.H
    nx, ny = int(W / STEP) + 1, int(H / STEP) + 1
    xs, ys = np.meshgrid(np.arange(nx) * STEP, np.arange(ny) * STEP)
    edge = 0.4 + width / 2   # copper >= 0.4 mm from the edge (V-cut panels)
    T0 = getattr(gen_pcb, "TOP", 0.0)
    border = (xs < edge) | (ys < T0 + edge) | (xs > W - edge) | (ys > H - edge)
    blocked = {}
    for (kx0, ky0, kx1, ky1) in keepout:
        border = border | ((xs > kx0) & (xs < kx1) & (ys > ky0) & (ys < ky1))
    kb = np.zeros_like(border)
    for (kx0, ky0, kx1, ky1) in keepout_b:
        kb = kb | ((xs > kx0) & (xs < kx1) & (ys > ky0) & (ys < ky1))
    for l in LAYERS:
        u = unary_union(obst[l]).buffer(width / 2 + clear)
        blocked[l] = shapely.contains_xy(u, xs, ys) | border | (kb if l == pcbnew.B_Cu else False)
    vb = np.zeros_like(border)
    for l in LAYERS:
        u = unary_union(obst[l]).buffer((via_w / 2) + CLEAR)
        vb |= shapely.contains_xy(u, xs, ys)
    vb |= shapely.contains_xy(unary_union(pads_all).buffer((via_w / 2) + 0.15), xs, ys)
    holes = [Point(pcbnew.ToMM(t.GetPosition().x) - OX, pcbnew.ToMM(t.GetPosition().y) - OY)
             for t in board.GetTracks() if t.GetClass() == "PCB_VIA"]
    if holes:   # no new via on or next to any via, own net too (hole to hole 0.25 mm)
        vb |= shapely.contains_xy(unary_union(holes).buffer(via_w + 0.25), xs, ys)
    vb |= shapely.contains_xy(unary_union(cys).buffer((via_w / 2)), xs, ys)
    vb |= (xs < 0.8) | (ys < T0 + 0.8) | (xs > W - 0.8) | (ys > H - 0.8) | kb
    # start / goal sets: own copper of the island under each end, on each layer
    def island_cells(pt, layers=(0, 1), pad_only=False):
        cells = set()
        for li, l in enumerate(LAYERS):
            if li not in layers:
                continue
            for g, it in own[l]:
                if pad_only and not isinstance(it, pcbnew.PAD):
                    continue
                if g.distance(Point(pt)) < 0.05:
                    gi = g.buffer(-width / 2 + 0.02) if g.area > 0.05 else g
                    if gi.is_empty:
                        gi = g
                    m = shapely.contains_xy(gi, xs, ys)
                    for j, i in zip(*np.nonzero(m)):
                        cells.add((li, int(i), int(j)))
                    if not m.any():   # a pad narrower than the grid: its centre cell
                        cells.add((li, int(round(pt[0] / STEP)), int(round(pt[1] / STEP))))
        return cells
    def pad_layers(pt, default):
        """An SMD pad under the point: only its own layer belongs to that end."""
        for fp in board.GetFootprints():
            for p in fp.Pads():
                if p.GetNetCode() == nc and p.GetAttribute() == pcbnew.PAD_ATTRIB_SMD:
                    q = p.GetPosition()
                    if math.hypot(pcbnew.ToMM(q.x) - OX - pt[0], pcbnew.ToMM(q.y) - OY - pt[1]) < 0.05:
                        return tuple(i for i, l in enumerate(LAYERS) if p.IsOnLayer(l))
        return default
    S = island_cells(ends[0], pad_layers(ends[0], start_layers), start_pad_only)
    T = island_cells(ends[1], pad_layers(ends[1], target_layers)) - S
    if not S or not T:
        raise RuntimeError("ends not on copper: %d %d" % (len(S), len(T)))
    tx, ty = ends[1][0] / STEP, ends[1][1] / STEP
    h = (lambda i, j: math.hypot(i - tx, j - ty) * 0.98) if heur else (lambda i, j: 0.0)
    best = {}
    came = {}
    pq = []
    for c in S:
        best[c] = 0.0
        heapq.heappush(pq, (h(c[1], c[2]), 0.0, c, None))
    moves = [(1, 0, 1), (-1, 0, 1), (0, 1, 1), (0, -1, 1),
             (1, 1, math.sqrt(2)), (1, -1, math.sqrt(2)), (-1, 1, math.sqrt(2)), (-1, -1, math.sqrt(2))]
    goal = None
    while pq:
        f, g, c, d = heapq.heappop(pq)
        if c in T:
            goal = c; break
        if g > best.get(c, 1e18):
            continue
        li, i, j = c
        nbrs = []
        for dx, dy, cost in moves:
            q = (li, int(i + dx), int(j + dy))
            if window and math.hypot(q[1] - ends[0][0] / STEP, q[2] - ends[0][1] / STEP) > window / STEP:
                continue
            if 0 <= q[1] < nx and 0 <= q[2] < ny and (not blocked[LAYERS[li]][q[2], q[1]] or q in T):
                nbrs.append((q, cost * layer_cost[li] + (0.4 if d and d != (dx, dy) else 0), (dx, dy)))
        if not vb[j, i]:
            nbrs.append(((1 - li, i, j), VIA_COST, None))
        for q, cost, nd in nbrs:
            ng = g + cost
            if ng < best.get(q, 1e18):
                best[q] = ng; came[q] = c
                heapq.heappush(pq, (ng + h(q[1], q[2]), ng, q, nd))
    if goal is None:
        raise RuntimeError("no path for " + net)
    path = [goal]
    while path[-1] in came:
        path.append(came[path[-1]])
    path.reverse()
    n = board.FindNet(net)
    pos = lambda c: pcbnew.VECTOR2I(mm(OX + c[1] * STEP), mm(OY + c[2] * STEP))
    segs = 0; vias = 0
    run = [path[0]]
    def flush(run):
        nonlocal segs
        pts = [run[0]]
        for a, b, c in zip(run, run[1:], run[2:]):
            if (b[1] - a[1], b[2] - a[2]) != (c[1] - b[1], c[2] - b[2]):
                pts.append(b)
        pts.append(run[-1])
        for a, b in zip(pts, pts[1:]):
            if a == b:
                continue
            t = pcbnew.PCB_TRACK(board)
            t.SetStart(pos(a)); t.SetEnd(pos(b)); t.SetWidth(mm(width))
            t.SetLayer(LAYERS[a[0]]); t.SetNet(n); t.SetLocked(lock); board.Add(t); segs += 1
    for c in path[1:]:
        if c[0] != run[-1][0]:
            if len(run) > 1:
                flush(run)
            v = pcbnew.PCB_VIA(board)
            v.SetPosition(pos(c)); v.SetWidth(mm(via_w)); v.SetLocked(lock); v.SetDrill(mm(0.4 if via_w >= 0.8 else 0.3)); v.SetNet(n)
            v.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu); board.Add(v); vias += 1
            run = [c]
        else:
            run.append(c)
    if len(run) > 1:
        flush(run)
    return "routed %s: %d segments, %d vias, length %.1f mm" % (net, segs, vias, len(path) * STEP)

if __name__ == "__main__":
    main()
