"""Close-up images of the engraved labels on the two terminal steps, taken from the finished cover model.

Usage:   <cadquery venv>/bin/python hardware/enclosure/scripts/export_label_views.py
Output:  hardware/renders/labels_top_step.png and labels_bottom_step.png

The cover is cut by a plane half the engraving depth below the step surface: everything engraved (labels, brackets)
and the screwdriver holes appear as openings in that section (the counters of letters as small islands inside them).
Round openings are drawn as holes (black), the rest as engraving (grey), seen from the front as on the installed device (x to the right, the nose towards the bottom of the
top-step image and the top of the bottom-step image).
"""
import math
import os
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt                     # noqa: E402
from matplotlib.collections import PolyCollection  # noqa: E402
from matplotlib.patches import Polygon              # noqa: E402

sys.argv = sys.argv[:1]
import build_enclosure as E                         # noqa: E402

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "renders")
os.makedirs(OUT, exist_ok=True)

z = E.STEP_Z - E.ENGRAVE / 2
faces = E.cq.Workplane().add(E.cover.val() if hasattr(E.cover, "val") else E.cover).section(z).faces().vals()


def tris(w):
    """triangles of the opening bounded by wire w (tessellated face: independent of the edge order in the wire)"""
    v, ix = E.cq.Face.makeFromWires(w).tessellate(0.01, 0.1)
    return [[(v[i].x, v[i].y) for i in tri] for tri in ix], [(p.x, p.y) for p in v]


def is_round(p):
    xs, ys = [a for a, _ in p], [b for _, b in p]
    w, h = max(xs) - min(xs), max(ys) - min(ys)
    if not 1.5 < w < 6 or abs(w - h) > 0.3:
        return False
    cx, cy = (max(xs) + min(xs)) / 2, (max(ys) + min(ys)) / 2
    r = [math.hypot(a - cx, b - cy) for a, b in p]
    return max(r) - min(r) < 0.15


# the steps: physical y bands outside the nose (top step: y > H/2 + NOSE_H/2, bottom step: y < H/2 - NOSE_H/2)
bands = {"top": (E.H / 2 + E.NOSE_H / 2, E.H), "bottom": (0.0, E.H / 2 - E.NOSE_H / 2)}
for name, (y0, y1) in bands.items():
    fig, ax = plt.subplots(figsize=(E.W / 6, (y1 - y0) / 6 + 0.2), dpi=100)
    ax.add_patch(Polygon([(0, y0), (E.W, y0), (E.W, y1), (0, y1)], closed=True, fc="#eeeeee", ec="#999999", lw=1))
    for f in faces:
        for w in f.innerWires():
            tr, p = tris(w)
            yc = sum(b for _, b in p) / len(p)
            if not y0 < yc < y1:
                continue
            c = "#111111" if is_round(p) else "#8a8a8a"
            ax.add_collection(PolyCollection(tr, facecolors=c, edgecolors=c, linewidths=0.2))
    # islands inside the engraving (the counters of O, A, B, 6, 8 …) are separate small faces of the section: paint them
    # in the surface colour on top
    for f in faces:
        if f.Area() > 20.0:
            continue
        tr, p = tris(f.outerWire())
        yc = sum(b for _, b in p) / len(p)
        if y0 < yc < y1:
            ax.add_collection(PolyCollection(tr, facecolors="#eeeeee", edgecolors="#eeeeee", linewidths=0.2, zorder=3))
    ax.set_xlim(0, E.W)
    ax.set_ylim(y0, y1)
    ax.set_aspect("equal")
    ax.axis("off")
    out = os.path.join(OUT, f"labels_{name}_step.png")
    fig.savefig(out, dpi=100, bbox_inches="tight", pad_inches=0.05, facecolor="white")
    plt.close(fig)
    print("done:", out)
