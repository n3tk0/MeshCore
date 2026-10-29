#!/usr/bin/env python3
"""Re-applies zone settings from gen_pcb.py, refills, adds island vias and writes the DRC
report, without re-running the autorouter."""
import os, re, sys
import pcbnew
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import route  # noqa: E402

STEPS = {0.8: 0.5, 0.5: 0.3, 0.3: 0.25}

def narrow(board, tmp):
    """Steps widened charger tracks down (0.8 -> 0.5 -> 0.3 -> 0.25 mm) wherever DRC
    reports a clearance problem near them, until nothing changes."""
    nets = set(route.gen_pcb.NETCLASSES["PowerQFN"][4])
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
            if t.GetClass() != "PCB_TRACK" or t.GetNetname() not in nets or w not in STEPS:
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
        if v.GetClass() == "PCB_VIA":
            ends = [v.GetPosition()]
        else:
            ends = [v.GetStart(), v.GetEnd(), (v.GetStart() + v.GetEnd()) / 2]
        if any((round(pcbnew.ToMM(p.x), 2), round(pcbnew.ToMM(p.y), 2)) in pts for p in ends):
            board.Delete(v); n += 1
    print("dangling vias removed:", n)
    return n

def main():
    board = pcbnew.LoadBoard(route.PCB)
    dedupe(board)
    board.Save(route.PCB)
    board = pcbnew.LoadBoard(route.PCB)
    narrow(board, "/tmp/claude-0/narrow.rpt" if os.path.isdir("/tmp/claude-0") else route.PCB + ".rpt")
    for z in board.Zones():
        if z.GetIsRuleArea():
            continue
        z.SetPadConnection(pcbnew.ZONE_CONNECTION_THT_THERMAL)
        z.SetLocalClearance(pcbnew.FromMM(0.2))
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    tmp = "/tmp/claude-0/dangling.rpt" if os.path.isdir("/tmp/claude-0") else route.PCB + ".rpt"
    if drop_dangling(board, tmp):
        board.Save(route.PCB)
        board = pcbnew.LoadBoard(route.PCB)
        pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    while route.stitch_islands(board):
        pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    board.BuildConnectivity()
    board.Save(route.PCB)
    rep = route.drc(board, os.path.join(route.ROOT, "production", "drc.rpt"))
    print("unconnected:", board.GetConnectivity().GetUnconnectedCount(True))
    print(re.findall(r"\*\* Found \d+ [^*]+", rep))

if __name__ == "__main__":
    main()
