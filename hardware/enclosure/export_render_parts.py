"""Input for render_nice.py: the enclosure parts as STL (in exploded position) + layout.json.

Usage:  <cadquery venv>/bin/python hardware/enclosure/export_render_parts.py <dir>
The coloured GLBs of the boards must go into the same directory (kicad-cli pcb export glb, see render_nice.py).
"""
import json
import os
import sys

import cadquery as cq

OUT = sys.argv[1]
sys.argv = sys.argv[:1]
import build_enclosure as E  # noqa: E402

os.makedirs(OUT, exist_ok=True)
DZ = 70.0
OFF = {"lower": DZ, "logic": 2 * DZ, "cover": 3 * DZ + 40.0, "screws": -25.0}


def put(shape, name, dz=0.0):
    cq.exporters.export(cq.Workplane().add(shape.translate(cq.Vector(0, 0, dz))), os.path.join(OUT, name + ".stl"),
                        tolerance=0.05, angularTolerance=0.15)


put(E.base.val(), "base")
put(E.slider.val(), "slider")
put(E.cover.val(), "cover", OFF["cover"])
put(E.window.val(), "window", OFF["cover"])
put(E.spacer.val(), "spacer", OFF["logic"])
screws, inserts = [], []
for i, (x, y) in enumerate(E.CORNERS):
    yp = E.H - y
    screws.append(cq.Workplane("XY").workplane(offset=-1.8).center(x, yp).circle(2.75).extrude(1.8)
                  .faces(">Z").workplane().circle(1.5).extrude(12.0).faces("<Z").workplane().hole(2.0, 1.0).val())
    if i not in E.BOSS_FLAT:
        inserts.append(cq.Workplane("XY").workplane(offset=E.LOWER_Z + E.PCB_T).center(x, yp).circle(2.0).circle(1.5)
                       .extrude(5.0).val())
put(cq.Compound.makeCompound(screws), "screws", OFF["screws"])
put(cq.Compound.makeCompound(inserts), "inserts", OFF["cover"])
json.dump({"H": E.H, "LOWER_Z": E.LOWER_Z, "UPPER_Z": E.UPPER_Z, "origin": E.FRONT["origin"], "off": OFF, "W": E.W,
           "DEPTH": E.DEPTH}, open(os.path.join(OUT, "layout.json"), "w"))
print("exported:", OUT)
