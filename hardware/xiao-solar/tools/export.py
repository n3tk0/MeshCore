#!/usr/bin/env python3
"""Writes the JLCPCB fabrication files from xiao-solar.kicad_pcb:
production/gerber/ (Gerber + Excellon), production/xiao-solar-gerber.zip,
production/cpl-jlc.csv (placement) and production/bom-jlc.csv (from gen_sch.py).

Run: python3 tools/export.py   (needs kicad-cli 7)
"""
import csv, os, shutil, subprocess, sys, zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.append(HERE)
import gen_pcb, gen_sch  # noqa: E402  (the board's own ones when its tools dir comes first)

ROOT, PCB = gen_pcb.ROOT, gen_pcb.PCB
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
    z = os.path.join(OUT, gen_pcb.PROJECT + "-gerber.zip")
    with zipfile.ZipFile(z, "w", zipfile.ZIP_DEFLATED) as zf:
        for f in sorted(os.listdir(GERB)):
            zf.write(os.path.join(GERB, f), f)
    print("wrote", z)

# JLC's part models are drawn in EasyEDA's orientation. These KiCad library footprints are
# rotated against it (checked pad by pad against the EasyEDA footprint of the LCSC part):
# SOT-23 pin 1 sits top left in KiCad and bottom right in EasyEDA; the 0805 LED has pad 1
# (cathode) on the other side. Footprints imported with easyeda2kicad need no correction.
# SOT-23-8 (INA219) checked the same way: EasyEDA has pin 1 bottom right, KiCad top left.
# SC-70-6 (LM66100) is taken from the JLC rotation database, not yet checked against JLC's
# preview: check it there before ordering.
ROT_FIX = {"SOT-23": 180, "LED_0805_2012Metric": 180, "SOT-23-8": 180,
           "TSSOP-16_4.4x5mm_P0.65mm": 270, "SOT-363_SC-70-6": 180}

def cpl():
    """KiCad position file -> JLC CPL (Designator, Mid X, Mid Y, Layer, Rotation).
    Only parts with an LCSC number (the ones JLC places) are kept."""
    jlc = {p["ref"]: ROT_FIX.get(p["fp"].split(":")[-1], 0) for p in gen_sch.parts if p["lcsc"] and not p["dnp"]}
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
                            "Top" if r["Side"] == "top" else "Bottom", "%g" % ((float(r["Rot"]) + jlc[r["Ref"]]) % 360)])
    print("wrote", out)

if __name__ == "__main__":
    gerbers()
    cpl()
