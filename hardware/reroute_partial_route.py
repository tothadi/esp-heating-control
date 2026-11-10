"""Second step of reroute_partial.py (in a separate process): Freerouting on the missing connections + post-processing."""
import os
import sys

import pcbnew

from pcbgen import Gen

pcb, jar = sys.argv[1], sys.argv[2]
b = pcbnew.LoadBoard(pcb)
bb = b.GetBoardEdgesBoundingBox()
g = Gen.__new__(Gen)
g.out, g.w, g.h, g.board, g.nets, g.assy, g.tmp_areas = pcb, bb.GetWidth() / 1e6, bb.GetHeight() / 1e6, b, {}, {}, []
g.freeroute(jar, minutes=10)
g.finish()
g.finish(include_gnd=True)
g.dedupe()
g.fill()
g.save()
g.cleanup()
print("done:", pcb, flush=True)
os._exit(0)   # skip cleanup: pcbnew SWIG would crash on exit (macOS error dialog); the board is already saved
