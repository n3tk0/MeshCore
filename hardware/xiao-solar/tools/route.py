#!/usr/bin/env python3
"""Routes xiao-solar.kicad_pcb with Freerouting and finishes the copper.

1. export Specctra DSN (net classes fixed up by dsn_classes.py)
2. freerouting (FREEROUTING_JAR, run under xvfb-run)
3. import the SES wires and vias (KiCad 7's ImportSpecctraSES needs the GUI)
4. widen power tracks away from U1, add GND stitching vias, fill zones
(the charger's power zones go into the DSN as fixed copper, see dsn_classes.add_zone_copper)
5. write DRC report to production/drc.rpt

Run: FREEROUTING_JAR=/path/freerouting-1.9.0.jar python3 tools/route.py
"""
import os, re, subprocess, sys, tempfile
import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.append(HERE)
import gen_pcb, dsn_classes  # noqa: E402

ROOT, PCB = gen_pcb.ROOT, gen_pcb.PCB

def mm(v):
    return pcbnew.FromMM(v)

def tokens(text):
    return re.findall(r'\(|\)|"[^"]*"|[^\s()]+', text)

def parse(tok, i=0):
    out = []
    while i < len(tok):
        t = tok[i]
        if t == "(":
            sub, i = parse(tok, i + 1)
            out.append(sub)
        elif t == ")":
            return out, i + 1
        else:
            out.append(t.strip('"'))
            i += 1
    return out, i

def find(node, key):
    return [n for n in node if isinstance(n, list) and n and n[0] == key]

def import_ses(board, path):
    tree, _ = parse(tokens(open(path).read()))
    sess = tree[0]
    routes = find(sess, "routes")[0]
    res = find(routes, "resolution")[0]
    scale = {"um": 1e-3, "mm": 1.0, "mil": 0.0254}[res[1]] / float(res[2])
    nets = find(find(routes, "network_out")[0], "net")
    layers = {"F.Cu": pcbnew.F_Cu, "B.Cu": pcbnew.B_Cu}
    nseg = nvia = 0
    for n in nets:
        net = board.FindNet(n[1])
        for w in find(n, "wire"):
            for path in find(w, "path"):
                layer = layers[path[1]]
                width = float(path[2]) * scale
                xy = [float(v) * scale for v in path[3:] if not isinstance(v, list)]
                pts = [(xy[k], -xy[k + 1]) for k in range(0, len(xy) - 1, 2)]
                for a, b in zip(pts, pts[1:]):
                    t = pcbnew.PCB_TRACK(board)
                    t.SetStart(pcbnew.VECTOR2I(mm(a[0]), mm(a[1])))
                    t.SetEnd(pcbnew.VECTOR2I(mm(b[0]), mm(b[1])))
                    t.SetWidth(mm(width)); t.SetLayer(layer); t.SetNet(net)
                    board.Add(t); nseg += 1
        for v in find(n, "via"):
            m = re.search(r"_(\d+):(\d+)_um", v[1])
            dia, drill = (int(m.group(1)) / 1000.0, int(m.group(2)) / 1000.0) if m else (0.6, 0.3)
            via = pcbnew.PCB_VIA(board)
            via.SetPosition(pcbnew.VECTOR2I(mm(float(v[2]) * scale), mm(-float(v[3]) * scale)))
            via.SetWidth(mm(dia)); via.SetDrill(mm(drill)); via.SetNet(net)
            via.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
            board.Add(via); nvia += 1
    return nseg, nvia

def drc(board, path):
    pcbnew.WriteDRCReport(board, path, pcbnew.EDA_UNITS_MILLIMETRES, True)
    return open(path).read()

