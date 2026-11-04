#!/usr/bin/env python3
"""Runs one of the shared board tools of ../xiao-solar/tools on this board:
python3 tools/run.py route|finish|refill|export|patch_route [args]
Our tools dir comes first on sys.path, so the shared tools pick up this board's
gen_sch.py / gen_pcb.py."""
import os, runpy, sys
HERE = os.path.dirname(os.path.abspath(__file__))
SHARED = os.path.join(os.path.dirname(HERE), "..", "xiao-solar", "tools")
sys.path[:0] = [HERE, os.path.abspath(SHARED)]
import pcbnew
# KiCad 10's WriteDRCReport needs the GUI program object (asserts in Pgm()); without it the
# shared route.drc() falls back to kicad-cli
if hasattr(pcbnew, "WriteDRCReport"):
    del pcbnew.WriteDRCReport

# This board has no modules on top: vias may go anywhere the fills leave room, except
# headers, the small power ICs and pads. The SOIC-20W body of U4 takes tented vias like U1.
import route, dfm  # noqa: E402
def _free_point(board, p, mm=pcbnew.FromMM):
    for z in board.Zones():
        if not z.GetIsRuleArea() and z.GetNetname() != "GND" and z.Outline().Collide(p, mm(0.6)):
            return False
    for fp in board.GetFootprints():
        ref = fp.GetReference()
        if ref.startswith(("J", "U5", "U6", "U7")):
            bb = fp.GetBoundingBox(False, False)
            bb.Inflate(mm(0.3))
            if bb.Contains(p):
                return False
    return not route._on_pad(board, p)
route.free_point = _free_point
dfm._Placer.__init__.__defaults__ = ("GND", ("U1", "U4"))
tool = sys.argv.pop(1)
sys.argv[0] = tool + ".py"
runpy.run_module(tool, run_name="__main__")
