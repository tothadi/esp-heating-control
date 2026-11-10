"""Enclosure labels: the poles of the power/IO board's screw terminals (position + signal) -> lower/terminals.json.

Usage:  <KiCad python> hardware/pcb/scripts/export_labels.py
Coordinates are in board coordinates (mm, y down); the enclosure generator (hardware/enclosure/scripts/build_enclosure.py) converts them.
"""
import json
import os

import pcbnew

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))   # hardware/pcb/ (the scripts live in hardware/pcb/scripts/)
b = pcbnew.LoadBoard(os.path.join(HERE, "lower", "lower.kicad_pcb"))


def pole_label(net):
    if net.startswith("NTC") or net.startswith("TIN"):
        return net[3:]
    return {"GND": "G", "OT_A": "A", "OT_B": "B", "N_AC": "N", "L_IN": "L"}.get(net, "L" if net.startswith("LOUT") else "?")


poles, groups = [], []
for fp in b.GetFootprints():
    ref = fp.GetReference()
    if not ref.startswith("J") or ref in ("J30", "J40"):     # J30: FFC, J40: RJ45
        continue
    pads = sorted((p for p in fp.Pads() if p.GetNumber().isdigit()), key=lambda p: p.GetPosition().x)
    if not pads:
        continue
    y = pads[0].GetPosition().y / 1e6
    side = "top" if y < 42 else "bottom"
    for p in pads:
        poles.append({"x": round(p.GetPosition().x / 1e6, 3), "side": side, "label": pole_label(p.GetNetname())})
    nets = [p.GetNetname() for p in pads]
    xs = [p.GetPosition().x / 1e6 for p in pads]
    if side == "top":
        if nets[0].startswith("NTC"):
            t = f"NTC {nets[0][3:]}-{nets[2][3:]}"
        elif nets[0].startswith("TIN"):
            t = f"TERM {nets[0][3:]}-{nets[2][3:]}"
        else:
            t = "OT"
        groups.append({"x": round(sum(xs) / len(xs), 3), "side": side, "label": t})
    else:
        for i in range(0, len(pads), 2):           # pairs: L + N
            n = nets[i]
            t = "230V" if n == "L_IN" else (f"Z{n[4:]}" if int(n[4:]) <= 8 else f"P{int(n[4:]) - 8}")
            groups.append({"x": round((xs[i] + xs[i + 1]) / 2, 3), "side": side, "label": t})
rows, blocks = {}, []
for fp in b.GetFootprints():                       # y position of the terminal rows (the screws sit above the pad row) and the blocks
    if fp.GetReference().startswith("J") and fp.GetReference() not in ("J30", "J40"):
        y_ = fp.GetPosition().y / 1e6
        side = "top" if y_ < 42 else "bottom"
        rows[side] = round(y_, 3)
        xs = [p.GetPosition().x / 1e6 for p in fp.Pads() if p.GetNumber().isdigit()]
        blocks.append({"ref": fp.GetReference(), "side": side, "x0": round(min(xs), 3), "x1": round(max(xs), 3)})
rj = b.FindFootprintByReference("J40")          # RJ45 magjack: opening in the top step wall
rj45 = {"x": round(rj.GetPosition().x / 1e6, 3), "w": 16.0, "h": 13.3, "label": "LAN"} if rj else None
json.dump({"poles": poles, "groups": groups, "row_y": rows, "blocks": blocks, "rj45": rj45},
          open(os.path.join(HERE, "lower", "terminals.json"), "w"), indent=1)
print("labels:", len(poles), "poles,", len(groups), "groups")
