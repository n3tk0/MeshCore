#!/usr/bin/env python3
"""Builds the JLC 100x100 panel of this board with KiKit and writes its fabrication files:
panel/<board>-panel.kicad_pcb, panel/production/ (Gerber + drill zip, cpl-jlc.csv, bom-jlc.csv).

The copies sit side by side in one row, cut apart by V-cuts (straight lines through the whole
panel, so all copies are the same height), with 5 mm rails top and bottom carrying JLC's
tooling holes, fiducials and the "JLCJLCJLCJLC" order-number spot. Copper keeps 0.4 mm from
every board edge (gen_pcb sets it), as JLC wants from a V-cut line.

Run: python3 tools/panel.py [copies]      (lite: python3 tools/run.py panel [copies])
KiKit comes from $KIKIT (default: kikit on PATH).
"""
import csv, os, re, subprocess, sys, zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.append(HERE)
import pcbnew  # noqa: E402
import export, gen_pcb, gen_sch  # noqa: E402

RAIL = 5.0
PANEL_MAX = 100.0

def copies_that_fit():
    return int(PANEL_MAX // gen_pcb.W)

def panelize(n, out):
    kikit = os.environ.get("KIKIT", "kikit")
    subprocess.run([kikit, "panelize",
                    "--layout", "grid; rows: 1; cols: %d; space: 0mm" % n,
                    "--tabs", "full",
                    "--cuts", "vcuts; clearance: 0.4mm; layer: Edge.Cuts; endprolongation: 0mm; textprolongation: 0mm",
                    "--framing", "railstb; width: %gmm; space: 0mm" % RAIL,
                    "--tooling", "3hole; hoffset: 2.5mm; voffset: 2.5mm; size: 1.152mm",
                    "--fiducials", "3fid; hoffset: 6mm; voffset: 2.5mm; coppersize: 1mm; opening: 2mm",
                    "--text", "simple; text: JLCJLCJLCJLC; anchor: mt; voffset: 2.5mm; hoffset: %gmm; "
                              "hjustify: center; vjustify: center; width: 1mm; height: 1mm" % (gen_pcb.W / 2),
                    "--post", "millradius: 0mm",
                    gen_pcb.PCB, out], check=True, stderr=subprocess.DEVNULL)
    board = pcbnew.LoadBoard(out)
    # KiKit writes a "V-CUT" note next to each cut on the cut layer; outside the outline it
    # would grow the board size JLC prices by, so it goes to the comments layer
    for d in board.GetDrawings():
        if d.GetLayer() == pcbnew.Edge_Cuts and d.GetClass() == "PCB_TEXT":
            d.SetLayer(pcbnew.Cmts_User)
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    board.BuildConnectivity()
    board.Save(out)
    bb = pcbnew.LoadBoard(out).GetBoardEdgesBoundingBox()
    size = (pcbnew.ToMM(bb.GetWidth()) - 0.1, pcbnew.ToMM(bb.GetHeight()) - 0.1)
    print("panel %d x %s: %.1f x %.1f mm" % (n, gen_pcb.PROJECT, size[0], size[1]))
    return size

def fab(pcb, n):
    prod = os.path.join(os.path.dirname(pcb), "production")
    gerb = os.path.join(prod, "gerber")
    os.makedirs(gerb, exist_ok=True)
    for f in os.listdir(gerb):
        os.remove(os.path.join(gerb, f))
    export.run("pcb", "export", "gerbers", "--layers", export.LAYERS, "--subtract-soldermask",
               "--no-x2", "-o", gerb + "/", pcb)
    export.run("pcb", "export", "drill", "--format", "excellon", "--drill-origin", "absolute",
               "--excellon-units", "mm", "--excellon-separate-th", "-o", gerb + "/", pcb)
    z = os.path.join(prod, gen_pcb.PROJECT + "-panel-gerber.zip")
    with zipfile.ZipFile(z, "w", zipfile.ZIP_DEFLATED) as zf:
        for f in sorted(os.listdir(gerb)):
            zf.write(os.path.join(gerb, f), f)
    # every copy keeps the board's references on the silk; JLC needs unique designators in
    # the BOM and CPL, so copy k (left to right) gets "_k"
    fix = {p["ref"]: export.ROT_FIX.get(p["fp"].split(":")[-1], 0)
           for p in gen_sch.parts if p["lcsc"] and not p["dnp"]}
    tmp = os.path.join(prod, "pos.csv")
    export.run("pcb", "export", "pos", "--format", "csv", "--units", "mm", "--side", "both",
               "--use-drill-file-origin", "-o", tmp, pcb)
    rows = [r for r in csv.DictReader(open(tmp)) if r["Ref"] in fix]
    os.remove(tmp)
    first = {}
    for r in rows:
        first[r["Ref"]] = min(first.get(r["Ref"], 1e9), float(r["PosX"]))
    names = {}
    with open(os.path.join(prod, "cpl-jlc.csv"), "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["Designator", "Mid X", "Mid Y", "Layer", "Rotation"])
        for r in rows:
            k = int(round((float(r["PosX"]) - first[r["Ref"]]) / gen_pcb.W)) + 1
            d = "%s_%d" % (r["Ref"], k)
            names.setdefault(r["Ref"], []).append(d)
            w.writerow([d, r["PosX"] + "mm", r["PosY"] + "mm", "Top" if r["Side"] == "top" else "Bottom",
                        "%g" % ((float(r["Rot"]) + fix[r["Ref"]]) % 360)])
    bad = [r for r, v in names.items() if len(v) != n]
    assert not bad, "copies missing for " + " ".join(bad)
    groups, _ = gen_sch.write_bom()
    with open(os.path.join(prod, "bom-jlc.csv"), "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["Comment", "Designator", "Footprint", "LCSC"])
        for (v, fp, l), refs in sorted(groups.items(), key=lambda kv: kv[1][0]):
            w.writerow([v, ",".join(d for r in refs for d in sorted(names[r])), fp, l])
    print("wrote", z, "and the panel BOM/CPL")

if __name__ == "__main__":
    n = int(sys.argv[1]) if len(sys.argv) > 1 else copies_that_fit()
    out = os.path.join(gen_pcb.ROOT, "panel", gen_pcb.PROJECT + "-panel.kicad_pcb")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    w, h = panelize(n, out)
    assert w <= PANEL_MAX and h <= PANEL_MAX, "panel larger than %g mm" % PANEL_MAX
    fab(out, n)
