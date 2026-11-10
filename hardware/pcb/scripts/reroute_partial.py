"""Partial reroute of a finished board: move the given footprints, delete the tracks leading to them, then run Freerouting
only on the missing connections (all other tracks stay), followed by the usual post-processing (A*, GND, fill, stub cleanup).

Usage:  <KiCad python> hardware/pcb/scripts/reroute_partial.py <board.kicad_pcb> <freerouting.jar> <dx> <dy> <ref1,ref2,...>
"""
import os
import sys

import pcbnew

from pcbgen import MM, Gen

pcb, jar, dx, dy, refs = sys.argv[1], sys.argv[2], float(sys.argv[3]), float(sys.argv[4]), sys.argv[5].split(",")
b = pcbnew.LoadBoard(pcb)
bb = b.GetBoardEdgesBoundingBox()
g = Gen.__new__(Gen)
g.out, g.w, g.h, g.board, g.nets, g.assy, g.tmp_areas = pcb, bb.GetWidth() / 1e6, bb.GetHeight() / 1e6, b, {}, {}, []
old_pads = []
for r in refs:
    fp = b.FindFootprintByReference(r)
    old_pads += [p.GetPosition() for p in fp.Pads()]
    fp.Move(pcbnew.VECTOR2I(MM(dx), MM(dy)))
# delete: tracks attached to the old pads, and every track/via that is in the way of the new pads
new_pads = [p for r in refs for p in b.FindFootprintByReference(r).Pads()]
gone = 0
for t in list(b.GetTracks()):
    if t.IsLocked():
        continue
    pts = [t.GetPosition()] if isinstance(t, pcbnew.PCB_VIA) else [t.GetStart(), t.GetEnd()]
    hit_old = any((q - p).EuclideanNorm() < MM(0.3) for q in pts for p in old_pads)
    hit_new = False
    for p in new_pads:
        r = max(p.GetSizeX(), p.GetSizeY()) // 2 + MM(0.35)
        if isinstance(t, pcbnew.PCB_VIA):
            hit_new = (t.GetPosition() - p.GetPosition()).EuclideanNorm() < r + t.GetWidth() // 2
        else:
            hit_new = t.HitTest(p.GetPosition(), r) and t.GetNetCode() != p.GetNetCode()
        if hit_new:
            break
    if hit_old or hit_new:
        b.Remove(t)
        gone += 1
print("moved:", len(refs), "footprints | tracks deleted:", gone)
pcbnew.SaveBoard(pcb, b)
# pcbnew SWIG cannot handle new objects after many Remove calls in one process: routing runs in a new process
import subprocess  # noqa: E402
rc = subprocess.call([sys.executable, __file__.replace("reroute_partial.py", "reroute_partial_route.py"), pcb, jar])
os._exit(rc)   # skip cleanup: pcbnew SWIG would crash on exit (macOS error dialog); the board is already saved
