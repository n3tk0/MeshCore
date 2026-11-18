#!/usr/bin/env python3
"""Applies gen_pcb.silk_fixups() to the routed board (silkscreen only; no copper is touched).
Run: python3 tools/run.py silk_fix   (do not run it twice: the label texts would be added again)"""
import os, sys
import pcbnew
HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.append(HERE)
import gen_pcb  # noqa: E402
b = pcbnew.LoadBoard(gen_pcb.PCB)
gen_pcb.silk_fixups(b)
b.Save(gen_pcb.PCB)
print("silk fixups applied")
