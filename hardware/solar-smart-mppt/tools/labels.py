# silkscreen labels, shared between gen_pcb.py (build) and the local rework of the board
import pcbnew

def add_labels(board, pad, mm, pt, version="v0.4"):
    """pad(ref, num) -> (x, y) board mm. Returns {label key: (x, y, text)} for the position check."""
    placed = {}
    made = []
    def text(key, s, x, y, size=0.8, angle=0, layer=pcbnew.F_SilkS, h="c", thick=None):
        t = pcbnew.PCB_TEXT(board)
        t.SetText(s); t.SetPosition(pt(x, y)); t.SetLayer(layer)
        if layer == pcbnew.B_SilkS:
            t.SetMirrored(True)
        t.SetTextSize(pcbnew.VECTOR2I(mm(size), mm(size))); t.SetTextThickness(mm(thick or 0.15))
        t.SetTextAngleDegrees(angle)
        t.SetHorizJustify({"c": pcbnew.GR_TEXT_H_ALIGN_CENTER, "l": pcbnew.GR_TEXT_H_ALIGN_LEFT, "r": pcbnew.GR_TEXT_H_ALIGN_RIGHT}[h])
        board.Add(t)
        placed[key] = (x, y, s)
        made.append(t)
        return t
    def line(pts, w=0.15):
        for a, b in zip(pts, pts[1:]):
            s = pcbnew.PCB_SHAPE(board)
            s.SetShape(pcbnew.SHAPE_T_SEGMENT); s.SetStart(pt(*a)); s.SetEnd(pt(*b))
            s.SetLayer(pcbnew.F_SilkS); s.SetWidth(mm(w))
            board.Add(s)
    # header row on top (J6, J5, J7; JST XH housings, F.Fab outlines): + / - and names inside the housing, below the pads
    j6a, j6b = pad("J6", "1"), pad("J6", "2")
    text("J6+", "+", j6a[0], 2.7, 1.0); text("J6-", "-", j6b[0], 2.7, 1.0)
    text("J6 name", "SYS", (j6a[0] + j6b[0]) / 2, 3.7)
    for i, s_ in enumerate(("SCL", "SDA", "NC", "GND")):
        text("J5 " + s_, s_, pad("J5", str(i + 1))[0], 2.7)
    j7a, j7b = pad("J7", "1"), pad("J7", "2")
    text("J7+", "+", j7a[0], 2.7, 1.0); text("J7-", "-", j7b[0], 2.7, 1.0)
    text("J7 name", "3V3/5V", (j7a[0] + j7b[0]) / 2, 3.7)
    # battery heater (J8) on the right edge next to H3, housing toward the board: + (SYS) / - (FET drain) left of the pads,
    # HEAT rotated beside them
    j8a, j8b = pad("J8", "1"), pad("J8", "2")
    text("J8+", "+", j8a[0] - 1.9, j8a[1], 1.0); text("J8-", "-", j8b[0] - 1.9, j8b[1], 1.0)
    text("J8 name", "HEAT", j8a[0] - 2.9, (j8a[1] + j8b[1]) / 2, 0.8, angle=90)
    # solar input: + / - right of the pads
    j1a, j1b = pad("J1", "1"), pad("J1", "2")
    text("J1+", "+", j1a[0] + 1.9, j1a[1], 1.0); text("J1-", "-", j1b[0] + 1.9, j1b[1], 1.0)
    text("J1 name", "SOLAR", j1a[0] + 0.5, j1a[1] - 2.0)
    j3 = pad("J3", "1")
    text("J3 name", "NTC", j3[0], j3[1] - 1.7)
    # battery: + / - above the outer pads, names below
    j4 = [pad("J4", str(i)) for i in (1, 2, 3)]
    text("J4+", "+", j4[0][0], j4[0][1] - 1.9, 1.0); text("J4-", "-", j4[2][0], j4[2][1] - 1.9, 1.0)
    for s_, p_ in zip(("BAT+", "BM", "BAT-"), j4):
        text("J4 " + s_, s_, p_[0], p_[1] + 1.6)
    # jumpers: what the pads do, next to the pads (bridge = solder blob)
    p3 = [pad("JP5", str(i)) for i in (1, 2, 3)]
    yb = p3[0][1]
    x12, x23 = (p3[0][0] + p3[1][0]) / 2, (p3[1][0] + p3[2][0]) / 2
    line([(p3[0][0], yb + 1.85), (p3[0][0], yb + 2.1), (p3[1][0], yb + 2.1), (p3[1][0], yb + 1.85)])
    text("JP5 ON", "ON", x12, yb + 2.7)
    line([(p3[1][0], yb + 3.3), (p3[1][0], yb + 3.55), (p3[2][0], yb + 3.55), (p3[2][0], yb + 3.3)])
    text("JP5 MCU", "MCU", x23, yb + 4.15)
    text("JP5 name", "J7 EN", p3[1][0], yb + 5.3)
    p6 = pad("JP6", "1"), pad("JP6", "2")
    text("JP6 name", "J7 5V", (p6[0][0] + p6[1][0]) / 2 + 0.2, p6[0][1] + 1.9)
    q = [pad("JP4", str(i)) for i in (1, 2, 3)]
    text("JP4 LFP", "LFP", q[0][0] - 1.0, q[0][1] + 1.4, 0.8, h="r")
    text("JP4 LTO", "LTO", q[2][0] + 1.05, q[2][1] - 0.3, 0.8, h="l")
    text("JP4 name", "CHEM", q[2][0] + 1.7, q[2][1] + 0.65, 0.8, h="l")
    r = pad("JP3", "1"), pad("JP3", "2")
    ym = (r[0][1] + r[1][1]) / 2
    text("JP3 a", "NO SHIP FET", r[0][0] - 2.6, ym + 2.6, 0.8, angle=90)
    text("JP3 b", "LFP: close", r[0][0] - 3.6, ym + 2.6, 0.8, angle=90)
    return placed, made

def thicken_silk(fp, w=0.15):
    """Footprint silk graphics and texts at least w mm wide (JLC: 0.15 mm lines)."""
    for g in fp.GraphicalItems():
        if g.GetLayer() in (pcbnew.F_SilkS, pcbnew.B_SilkS):
            if isinstance(g, pcbnew.PCB_SHAPE) and g.GetWidth() < pcbnew.FromMM(w):
                g.SetWidth(pcbnew.FromMM(w))
            elif isinstance(g, pcbnew.PCB_TEXT) and g.GetTextThickness() < pcbnew.FromMM(w):
                g.SetTextThickness(pcbnew.FromMM(w))

def place_refs(board, dfm, texts, **kw):
    """dfm.place_silk moves board texts that are not clear; the labels above stand where the pads
    are, so they go in as plain boxes while the references are placed, then come back unchanged."""
    from shapely.geometry import box as _box
    boxes = []
    for t in texts:
        bb = t.GetBoundingBox()
        board.Remove(t)
        r = pcbnew.PCB_SHAPE(board)
        r.SetShape(pcbnew.SHAPE_T_RECT); r.SetStart(bb.GetPosition()); r.SetEnd(pcbnew.VECTOR2I(bb.GetRight(), bb.GetBottom()))
        r.SetLayer(pcbnew.F_SilkS); r.SetWidth(0); r.SetFilled(False)
        board.Add(r); boxes.append(r)
    hidden = dfm.place_silk(board, **kw)
    for r in boxes:
        board.Remove(r)
    for t in texts:
        board.Add(t)
    return hidden

VERSION_AT = (14.5, 4.3)
