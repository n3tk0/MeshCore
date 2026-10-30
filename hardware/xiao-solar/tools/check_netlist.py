#!/usr/bin/env python3
"""Exports the netlist of a generated schematic with kicad-cli and checks that every net
in the generator's part list came out exactly (same pins, same name).

Run: python3 tools/check_netlist.py path/to/gen_sch.py
"""
import importlib.util, os, re, subprocess, sys, tempfile

def main():
    gen = os.path.abspath(sys.argv[1])
    spec = importlib.util.spec_from_file_location("gs", gen)
    gs = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(gs)
    base = getattr(gs, "base", gs)
    root = os.path.dirname(os.path.dirname(gen))
    sch = os.path.join(root, base.PROJECT + ".kicad_sch")
    want = {}
    for p in gs.parts:
        for pin, net in p["nets"].items():
            if net and not p["ref"].startswith("#"):   # power flags are not in the netlist
                want.setdefault(net, set()).add((p["ref"], pin))
    with tempfile.TemporaryDirectory() as d:
        out = os.path.join(d, "n.net")
        subprocess.run(["kicad-cli", "sch", "export", "netlist", "-o", out, sch], check=True, stdout=subprocess.DEVNULL)
        text = open(out).read()
    got = {}
    for m in re.finditer(r'\(net \(code "?\d+"?\) \(name "([^"]*)"\)(.*?)\)\s*(?=\(net |\)\s*\)\s*$)', text, re.S):
        name = m.group(1).lstrip("/")
        nodes = set(re.findall(r'\(node \(ref "([^"]+)"\) \(pin "([^"]+)"\)', m.group(2)))
        nodes = {n for n in nodes if not n[0].startswith("#PWR")}
        if nodes and not name.startswith("unconnected-"):
            got[name] = nodes
    bad = 0
    for net, pins in sorted(want.items()):
        if got.get(net) != pins:
            bad += 1
            print("MISMATCH", net, "want", sorted(pins), "got", sorted(got.get(net, [])))
    for net, pins in got.items():
        if net not in want:
            bad += 1
            print("EXTRA", net, sorted(pins))
    print("nets checked:", len(want), "mismatches:", bad)
    sys.exit(1 if bad else 0)

if __name__ == "__main__":
    main()
