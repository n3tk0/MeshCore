#!/usr/bin/env python3
"""Writes the JLCPCB fabrication files from xiao-solar.kicad_pcb:
production/gerber/ (Gerber + Excellon), production/xiao-solar-gerber.zip,
production/cpl-jlc.csv (placement) and production/bom-jlc.csv (from gen_sch.py).

Run: python3 tools/export.py   (needs kicad-cli 7)
"""
import csv, os, shutil, subprocess, sys, zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
PCB = os.path.join(ROOT, "xiao-solar.kicad_pcb")
OUT = os.path.join(ROOT, "production")
GERB = os.path.join(OUT, "gerber")
LAYERS = "F.Cu,B.Cu,F.Paste,B.Paste,F.SilkS,B.SilkS,F.Mask,B.Mask,Edge.Cuts"

def run(*args):
    subprocess.run(["kicad-cli", *args], check=True, stdout=subprocess.DEVNULL)

def gerbers():
    shutil.rmtree(GERB, ignore_errors=True)
    os.makedirs(GERB)
    run("pcb", "export", "gerbers", "--layers", LAYERS, "--subtract-soldermask",
        "--no-x2", "-o", GERB + "/", PCB)
    run("pcb", "export", "drill", "--format", "excellon", "--drill-origin", "absolute",
        "--excellon-units", "mm", "--excellon-separate-th", "-o", GERB + "/", PCB)
    z = os.path.join(OUT, "xiao-solar-gerber.zip")
    with zipfile.ZipFile(z, "w", zipfile.ZIP_DEFLATED) as zf:
        for f in sorted(os.listdir(GERB)):
            zf.write(os.path.join(GERB, f), f)
    print("wrote", z)

def cpl():
    """KiCad position file -> JLC CPL (Designator, Mid X, Mid Y, Layer, Rotation).
    Only parts with an LCSC number (the ones JLC places) are kept."""
    sys.path.insert(0, HERE)
    import gen_sch  # noqa: E402
    jlc = {p["ref"] for p in gen_sch.parts if p["lcsc"] and not p["dnp"]}
    tmp = os.path.join(OUT, "pos.csv")
    run("pcb", "export", "pos", "--format", "csv", "--units", "mm", "--side", "both",
        "--use-drill-file-origin", "-o", tmp, PCB)
    rows = list(csv.DictReader(open(tmp)))
    os.remove(tmp)
    out = os.path.join(OUT, "cpl-jlc.csv")
    with open(out, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["Designator", "Mid X", "Mid Y", "Layer", "Rotation"])
        for r in rows:
            if r["Ref"] in jlc:
                w.writerow([r["Ref"], r["PosX"] + "mm", r["PosY"] + "mm",
                            "Top" if r["Side"] == "top" else "Bottom", r["Rot"]])
    print("wrote", out)

if __name__ == "__main__":
    gerbers()
    cpl()
