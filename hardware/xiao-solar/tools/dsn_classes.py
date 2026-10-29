#!/usr/bin/env python3
"""KiCad 7's Python API cannot set net class patterns, so the exported DSN has every
net in kicad_default. This moves the nets of gen_pcb.NETCLASSES into their classes."""
import re, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_pcb

def quote(n):
    return '"%s"' % n if re.search(r'[^A-Za-z0-9_+./]', n) or n.endswith('-') else n

def fix(path):
    s = open(path).read()
    m = re.search(r'\(class kicad_default ""(.*?)\n\s*\(circuit', s, re.S)
    default = m.group(1)
    toks = re.findall(r'"[^"]*"|\S+', default)
    names = [t.strip('"') for t in toks]
    for cname, (_, _, _, _, nets) in gen_pcb.NETCLASSES.items():
        mine = [n for n in nets if n in names]
        names = [n for n in names if n not in mine]
        s = re.sub(r'\(class %s\b[^\n(]*' % cname, '(class %s %s' % (cname, " ".join(quote(n) for n in mine)), s, count=1)
    s = s.replace(m.group(1), " " + " ".join(quote(n) for n in names), 1)
    open(path, "w").write(s)

if __name__ == "__main__":
    fix(sys.argv[1])
