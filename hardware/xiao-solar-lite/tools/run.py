#!/usr/bin/env python3
"""Runs one of the shared board tools of ../xiao-solar/tools on this board:
python3 tools/run.py route|finish|refill|export|patch_route [args]
Our tools dir comes first on sys.path, so the shared tools pick up this board's
gen_sch.py / gen_pcb.py."""
import os, runpy, sys
HERE = os.path.dirname(os.path.abspath(__file__))
SHARED = os.path.join(os.path.dirname(HERE), "..", "xiao-solar", "tools")
sys.path[:0] = [HERE, os.path.abspath(SHARED)]
tool = sys.argv.pop(1)
sys.argv[0] = tool + ".py"
runpy.run_module(tool, run_name="__main__")