def widen(board, tmp):
    """Widen PowerQFN tracks to 0.8 mm except near U1, then undo any that break clearance."""
    u1 = board.FindFootprintByReference("U1").GetPosition()
    nets = set(gen_pcb.NETCLASSES["PowerQFN"][4]) | set(gen_pcb.NETCLASSES["Power"][4])
    changed, orig = [], {}
    for t in board.GetTracks():
        if t.GetClass() != "PCB_TRACK" or t.IsLocked() or t.GetNetname() not in nets:
            continue
        far = all(((p.x - u1.x) ** 2 + (p.y - u1.y) ** 2) ** 0.5 > mm(2.6) for p in (t.GetStart(), t.GetEnd()))
        if far:
            orig[t] = t.GetWidth()
            t.SetWidth(mm(0.8)); changed.append(t)
    steps = {0.8: 0.5, 0.5: 0.3, 0.3: None}
    for width in (0.8, 0.5, 0.3):
        board.BuildConnectivity()
        rep = drc(board, tmp)
        bad = set()
        for blk in rep.split("\n["):
            if "clearance" in blk.split("\n")[0] or "shorting" in blk.split("\n")[0]:
                for m in re.finditer(r"@\(([-\d.]+) mm, ([-\d.]+) mm\)", blk):
                    bad.add((round(float(m.group(1)), 2), round(float(m.group(2)), 2)))
        reverted = 0
        for t in changed:
            if t.GetWidth() != mm(width):
                continue
            pts = [(round(pcbnew.ToMM(p.x), 2), round(pcbnew.ToMM(p.y), 2)) for p in (t.GetStart(), t.GetEnd())]
            if any(_near_seg(b, pts) for b in bad):
                nxt = steps[width]
                t.SetWidth(mm(nxt) if nxt and mm(nxt) > orig[t] else orig[t]); reverted += 1
        print("widen: %d tracks at %.1f mm, %d stepped down" % (len(changed), width, reverted))

def _near_seg(p, seg, tol=0.5):
    (x1, y1), (x2, y2) = seg
    dx, dy = x2 - x1, y2 - y1
    L = dx * dx + dy * dy
    u = 0 if L == 0 else max(0, min(1, ((p[0] - x1) * dx + (p[1] - y1) * dy) / L))
    cx, cy = x1 + u * dx, y1 + u * dy
    return ((p[0] - cx) ** 2 + (p[1] - cy) ** 2) ** 0.5 < tol

def stitch(board, tmp, pitch=3.0):
    gnd = board.FindNet("GND")
    added = []
    x = 1.5
    while x < gen_pcb.W - 1.0:
        y = 1.5
        while y < gen_pcb.H - 1.0:
            v = pcbnew.PCB_VIA(board)
            v.SetPosition(pcbnew.VECTOR2I(mm(gen_pcb.OX + x), mm(gen_pcb.OY + y)))
            v.SetWidth(mm(0.6)); v.SetDrill(mm(0.3)); v.SetNet(gnd)
            v.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
            board.Add(v); added.append(v)
            y += pitch
        x += pitch
    board.BuildConnectivity()
    rep = drc(board, tmp)
    bad = []
    for blk in rep.split("\n["):
        head = blk.split("\n")[0]
        if any(k in head for k in ("clearance", "hole", "courtyard", "shorting", "silk")):
            for m in re.finditer(r"@\(([-\d.]+) mm, ([-\d.]+) mm\)", blk):
                bad.append((float(m.group(1)), float(m.group(2))))
    keep = 0
    for v in added:
        p = (pcbnew.ToMM(v.GetPosition().x), pcbnew.ToMM(v.GetPosition().y))
        if any(abs(p[0] - b[0]) < 0.8 and abs(p[1] - b[1]) < 0.8 for b in bad) or not _in_free_area(board, v):
            board.Remove(v)
        else:
            keep += 1
    print("stitching vias:", keep)

def drop_dangling(board, tmp):
    """Removes GND vias that the fills left connected on one layer only."""
    rep = drc(board, tmp)
    pts = set()
    for blk in rep.split("\n["):
        if blk.split("\n")[0].startswith("via_dangling") or "via_dangling" in blk.split("\n")[0]:
            for m in re.finditer(r"@\(([-\d.]+) mm, ([-\d.]+) mm\)", blk):
                pts.add((round(float(m.group(1)), 3), round(float(m.group(2)), 3)))
    n = 0
    for v in list(board.GetTracks()):
        if v.GetClass() == "PCB_VIA" and v.GetNetname() == "GND" and not v.IsLocked():
            p = (round(pcbnew.ToMM(v.GetPosition().x), 3), round(pcbnew.ToMM(v.GetPosition().y), 3))
            if p in pts:
                board.Remove(v); n += 1
    print("dangling vias removed:", n)
    return n

def _near_nettie(board, p):
    for fp in board.GetFootprints():
        if fp.GetReference().startswith("NT"):
            bb = fp.GetBoundingBox(False, False); bb.Inflate(mm(0.6))
            if bb.Contains(p):
                return True
    return False

