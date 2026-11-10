"""Apply the 3D models from pcbgen.MODEL_FIX to a finished board (without rerouting).

Usage:  <KiCad python> hardware/pcb/scripts/fix_models.py <board.kicad_pcb>
"""
import sys

import pcbnew

from pcbgen import fix_models

b = pcbnew.LoadBoard(sys.argv[1])
for fp in b.GetFootprints():
    fix_models(fp)
pcbnew.SaveBoard(sys.argv[1], b)
print("3D models updated:", sys.argv[1])
