#!/usr/bin/env python3
"""Second Freerouting pass on the routed board: the existing copper goes into the DSN, so
the router only has to finish the few connections the first pass left open. All tracks
and vias are then replaced by the session result, and refill.py runs again.

Run: FREEROUTING_JAR=/path/freerouting-1.9.0.jar python3 tools/finish.py
"""
import os, subprocess, sys, tempfile
import pcbnew
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import dsn_classes, refill, route  # noqa: E402

def main():
    jar = os.environ["FREEROUTING_JAR"]
    tmp = tempfile.mkdtemp()
    dsn, ses = os.path.join(tmp, "xs.dsn"), os.path.join(tmp, "xs.ses")
    board = pcbnew.LoadBoard(route.PCB)
    before = board.GetConnectivity().GetUnconnectedCount(True)
    pcbnew.ExportSpecctraDSN(board, dsn)
    dsn_classes.fix(dsn)
    subprocess.run(["xvfb-run", "-a", "java", "-jar", jar, "-de", dsn, "-do", ses, "-mp", "100"],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    for t in list(board.GetTracks()):
        board.Remove(t)
    print("ses: %d segments, %d vias" % route.import_ses(board, ses))
    board.BuildConnectivity()
    print("unconnected before/after: %d/%d" % (before, board.GetConnectivity().GetUnconnectedCount(True)))
    board.Save(route.PCB)
    refill.main()

if __name__ == "__main__":
    main()
