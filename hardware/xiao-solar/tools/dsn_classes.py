#!/usr/bin/env python3
"""KiCad 7's Python API cannot set net class patterns, so the exported DSN has every
net in kicad_default. This moves the nets of gen_pcb.NETCLASSES into their own class
blocks (track width and clearance from NETCLASSES) right after the default one."""
import re, sys, os
if os.path.dirname(os.path.abspath(__file__)) not in sys.path:
    sys.path.append(os.path.dirname(os.path.abspath(__file__)))
import gen_pcb

def quote(n):
    return '"%s"' % n if re.search(r'[^A-Za-z0-9_+./]', n) or n.endswith('-') else n

def fix(path):
    s = open(path).read()
    m = re.search(r'\(class kicad_default ""(.*?)\n(\s*)\(circuit', s, re.S)
    toks = re.findall(r'"[^"]*"|\S+', m.group(1))
    names = [t.strip('"') for t in toks]
    ind = m.group(2)
    blocks = []
    for cname, (width, clearance, _, _, nets) in gen_pcb.NETCLASSES.items():
        mine = [n for n in nets if n in names]
        names = [n for n in names if n not in mine]
        if mine:
            blocks.append('%s(class %s %s\n%s  (circuit\n%s    (use_via Via[0-1]_600:300_um)\n%s  )\n'
                          '%s  (rule\n%s    (width %d)\n%s    (clearance %d)\n%s  )\n%s)'
                          % (ind[:-2] if len(ind) > 2 else ind, cname, " ".join(quote(n) for n in mine),
                             ind[:-2], ind[:-2], ind[:-2], ind[:-2], ind[:-2], width * 1000, ind[:-2],
                             clearance * 1000, ind[:-2], ind[:-2]))
    s = s[:m.start(1)] + " " + " ".join(quote(n) for n in names) + s[m.end(1):]
    # the default class block ends at the first "\n    )" after its start
    start = s.index('(class kicad_default')
    depth, i = 0, start
    while True:
        if s[i] == "(":
            depth += 1
        elif s[i] == ")":
            depth -= 1
            if depth == 0:
                break
        i += 1
    s = s[:i + 1] + "\n" + "\n".join(blocks) + s[i + 1:]
    open(path, "w").write(s)

if __name__ == "__main__":
    fix(sys.argv[1])
