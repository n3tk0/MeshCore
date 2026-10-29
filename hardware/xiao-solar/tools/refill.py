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

def main():
    board = pcbnew.LoadBoard(route.PCB)
    narrow(board, "/tmp/claude-0/narrow.rpt" if os.path.isdir("/tmp/claude-0") else route.PCB + ".rpt")
    for z in board.Zones():
        if z.GetIsRuleArea():
            continue
        z.SetPadConnection(pcbnew.ZONE_CONNECTION_THT_THERMAL)
        z.SetLocalClearance(pcbnew.FromMM(0.2))
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
