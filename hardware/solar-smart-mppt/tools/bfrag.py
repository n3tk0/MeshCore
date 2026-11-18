#!/usr/bin/env python3
"""Ties B.Cu GND pieces cut off from the main B.Cu pour (between two long B.Cu tracks) to the
main F.Cu GND pour with a via, where both pours leave room. Run after refill:
python3 tools/bfrag.py (then tools/run.py refill is not needed; this refills and saves)."""
import os, sys
import pcbnew
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gen_pcb  # noqa: E402

mm = pcbnew.FromMM

def piece(ps, i):
    """Outline i of a poly set with its holes, as its own poly set."""
    out = pcbnew.SHAPE_POLY_SET(); out.AddOutline(ps.Outline(i))
    for h in range(ps.HoleCount(i)):
        out.AddHole(ps.Hole(i, h))
    return out

def main():
    b = pcbnew.LoadBoard(gen_pcb.PCB)
    fill = lambda: pcbnew.ZONE_FILLER(b).Fill(b.Zones())
    fill()
    g = b.FindNet("GND")
    zone = {l: [z for z in b.Zones() if not z.GetIsRuleArea() and z.GetNetCode() == g.GetNetCode() and z.IsOnLayer(l)][0]
            for l in (pcbnew.F_Cu, pcbnew.B_Cu)}
    added = 0
    for _ in range(4):
        fill()
        back = zone[pcbnew.B_Cu].GetFilledPolysList(pcbnew.B_Cu)
        front = zone[pcbnew.F_Cu].GetFilledPolysList(pcbnew.F_Cu)
        bmain = max(range(back.OutlineCount()), key=lambda i: back.Outline(i).Area())
        fmain = max(range(front.OutlineCount()), key=lambda i: front.Outline(i).Area())
        fmain_ps = piece(front, fmain)
        fm = piece(front, fmain); fm.Deflate(mm(0.31), 16)
        vias = [t.GetPosition() for t in b.GetTracks() if t.GetClass() == "PCB_VIA"]
        pads = [p for f in b.GetFootprints() for p in f.Pads()]
        n = 0
        for i in range(back.OutlineCount()):
            if i == bmain:
                continue
            pc = piece(back, i)
            if pc.Area() < mm(1.0) * mm(1.0):
                continue
            # already tied: a via in this piece that also sits in the main F.Cu pour
            if any(pc.Contains(v) and fmain_ps.Contains(v) for v in vias):
                continue
            pc.Deflate(mm(0.31), 16)
            bb = back.Outline(i).BBox()
            done = False
            y = bb.GetY()
            while y < bb.GetBottom() and not done:
                x = bb.GetX()
                while x < bb.GetRight() and not done:
                    p = pcbnew.VECTOR2I(x, y)
                    if (pc.Contains(p) and fm.Contains(p)
                            and all((v - p).EuclideanNorm() > mm(0.9) for v in vias)
                            and not any(pd.GetEffectivePolygon().Collide(p, mm(0.45)) for pd in pads)):
                        v = pcbnew.PCB_VIA(b); v.SetPosition(p); v.SetWidth(mm(0.6)); v.SetDrill(mm(0.3))
                        v.SetNet(g); v.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu); v.SetLocked(True); b.Add(v)
                        vias.append(p); n += 1; done = True
                    x += mm(0.1)
                y += mm(0.1)
        added += n
        if not n:
            break
    fill()
    b.Save(gen_pcb.PCB)
    print("B.Cu piece vias:", added)

if __name__ == "__main__":
    main()
