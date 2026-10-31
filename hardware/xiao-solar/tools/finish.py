#!/usr/bin/env python3
"""Second Freerouting pass on the routed board: the existing copper goes into the DSN, so
the router only has to finish the few connections the first pass left open. The routed
tracks and vias are then replaced by the session result (hand-routed copper is kept), and refill.py runs again.

Run: FREEROUTING_JAR=/path/freerouting-1.9.0.jar python3 tools/finish.py
"""
import os, subprocess, sys, tempfile
import pcbnew
HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.append(HERE)
import dfm, dsn_classes, refill, route  # noqa: E402

def main():
    jar = os.environ["FREEROUTING_JAR"]
    tmp = tempfile.mkdtemp()
    dsn, ses = os.path.join(tmp, "xs.dsn"), os.path.join(tmp, "xs.ses")
    board = pcbnew.LoadBoard(route.PCB)
    before = board.GetConnectivity().GetUnconnectedCount(True)
    dfm.remove_teardrops(board)
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    pcbnew.ExportSpecctraDSN(board, dsn)
    dsn_classes.fix(dsn)
    dsn_classes.add_zone_copper(board, dsn)
    dsn_classes.add_keepouts(board, dsn, route.b_clear(board))
    subprocess.run(["xvfb-run", "-a", "java", "-jar", jar, "-de", dsn, "-do", ses, "-mp", "30"],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=900)
    # the session leaves out the fixed (locked, hand-routed) wiring, so that stays on the board
    for t in list(board.GetTracks()):
        if not t.IsLocked():
            board.Delete(t)
    print("ses: %d segments, %d vias" % route.import_ses(board, ses))
    board.Save(route.PCB)
    board = pcbnew.LoadBoard(route.PCB)
    board.BuildConnectivity()
    print("unconnected before/after: %d/%d" % (before, board.GetConnectivity().GetUnconnectedCount(True)))
    board.Save(route.PCB)
    refill.main()

if __name__ == "__main__":
    main()
