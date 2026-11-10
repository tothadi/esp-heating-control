"""Post-hoc A* repair of a finished board: route the remaining unconnected items with a larger search window.

Usage:  <KiCad python> hardware/fix_unconnected.py <board.kicad_pcb> [step] [margin] [max_nodes]
"""
import os
import sys

import pcbnew

from pcbgen import Gen

pcb = sys.argv[1]
step, margin, nodes = (float(sys.argv[2]) if len(sys.argv) > 2 else 0.1, float(sys.argv[3]) if len(sys.argv) > 3 else 15.0,
                       int(sys.argv[4]) if len(sys.argv) > 4 else 2000000)
b = pcbnew.LoadBoard(pcb)
bb = b.GetBoardEdgesBoundingBox()
g = Gen.__new__(Gen)
g.out, g.w, g.h, g.board, g.nets, g.assy, g.tmp_areas = pcb, bb.GetWidth() / 1e6, bb.GetHeight() / 1e6, b, {}, {}, []
print("pairs / routed:", g.finish(step=step, margin=margin, max_nodes=nodes, clr=float(os.environ.get("CLR", "0.15"))))
g.dedupe()
g.fill()
g.save()
