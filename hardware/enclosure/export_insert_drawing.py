"""ESP Heating Control v4 – 2D drawing for the JLC3DP threaded insert service (bottom view of the cover, insert positions and types).

Usage:   <cadquery venv>/bin/python hardware/enclosure/export_insert_drawing.py   (after build_enclosure.py)
Input:   cover.stl, inserts.json (written by build_enclosure.py)
Output:  inserts_drawing.pdf and inserts_drawing.png – the drawing to upload with the order (JLC3DP asks for PNG or PDF)
"""
import json
import os
import struct

import matplotlib
import numpy as np

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
from matplotlib.collections import PolyCollection  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
J = json.load(open(os.path.join(HERE, "inserts.json")))
W, H = J["W"], J["H"]

# bottom view of the cover (from the open side): the downward-facing faces, view mirrored in X (x' = W - x)
d = open(os.path.join(HERE, "cover.stl"), "rb").read()
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

for h in J["holes"]:
    x, y = W - h["x"], h["y"]
    if h["insert"]:
        ax.add_patch(plt.Circle((x, y), J["insert_d"] / 2, color="#d62728", zorder=5))
        ax.add_patch(plt.Circle((x, y), 5.0, fill=False, ec="#d62728", lw=2, zorder=5))
        txt = f"{h['ref']}: INSERT TYPE 5 (M3*4*5)\nhole Ø{J['insert_d']:.1f} × {J['insert_l']:.0f} mm (modelled)"
    else:
        ax.add_patch(plt.Circle((x, y), 3.0, fill=False, ec="#1f77b4", lw=2, ls="--", zorder=5))
        txt = f"{h['ref']}: NO INSERT\n(Ø{J['pilot_d']:.1f} pilot, screw cuts into PA)"
    dx = 14 if x < W / 2 else -14
    ax.annotate(txt, (x, y), (x + dx, y + (12 if y < H / 2 else -12)), fontsize=11, ha="left" if dx > 0 else "right",
                va="center", color="#d62728" if h["insert"] else "#1f77b4",
                arrowprops=dict(arrowstyle="-", color="gray", lw=1), zorder=6)
    # dimensions from the enclosure corner (bottom-left corner of the view)
    ax.annotate("", (0, y), (x, y), arrowprops=dict(arrowstyle="<->", color="black", lw=0.6))
    ax.text(x / 2, y + 1.0, f"{x:.1f}", fontsize=8, ha="center")
    ax.annotate("", (x, 0), (x, y), arrowprops=dict(arrowstyle="<->", color="black", lw=0.6))
    ax.text(x + 1.0, y / 2, f"{y:.1f}", fontsize=8, rotation=90, va="center")

ax.text(0, H + 14, "ESP Heating Control v4 – cover (cover.stl / cover_color.3mf) – threaded inserts", fontsize=16, weight="bold")
ax.text(0, H + 7, f"VIEW FROM BOTTOM (open side of the cover). 3 × JLC3DP insert Type 5: M3*4*5 (M3, 5 mm long, OD 4.0 mm), "
        f"hole Ø{J['insert_d']:.1f} mm × {J['insert_l']:.0f} mm deep, already in the model.\n"
        f"Inserts go into the end faces of the Ø{J['boss_d']:.1f} mm bosses (red), flush with the face; the boss faces stand "
        f"{J['z_rim'] - J['z_face']:.1f} mm proud of the cover rim. H1 (blue): no insert. Dimensions in mm from the bottom-left corner of this view.",
        fontsize=10, va="top")
ax.set_xlim(-10, W + 10)
ax.set_ylim(-10, H + 20)
ax.set_aspect("equal")
ax.axis("off")
plt.tight_layout()
for ext in ("pdf", "png"):
    plt.savefig(os.path.join(HERE, f"inserts_drawing.{ext}"), dpi=150)
print("done: inserts_drawing.pdf, inserts_drawing.png")
