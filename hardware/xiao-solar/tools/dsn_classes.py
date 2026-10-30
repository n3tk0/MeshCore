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

def add_zone_copper(board, path, w=0.3, pitch=0.25):
    """KiCad's DSN export leaves zone copper out (its planes do not stop other nets). The
    filled power zones (everything but the GND pours) go into the wiring section as fixed
    wires of their net: the outline plus a hatch of tracks closer than any track width, so
    the router connects to them and routes around them instead of through them."""
    import pcbnew
    from shapely.geometry import Polygon, LineString
    from shapely.ops import unary_union
    wires = []
    def emit(coords, lname, net):
        # repeated points and few-um segments (arcs of the fill) make Freerouting report
        # "offset_shapes: shape is empty" in a modal dialog, and then it hangs: drop them
        if len(coords) > 2:
            coords = list(LineString(coords).simplify(0.02).coords)
        pts = []
        for x, y in coords:
            q = (round(x * 1000), -round(y * 1000))
            if not pts or q != pts[-1]:
                pts.append(q)
        if len(pts) < 2:
            return
        xy = " ".join("%d %d" % q for q in pts)
        wires.append("    (wire (path %s %d %s)(net %s)(type fix))" % (lname, round(w * 1000), xy, quote(net)))
    for z in board.Zones():
        if z.GetIsRuleArea() or z.GetNetname() in ("", "GND"):
            continue
        for layer in z.GetLayerSet().Seq():
            polys = z.GetFilledPolysList(layer).CloneDropTriangulation()
            polys.Fracture(pcbnew.SHAPE_POLY_SET.PM_FAST)
            lname = board.GetLayerName(layer)
            for i in range(polys.OutlineCount()):
                ol = polys.Outline(i)
                pts = [(pcbnew.ToMM(ol.CPoint(k).x), pcbnew.ToMM(ol.CPoint(k).y)) for k in range(ol.PointCount())]
                if len(pts) < 3:
                    continue
                core = Polygon(pts).buffer(0).buffer(-w / 2)
                if core.is_empty:
                    continue
                parts = list(core.geoms) if hasattr(core, "geoms") else [core]
                for part in parts:
                    emit(list(part.exterior.coords), lname, z.GetNetname())
                    x0, y0, x1, y1 = part.bounds
                    lines = []
                    y = y0 + pitch / 2
                    while y < y1:
                        lines.append(LineString([(x0 - 1, y), (x1 + 1, y)])); y += pitch
                    x = x0 + pitch / 2
                    while x < x1:
                        lines.append(LineString([(x, y0 - 1), (x, y1 + 1)])); x += pitch
                    for ln in lines:
                        cut = part.intersection(ln)
                        for seg in (cut.geoms if hasattr(cut, "geoms") else [cut]):
                            if seg.geom_type == "LineString" and seg.length > 0.01:
                                emit(list(seg.coords), lname, z.GetNetname())
    s = open(path).read()
    i = s.index("(wiring") + len("(wiring")
    s = s[:i] + "\n" + "\n".join(wires) + s[i:]
    open(path, "w").write(s)
    return len(wires)

def _poly_str(lname, ps):
    import pcbnew
    out = []
    for i in range(ps.OutlineCount()):
        ol = ps.Outline(i)
        pts = [ol.CPoint(k) for k in range(ol.PointCount())]
        if len(pts) < 3:
            continue
        xy = "  ".join("%d %d" % (round(pcbnew.ToMM(p.x) * 1000), -round(pcbnew.ToMM(p.y) * 1000)) for p in pts)
        out.append("(polygon %s 0  %s)" % (lname, xy))
    return out

def add_keepouts(board, path, b_clear=(), b_cost=3.0):
    """v0.4 routing rules the KiCad export cannot carry:
    - via keepouts on the courtyards of SMD parts (but U1, whose vias are hand placed),
      shrunk by 0.25 mm so a via may touch the courtyard margin but not the part,
    - B.Cu keepouts (tracks and vias) over the given board rectangles (charger, radio),
      so B.Cu stays a whole ground plane there; fixed (hand routed) copper stays,
    - B.Cu trace cost `b_cost` times that of F.Cu, so signals run on top and B.Cu only
      takes short jumps."""
    import pcbnew, dfm
    lines = []
    for ref, cy in dfm.smd_courtyards(board):
        cy = cy.CloneDropTriangulation()
        cy.Deflate(pcbnew.FromMM(0.25), 16)
        for lname in ("F.Cu", "B.Cu"):
            lines += ['    (via_keepout "" %s)' % p for p in _poly_str(lname, cy)]
    for x0, y0, x1, y1 in b_clear:
        ps = pcbnew.SHAPE_POLY_SET(); ps.NewOutline()
        for x, y in ((x0, y0), (x1, y0), (x1, y1), (x0, y1)):
            ps.Append(pcbnew.FromMM(x), pcbnew.FromMM(y))
        lines += ['    (keepout "" %s)' % p for p in _poly_str("B.Cu", ps)]
    s = open(path).read()
    i = s.index("(structure") + len("(structure")
    # an autoroute_settings block replaces Freerouting's defaults: without (vias on) it
    # routes with no vias at all
    rules = ('\n    (autoroute_settings\n      (fanout off) (autoroute on) (postroute on) (vias on) (via_costs 50)'
             ' (plane_via_costs 5) (start_ripup_costs 100) (start_pass_no 1)\n      (layer_rule F.Cu (active on) (preferred_direction horizontal)'
             ' (preferred_direction_trace_costs 1.0) (against_preferred_direction_trace_costs 1.2))\n'
             '      (layer_rule B.Cu (active on) (preferred_direction vertical)'
             ' (preferred_direction_trace_costs %.1f) (against_preferred_direction_trace_costs %.1f))\n    )'
             % (b_cost, b_cost * 1.2))
    j = s.index("(boundary", i)
    s = s[:j] + rules.strip() + "\n    " + "\n".join(lines).strip() + "\n    " + s[j:]
    open(path, "w").write(s)
    return len(lines)