def stitch_islands(board):
    """Gives every F.Cu GND fragment without a via or THT GND pad one via, at a point where
    both layers' fills leave room for it (fills already keep clearance to other nets)."""
    gnd = board.FindNet("GND")
    def anchors():
        pts = [t.GetPosition() for t in board.GetTracks() if t.GetClass() == "PCB_VIA" and t.GetNetCode() == gnd.GetNetCode()]
        pts += [p.GetPosition() for f in board.GetFootprints() for p in f.Pads()
                if p.GetNetCode() == gnd.GetNetCode() and p.GetAttribute() == pcbnew.PAD_ATTRIB_PTH]
        return pts
    zones = {z.GetLayer(): z for z in board.Zones() if not z.GetIsRuleArea() and z.GetNetCode() == gnd.GetNetCode()}
    back = zones[pcbnew.B_Cu].GetFilledPolysList(pcbnew.B_Cu).CloneDropTriangulation()
    back.Deflate(mm(0.32), 16)
    front = zones[pcbnew.F_Cu].GetFilledPolysList(pcbnew.F_Cu)
    added = 0
    for i in range(front.OutlineCount()):
        ol = front.Outline(i)
        if any(ol.PointInside(a) for a in anchors()):
            continue
        frag = pcbnew.SHAPE_POLY_SET(); frag.AddOutline(ol)
        frag.Deflate(mm(0.32), 16)
        bb = ol.BBox()
        done = False
        y = bb.GetY()
        while y < bb.GetBottom() and not done:
            x = bb.GetX()
            while x < bb.GetRight() and not done:
                p = pcbnew.VECTOR2I(x, y)
                if frag.Contains(p) and back.Contains(p) and not _near_nettie(board, p):
                    v = pcbnew.PCB_VIA(board)
                    v.SetPosition(p); v.SetWidth(mm(0.6)); v.SetDrill(mm(0.3)); v.SetNet(gnd)
                    v.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
                    board.Add(v); added += 1; done = True
                x += mm(0.25)
            y += mm(0.25)
    print("island vias:", added)
    return added

def _in_free_area(board, via):
    """Keep vias out of module footprints (XIAO/Wio sit on the board), mounting holes and
    the SHT40 tongue."""
    p = via.GetPosition()
    x0, y0, x1, y1 = getattr(gen_pcb, "SENSOR_TONGUE", (-9, -9, -9, -9))
    if mm(gen_pcb.OX + x0 - 1.5) < p.x < mm(gen_pcb.OX + x1 + 1.5) and p.y < mm(gen_pcb.OY + y1 + 2.0):
        return False
    for z in board.Zones():
        if not z.GetIsRuleArea() and z.GetNetname() != "GND" and z.Outline().Collide(p, mm(0.6)):
            return False
    for fp in board.GetFootprints():
        if fp.GetReference()[0] in "UH" or fp.GetReference().startswith(("J", "NT")):
            bb = fp.GetBoundingBox(False, False)
            bb.Inflate(mm(0.5))
            if bb.Contains(p):
                return False
    return True

def main():
    jar = os.environ.get("FREEROUTING_JAR")
    if not jar:
        raise SystemExit("set FREEROUTING_JAR")
    tmp = os.environ.get("RT_TMP") or tempfile.mkdtemp()
    dsn, ses = os.path.join(tmp, "xs.dsn"), os.path.join(tmp, "xs.ses")
    board = pcbnew.LoadBoard(PCB)
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    pcbnew.ExportSpecctraDSN(board, dsn)
    dsn_classes.fix(dsn)
    dsn_classes.add_zone_copper(board, dsn)
    subprocess.run(["xvfb-run", "-a", "java", "-jar", jar, "-de", dsn, "-do", ses, "-mp", "200"],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    print("ses: %d segments, %d vias" % import_ses(board, ses))
    rep_tmp = os.path.join(tmp, "drc.rpt")
    widen(board, rep_tmp)
    stitch(board, rep_tmp)
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    while stitch_islands(board):
        pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    board.BuildConnectivity()
    if drop_dangling(board, rep_tmp):
        pcbnew.ZONE_FILLER(board).Fill(board.Zones())
        while stitch_islands(board):
            pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    board.BuildConnectivity()
    board.Save(PCB)
    import refill  # noqa: E402  (narrows widened tracks that DRC flags, refills, writes DRC)
    refill.main()
    board = pcbnew.LoadBoard(PCB)
    rep = open(os.path.join(ROOT, "production", "drc.rpt")).read()
    print("unconnected:", board.GetConnectivity().GetUnconnectedCount(True))
    print(re.findall(r"\*\* Found \d+ [^*]+", rep))

if __name__ == "__main__":
    main()
