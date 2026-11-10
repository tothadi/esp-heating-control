"""ESP Heating Control – 2D drawing for the JLC3DP threaded insert service (bottom view of the cover, insert positions and types).

Usage:   <cadquery venv>/bin/python hardware/enclosure/scripts/export_insert_drawing.py   (after build_enclosure.py)
Input:   ../models/cover.stl, ../drawings/inserts.json (written by build_enclosure.py)
Output:  ../drawings/inserts_drawing.pdf and .png – the drawing to upload with the order (JLC3DP asks for PNG or PDF)
"""
import json
import os
import struct

import matplotlib
import numpy as np

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
from matplotlib.collections import PolyCollection  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))                     # hardware/enclosure/scripts
ENC = os.path.dirname(HERE)                                            # hardware/enclosure
MODELS, DRAWINGS = (os.path.join(ENC, d) for d in ("models", "drawings"))
J = json.load(open(os.path.join(DRAWINGS, "inserts.json")))
W, H = J["W"], J["H"]

# bottom view of the cover (from the open side): the downward-facing faces, view mirrored in X (x' = W - x)
d = open(os.path.join(MODELS, "cover.stl"), "rb").read()
n = struct.unpack("<I", d[80:84])[0]
a = np.frombuffer(d, dtype=np.dtype([("n", "<3f4"), ("v", "<9f4"), ("a", "<u2")]), count=n, offset=84)
v = a["v"].reshape(-1, 3, 3).astype(float)
down = a["n"][:, 2] < -0.3
v = v[down]
zc = v[:, :, 2].mean(1)
order = np.argsort(-zc)                                   # deeper (farther) faces first
poly = np.stack([W - v[order][:, :, 0], v[order][:, :, 1]], axis=2)
shade = np.interp(zc[order], [J["z_face"] - 1, 60.0], [0.62, 0.95])

fig, ax = plt.subplots(figsize=(16.54, 11.69))            # A3 landscape
ax.add_collection(PolyCollection(poly, facecolors=[(s, s, s) for s in shade], edgecolors="none", antialiased=False))
ax.plot([0, W, W, 0, 0], [0, 0, H, H, 0], color="black", lw=1.2)

# every insert: marker on the view, label outside the outline (left / right column, spread vertically), position in the label
marks = []
for h in J["holes"]:
    marks.append(dict(ref=h["ref"], x=W - h["x"], y=h["y"], color="#d62728" if h["insert"] else "#1f77b4", insert=h["insert"],
                      what="corner boss" if h["insert"] else f"no insert (Ø{J['pilot_d']:.1f} pilot)"))
for h in J.get("logic_holes", []):          # logic board bosses, inside the nose (their faces lie deeper than the corner bosses)
    marks.append(dict(ref=h["ref"], x=W - h["x"], y=h["y"], color="#9467bd", insert=True, what="logic board boss"))
for m in marks:
    if m["insert"]:
        ax.add_patch(plt.Circle((m["x"], m["y"]), J["insert_d"] / 2, color=m["color"], zorder=5))
        ax.add_patch(plt.Circle((m["x"], m["y"]), 5.0, fill=False, ec=m["color"], lw=2, zorder=5))
    else:
        ax.add_patch(plt.Circle((m["x"], m["y"]), 3.0, fill=False, ec=m["color"], lw=2, ls="--", zorder=5))
    inner = m["x"] < W / 2                  # tag beside the marker, towards the middle of the view
    ax.text(m["x"] + (6.5 if inner else -6.5), m["y"], m["ref"], fontsize=9, ha="left" if inner else "right", va="center",
            color=m["color"], weight="bold", zorder=6)
GAP = 11.0                                  # minimum vertical distance between labels in one column (mm)
for left in (True, False):
    col = sorted((m for m in marks if (m["x"] < W / 2) == left), key=lambda m: m["y"])
    ys = [m["y"] for m in col]
    for i in range(1, len(ys)):             # push labels apart upwards, then shift the column back into range
        ys[i] = max(ys[i], ys[i - 1] + GAP)
    over = ys[-1] - (H + 4) if ys else 0
    if over > 0:
        ys = [y - over for y in ys]
        for i in range(len(ys) - 2, -1, -1):
            ys[i] = min(ys[i], ys[i + 1] - GAP)
    for m, ty in zip(col, ys):
        tx = -14 if left else W + 14
        txt = (f"{m['ref']}: " + ("INSERT TYPE 5 (M3*4*5)" if m["insert"] else "NO INSERT") +
               f"\n{m['what']}\nx {m['x']:.1f}  y {m['y']:.1f}")
        ax.annotate(txt, (m["x"], m["y"]), (tx, ty), fontsize=10, ha="right" if left else "left", va="center", color=m["color"],
                    arrowprops=dict(arrowstyle="-", color="gray", lw=0.8, shrinkA=0, shrinkB=6), zorder=6)
# origin of the coordinates
ax.plot([0], [0], "k+", ms=14, mew=1.5)
ax.annotate("0, 0", (0, 0), (-3, -6), fontsize=9, ha="right")

import textwrap
ax.text(-60, H + 30, "ESP Heating Control – cover (cover.stl) – threaded inserts", fontsize=16, weight="bold")
n_ins = sum(1 for m in marks if m["insert"])
no_ins = [m["ref"] for m in marks if not m["insert"]]
desc = (f"VIEW FROM BOTTOM (open side of the cover). {n_ins} × JLC3DP insert Type 5: M3*4*5 (M3, 5 mm long, OD 4.0 mm), "
        f"hole Ø{J['insert_d']:.1f} mm × {J['insert_l']:.0f} mm deep, already in the model. Inserts go into the end faces of the "
        f"Ø{J['boss_d']:.1f} mm bosses, flush with the face. Corner bosses (H, red): faces "
        f"{abs(J['z_rim'] - J['z_face']):.1f} mm {'proud of' if J['z_rim'] > J['z_face'] else 'inside'} the cover rim. "
        f"Logic board bosses (LB, purple): inside the nose, faces {J['z_logic_face'] - J['z_face']:.1f} mm deeper than the corner bosses. "
        f"{(', '.join(no_ins) + ' (blue): no insert. ') if no_ins else ''}"
        f"Positions in mm from the bottom-left corner of this view (0, 0).")
ax.text(-60, H + 22, textwrap.fill(desc, 150), fontsize=10, va="top")
ax.set_xlim(-62, W + 62)
ax.set_ylim(-14, H + 34)
ax.set_aspect("equal")
ax.axis("off")
plt.tight_layout()
for ext in ("pdf", "png"):
    plt.savefig(os.path.join(DRAWINGS, f"inserts_drawing.{ext}"), dpi=150)
print("done: inserts_drawing.pdf, inserts_drawing.png")
