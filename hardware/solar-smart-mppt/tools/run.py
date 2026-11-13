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
        if z.GetIsRuleArea() and z.GetDoNotAllowVias() and z.Outline().Collide(p, mm(0.35)):
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
# JLC's library has these LEDs (C2297, C84256, C2296) with pad 1 where KiCad's LED_0805 has it:
# no +180 here (checked in JLC's placement preview, 2026-11-11: with +180 D4/D6/D7 came out reversed)
import export  # noqa: E402
# Q3 (HJ8205, LCSC C20069150) sits on the DW01A-style SOT-23-6 footprint, but JLC's model has its
# pins in two columns: CPL rotation = KiCad rotation + 270 (same fix as ROT_LCSC in xiao-solar,
# commit 15a2abe; the shared export.py of this branch has no ROT_LCSC, so it is keyed by footprint
# here, Q3 is the only part on it). panel.py reads export.ROT_FIX too.
export.ROT_FIX = dict(export.ROT_FIX, LED_0805_2012Metric=0,
                      **{"SOT-23-6_L2.9-W1.6-P0.95-LS2.8-BL": 270})
dfm._Placer.__init__.__defaults__ = ("GND", ("U1", "U4"))
tool = sys.argv.pop(1)
sys.argv[0] = tool + ".py"
if tool == "export":
    # runpy would re-run export.py as __main__ with its own ROT_FIX, without the overrides above
    # (the LED fix of 2026-11-11 was lost on every `run.py export` that way): call the module
    export.gerbers()
    export.cpl()
else:
    runpy.run_module(tool, run_name="__main__")
