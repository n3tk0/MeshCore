#!/usr/bin/env python3
"""Routes one leftover connection on a single copper layer with a small grid A*, for the
odd net Freerouting leaves open. Obstacles are all other-net copper on that layer
(pads, tracks, vias, zones are refilled afterwards) grown by clearance + half width.

Run: python3 tools/patch_route.py NET LAYER X1 Y1 X2 Y2 [WIDTH]
     (board mm, origin at the board's top-left corner; ends must already be copper of
     NET on LAYER, or a via is placed there)
"""
import heapq, math, os, sys
import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.append(HERE)
import gen_pcb  # noqa: E402

PCB = gen_pcb.PCB
STEP = 0.1
CLEAR = 0.2

def mm(v):
    return pcbnew.FromMM(v)

def main():
    net, layer = sys.argv[1], sys.argv[2]
    x1, y1, x2, y2 = map(float, sys.argv[3:7])
    width = float(sys.argv[7]) if len(sys.argv) > 7 else 0.25
    board = pcbnew.LoadBoard(PCB)
    lay = board.GetLayerID(layer)
    nc = board.FindNet(net).GetNetCode()
    shapes = []
    for t in board.GetTracks():
        if t.GetNetCode() != nc and t.IsOnLayer(lay):
            shapes.append(t.GetEffectiveShape(lay))
    for fp in board.GetFootprints():
        for p in fp.Pads():
            if p.GetNetCode() != nc and p.IsOnLayer(lay):
                shapes.append(p.GetEffectiveShape(lay))
            if p.GetAttribute() == pcbnew.PAD_ATTRIB_NPTH:
                shapes.append(p.GetEffectiveHoleShape())
    for z in board.Zones():
        if z.GetIsRuleArea() and z.GetDoNotAllowTracks():
            shapes.append(z.Outline())
    nx, ny = int(gen_pcb.W / STEP) + 1, int(gen_pcb.H / STEP) + 1
    blocked = bytearray(nx * ny)
    r = mm(width / 2 + CLEAR)
    edge = 0.3 + width / 2
    for s in shapes:
        bb = s.BBox()
        x0 = max(0, int((pcbnew.ToMM(bb.GetX()) - gen_pcb.OX) / STEP) - 6)
        xe = min(nx - 1, int((pcbnew.ToMM(bb.GetRight()) - gen_pcb.OX) / STEP) + 6)
        y0 = max(0, int((pcbnew.ToMM(bb.GetY()) - gen_pcb.OY) / STEP) - 6)
        ye = min(ny - 1, int((pcbnew.ToMM(bb.GetBottom()) - gen_pcb.OY) / STEP) + 6)
        for i in range(x0, xe + 1):
            for j in range(y0, ye + 1):
                k = j * nx + i
                if blocked[k]:
                    continue
                c = pcbnew.VECTOR2I(mm(gen_pcb.OX + i * STEP), mm(gen_pcb.OY + j * STEP))
                if s.Collide(c, r):
                    blocked[k] = 1
    for i in range(nx):
        for j in range(ny):
            x, y = i * STEP, j * STEP
            if x < edge or y < edge or x > gen_pcb.W - edge or y > gen_pcb.H - edge:
                blocked[j * nx + i] = 1
    s0 = (round(x1 / STEP), round(y1 / STEP))
    s1 = (round(x2 / STEP), round(y2 / STEP))
    for p in (s0, s1):
        blocked[p[1] * nx + p[0]] = 0
    moves = [(1, 0, 1), (-1, 0, 1), (0, 1, 1), (0, -1, 1),
             (1, 1, math.sqrt(2)), (1, -1, math.sqrt(2)), (-1, 1, math.sqrt(2)), (-1, -1, math.sqrt(2))]
    h = lambda p: math.hypot(p[0] - s1[0], p[1] - s1[1])
    best = {s0: 0.0}
    came = {}
    pq = [(h(s0), 0.0, s0, None)]
    while pq:
        f, g, p, d = heapq.heappop(pq)
        if p == s1:
            break
        if g > best.get(p, 1e18):
            continue
        for dx, dy, c in moves:
            q = (p[0] + dx, p[1] + dy)
            if not (0 <= q[0] < nx and 0 <= q[1] < ny) or blocked[q[1] * nx + q[0]]:
                continue
            ng = g + c + (0.5 if d and d != (dx, dy) else 0)
            if ng < best.get(q, 1e18):
                best[q] = ng
                came[q] = (p, (dx, dy))
                heapq.heappush(pq, (ng + h(q), ng, q, (dx, dy)))
    else:
        raise SystemExit("no path")
    path = [s1]
    while path[-1] != s0:
        path.append(came[path[-1]][0])
    path.reverse()
    # keep only the corners
    pts = [path[0]]
    for a, b, c in zip(path, path[1:], path[2:]):
        if (b[0] - a[0], b[1] - a[1]) != (c[0] - b[0], c[1] - b[1]):
            pts.append(b)
    pts.append(path[-1])
    n = board.FindNet(net)
    for a, b in zip(pts, pts[1:]):
        t = pcbnew.PCB_TRACK(board)
        t.SetStart(pcbnew.VECTOR2I(mm(gen_pcb.OX + a[0] * STEP), mm(gen_pcb.OY + a[1] * STEP)))
        t.SetEnd(pcbnew.VECTOR2I(mm(gen_pcb.OX + b[0] * STEP), mm(gen_pcb.OY + b[1] * STEP)))
        t.SetWidth(mm(width)); t.SetLayer(lay); t.SetNet(n)
        board.Add(t)
    board.Save(PCB)
    print("routed %s on %s: %d segments" % (net, layer, len(pts) - 1))

if __name__ == "__main__":
    main()
