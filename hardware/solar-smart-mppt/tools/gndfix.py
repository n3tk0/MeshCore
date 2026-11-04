#!/usr/bin/env python3
"""Ties every F.Cu GND fragment without a via to the B.Cu ground plane.

The autorouter leaves small F.Cu GND islands between the 0402 parts that no stitching via
fits into (courtyards). For each such island (and each GND pad the pour does not reach),
this routes a short track from one of its GND pads to a via into the main B.Cu GND fill
with tools/patch2.py's two-layer grid search. Run after `tools/run.py finish`, then
`tools/run.py refill` (refill keeps the new tracks: they are locked).

Run: python3 tools/gndfix.py
"""
import json, os, subprocess, sys
import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gen_pcb, patch2  # noqa: E402

OX, OY = gen_pcb.OX, gen_pcb.OY

def board_mm(p):
    return (pcbnew.ToMM(p.x) - OX, pcbnew.ToMM(p.y) - OY)

def drc_pads(pcb, tmp):
    """GND pads the DRC lists as unconnected: (ref, pad number)."""
    subprocess.run(["kicad-cli", "pcb", "drc", "--format", "json", "-o", tmp, pcb],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    out = set()
    for u in json.load(open(tmp))["unconnected_items"]:
        for it in u["items"]:
            d = it["description"]
            if d.startswith("Pad ") and "[GND]" in d:
                out.add((d.split(" of ")[1].strip(), d.split()[1]))
    return out

def main():
    tmp = os.environ.get("RT_TMP", "/tmp") + "/gndfix.json"
    done = set()
    for rnd in range(6):
        board = pcbnew.LoadBoard(gen_pcb.PCB)
        pcbnew.ZONE_FILLER(board).Fill(board.Zones())
        gnd = board.FindNet("GND").GetNetCode()
        vias = [t.GetPosition() for t in board.GetTracks() if t.GetClass() == "PCB_VIA" and t.GetNetCode() == gnd]
        pads = [(fp.GetReference(), p) for fp in board.GetFootprints() for p in fp.Pads() if p.GetNetCode() == gnd]
        zb = [z for z in board.Zones() if not z.GetIsRuleArea() and z.GetNetCode() == gnd and z.IsOnLayer(pcbnew.B_Cu)][0]
        back = zb.GetFilledPolysList(pcbnew.B_Cu)
        main_b = max(range(back.OutlineCount()), key=lambda i: back.Outline(i).Area())
        ref_pt = next(v for v in vias if back.Outline(main_b).PointInside(v))
        zf = [z for z in board.Zones() if not z.GetIsRuleArea() and z.GetNetCode() == gnd and z.IsOnLayer(pcbnew.F_Cu)][0]
        front = zf.GetFilledPolysList(pcbnew.F_Cu)
        todo = []
        for i in range(front.OutlineCount()):
            ol = front.Outline(i)
            if any(ol.PointInside(v) for v in vias):
                continue
            if any(p.GetAttribute() == pcbnew.PAD_ATTRIB_PTH and ol.PointInside(p.GetPosition()) for _, p in pads):
                continue
            inside = [(r, p) for r, p in pads if p.IsOnLayer(pcbnew.F_Cu) and ol.Collide(p.GetPosition(), pcbnew.FromMM(0.4))]
            if inside:
                todo.append(inside[0])
        todo += [(r, p) for r, p in pads if (r, p.GetNumber()) in drc_pads(gen_pcb.PCB, tmp)]
        todo = [(r, p) for r, p in todo if (r, p.GetNumber()) not in done]
        if not todo:
            break
        n = 0
        seen = set()
        for r, p in todo:
            key = (r, p.GetNumber())
            if key in seen:
                continue
            seen.add(key); done.add(key)
            try:
                msg = patch2.route_net(board, "GND", (board_mm(p.GetPosition()), board_mm(ref_pt)),
                                       width=0.3, lock=True, heur=False,
                                       start_layers=(0,), window=3.0)
                print("%s.%s: %s" % (r, p.GetNumber(), msg)); n += 1
            except RuntimeError as e:
                print("%s.%s: %s" % (r, p.GetNumber(), e))
            pcbnew.ZONE_FILLER(board).Fill(board.Zones())
        board.Save(gen_pcb.PCB)
        print("round %d: %d ties" % (rnd, n))
        if not n:
            break
    gen_pcb.write_project()

if __name__ == "__main__":
    main()
