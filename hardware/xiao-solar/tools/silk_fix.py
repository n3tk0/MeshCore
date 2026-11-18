#!/usr/bin/env python3
"""Applies gen_pcb.silk_fixups() to the routed board (silkscreen only; no copper is touched).
Run: python3 tools/silk_fix.py   (safe to repeat: the label texts are replaced, not added again)"""
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
