#!/usr/bin/env python3
"""Re-applies zone settings from gen_pcb.py, refills, adds island vias and writes the DRC
report, without re-running the autorouter."""
import math, os, re, sys
import pcbnew
HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.append(HERE)
import dfm, route  # noqa: E402

STEPS = {0.8: 0.5, 0.5: 0.3, 0.3: 0.25}

def narrow(board, tmp):
    """Steps widened charger tracks down (0.8 -> 0.5 -> 0.3 -> 0.25 mm) wherever DRC
    reports a clearance problem near them, until nothing changes."""
    nets = set(route.gen_pcb.NETCLASSES["PowerQFN"][4]) | set(route.gen_pcb.NETCLASSES["Power"][4])
    for _ in range(4):
        board.BuildConnectivity()
        rep = route.drc(board, tmp)
        bad = set()
        for blk in rep.split("\n["):
            if "clearance" in blk.split("\n")[0]:
                for m in re.finditer(r"@\(([-\d.]+) mm, ([-\d.]+) mm\)", blk):
                    bad.add((round(float(m.group(1)), 2), round(float(m.group(2)), 2)))
        n = 0
        for t in board.GetTracks():
            w = round(pcbnew.ToMM(t.GetWidth()), 2)
            if t.GetClass() != "PCB_TRACK" or t.IsLocked() or t.GetNetname() not in nets or w not in STEPS:
                continue
            pts = [(round(pcbnew.ToMM(p.x), 2), round(pcbnew.ToMM(p.y), 2)) for p in (t.GetStart(), t.GetEnd())]
            if any(route._near_seg(b, pts) for b in bad):
                t.SetWidth(pcbnew.FromMM(STEPS[w])); n += 1
        print("narrowed:", n)
        if not n:
            break

def dedupe(board):
    """The session file repeats the hand-routed copper; drop exact duplicates."""
    seen, n = set(), 0
    for t in list(board.GetTracks()):
        if t.GetClass() == "PCB_VIA":
            key = ("v", t.GetPosition().x, t.GetPosition().y)
        else:
            a, b = sorted([(t.GetStart().x, t.GetStart().y), (t.GetEnd().x, t.GetEnd().y)])
            key = ("t", t.GetLayer(), a, b)
        if key in seen:
            board.Delete(t); n += 1
        else:
            seen.add(key)
    print("duplicates removed:", n)

def drop_dangling(board, tmp):
    """Removes vias DRC calls dangling (connected on one layer only): stitching vias that
    landed on another net's track, or hand-placed vias the router did not use."""
    rep = route.drc(board, tmp)
    pts = set()
    for blk in rep.split("\n["):
        if blk.lstrip("[").startswith(("via_dangling", "track_dangling")):
            for m in re.finditer(r"@\(([-\d.]+) mm, ([-\d.]+) mm\)", blk):
                pts.add((round(float(m.group(1)), 2), round(float(m.group(2)), 2)))
    n = 0
    for v in list(board.GetTracks()):
        if v.IsLocked():
            continue  # hand-routed (charger.preroute): the router is meant to reach it
        if v.GetClass() == "PCB_VIA":
            ends = [v.GetPosition()]
        else:
            ends = [v.GetStart(), v.GetEnd(), (v.GetStart() + v.GetEnd()) / 2]
        if any((round(pcbnew.ToMM(p.x), 2), round(pcbnew.ToMM(p.y), 2)) in pts for p in ends):
            board.Delete(v); n += 1
    print("dangling vias removed:", n)
    return n

def double_power_vias(board):
    """Every layer change on a power net gets a second via next to the first (shared
    current, no hot spot), placed where it and its two short links clear other nets.
    The added vias are locked, so a later finish.py pass keeps them."""
    mm = pcbnew.FromMM
    nets = set(route.gen_pcb.NETCLASSES["Power"][4])
    vias = [t for t in board.GetTracks() if t.GetClass() == "PCB_VIA" and t.GetNetname() in nets]
    others = [t for t in board.GetTracks()]
    pads = [p for fp in board.GetFootprints() for p in fp.Pads()]
    areas = [z for z in board.Zones() if z.GetIsRuleArea() or z.GetNetname() not in ("GND",)]
    edge = board.GetBoardEdgesBoundingBox()
    cys = dfm.smd_courtyards(board)
    added = 0
    for v in vias:
        pos, net = v.GetPosition(), v.GetNetCode()
        if any(o.GetClass() == "PCB_VIA" and o.GetNetCode() == net
               and 0 < (o.GetPosition() - pos).EuclideanNorm() < mm(1.0) for o in others):
            continue
        for d, k in [(d, k) for d in (0.8, 1.0, 1.2) for k in range(16)]:
            a = math.radians(22.5 * k)
            c = pcbnew.VECTOR2I(pos.x + int(mm(d) * math.cos(a)), pos.y + int(mm(d) * math.sin(a)))
            if not (edge.GetX() + mm(0.8) < c.x < edge.GetRight() - mm(0.8)
                    and edge.GetY() + mm(0.8) < c.y < edge.GetBottom() - mm(0.8)):
                continue
            if dfm.in_courtyard(cys, c):
                continue
            shape = pcbnew.SHAPE_SEGMENT(pos, c, max(mm(0.6), v.GetWidth()))
            ok = True
            for layer in (pcbnew.F_Cu, pcbnew.B_Cu):
                for o in others:
                    if o.GetNetCode() != net and o.IsOnLayer(layer) and o.GetEffectiveShape(layer).Collide(shape, mm(0.2)):
                        ok = False; break
                if ok:
                    for pd in pads:
                        if pd.IsOnLayer(layer) and (pd.GetNetCode() != net or pd.GetAttribute() == pcbnew.PAD_ATTRIB_SMD) \
                                and pd.GetEffectivePolygon().Collide(c, mm(0.5)):
                            ok = False; break
                if ok:
                    for z in areas:
                        if z.IsOnLayer(layer) and z.GetNetCode() != net and z.Outline().Collide(c, mm(0.5)):
                            if not z.GetIsRuleArea() or z.GetDoNotAllowVias():
                                ok = False; break
                if not ok:
                    break
            if not ok:
                continue
            nv = pcbnew.PCB_VIA(board)
            nv.SetPosition(c); nv.SetWidth(v.GetWidth()); nv.SetDrill(v.GetDrill()); nv.SetNet(v.GetNet())
            nv.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu); nv.SetLocked(True)
            board.Add(nv)
            for layer in (pcbnew.F_Cu, pcbnew.B_Cu):
                t = pcbnew.PCB_TRACK(board)
                t.SetStart(pos); t.SetEnd(c); t.SetWidth(mm(0.6)); t.SetLayer(layer); t.SetNet(v.GetNet())
                t.SetLocked(True)
                board.Add(t)
            others += [nv]
            added += 1
            break
        else:
            print("no room for a second via at %.2f %.2f (%s)" % (pcbnew.ToMM(pos.x) - 100, pcbnew.ToMM(pos.y) - 100, v.GetNetname()))
    print("second power vias:", added)
    return added

def gnd_passes(board):
    """v0.4 GND vias (tools/dfm.py): edge fence and grid, capacitor and signal-via return
    vias, then at least two vias in every F.Cu GND piece."""
    fill = lambda: pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    g = route.gen_pcb
    fill()
    dfm.stitch(board, route.free_point, g.OX, g.OY, g.W, g.H)
    fill()
    dfm.cap_vias(board, route.free_point)
    dfm.signal_return_vias(board, route.free_point, route.signal_nets(board))
    for _ in range(3):
        fill()
        added, _short = dfm.fragment_vias(board, route.free_point)
        if not added:
            break
    fill()

def main():
    board = pcbnew.LoadBoard(route.PCB)
    dfm.remove_teardrops(board)
    dedupe(board)
    board.Save(route.PCB)
    board = pcbnew.LoadBoard(route.PCB)
    narrow(board, "/tmp/claude-0/narrow.rpt" if os.path.isdir("/tmp/claude-0") else route.PCB + ".rpt")
    double_power_vias(board)
    for z in board.Zones():
        if z.GetIsRuleArea():
            continue
        # GND pours: thermal spokes (parts in charger.SOLID override to solid);
        # power zones: solid
        z.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL if z.GetNetname() == "GND"
                           else pcbnew.ZONE_CONNECTION_FULL)
        z.SetLocalClearance(pcbnew.FromMM(0.2))
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    tmp = "/tmp/claude-0/dangling.rpt" if os.path.isdir("/tmp/claude-0") else route.PCB + ".rpt"
    if drop_dangling(board, tmp):
        board.Save(route.PCB)
        board = pcbnew.LoadBoard(route.PCB)
    gnd_passes(board)
    if drop_dangling(board, tmp):
        gnd_passes(board)
    dfm.teardrops(board)
    # the stitching vias are new: move the silk references off them again
    hidden = dfm.place_silk(board)
    print("silk: hidden refs", " ".join(hidden) or "none")
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    board.BuildConnectivity()
    board.Save(route.PCB)
    rep = route.drc(board, os.path.join(route.ROOT, "production", "drc.rpt"))
    print("unconnected:", board.GetConnectivity().GetUnconnectedCount(True))
    print(re.findall(r"\*\* Found \d+ [^*]+", rep))
    dfm.report(board)

if __name__ == "__main__":
    main()
