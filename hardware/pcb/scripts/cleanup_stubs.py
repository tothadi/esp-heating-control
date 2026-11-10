"""Remove dangling track stubs with DRC verification (after Freerouting).

Usage:  <KiCad python> hardware/pcb/scripts/cleanup_stubs.py <board.kicad_pcb>
Each step loads the board in a separate process (pcbnew SWIG cannot handle repeated loads within one process):
it deletes a track that DRC flags as dangling, reruns DRC, and restores it if anything became unconnected as a result.
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile

CLI = os.environ.get("KICAD_CLI", "/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli")
PY = sys.executable
HERE = os.path.dirname(os.path.abspath(__file__))

REMOVE_ONE = r'''
import sys, json, pcbnew
out, rep, idx = sys.argv[1], sys.argv[2], int(sys.argv[3])
fl = []
for v in json.load(open(rep))["violations"]:
    if v["type"] == "via_dangling":
        for it in v["items"]:
            fl.append((it["pos"]["x"], it["pos"]["y"], -1.0, "VIA"))
    if v["type"] == "track_dangling":
        for it in v["items"]:
            try:   # the DRC description may be localized (Hungarian KiCad: "hossza" = "length")
                ln = float(it["description"].split("hossza ")[1].split(" mm")[0]) if "hossza" in it["description"] else \
                     float(it["description"].split("length ")[1].split(" mm")[0])
            except (IndexError, ValueError):
                continue
            fl.append((it["pos"]["x"], it["pos"]["y"], ln, "B.Cu" if "B.Cu" in it["description"] else "F.Cu"))
if idx >= len(fl):
    print("NONE"); sys.exit(0)
x, y, fln, lay = fl[idx]
b = pcbnew.LoadBoard(out)
for t in b.GetTracks():
    if t.IsLocked():
        continue
    if isinstance(t, pcbnew.PCB_VIA):
        if lay == "VIA" and abs(t.GetPosition().x / 1e6 - x) + abs(t.GetPosition().y / 1e6 - y) < 0.01:
            b.Remove(t); pcbnew.SaveBoard(out, b); print("REMOVED"); sys.exit(0)
        continue
    if lay != "VIA" and t.GetLayerName() == lay and abs(t.GetLength() / 1e6 - fln) < 0.002 and \
            min(abs(e.x / 1e6 - x) + abs(e.y / 1e6 - y) for e in (t.GetStart(), t.GetEnd())) < 0.01:
        b.Remove(t); pcbnew.SaveBoard(out, b); print("REMOVED"); sys.exit(0)
print("NOTFOUND")
'''

REFILL = r'''
import sys, pcbnew
b = pcbnew.LoadBoard(sys.argv[1]); pcbnew.ZONE_FILLER(b).Fill(b.Zones()); pcbnew.SaveBoard(sys.argv[1], b)
'''


def drc(pcb, rep):
    subprocess.run([CLI, "pcb", "drc", "--format", "json", "-o", rep, pcb], check=True, capture_output=True)
    return json.load(open(rep))


def main(pcb):
    pcb = os.path.abspath(pcb)
    tmp = tempfile.mkdtemp()
    script = os.path.join(tmp, "rm.py")
    open(script, "w").write(REMOVE_ONE)
    rep, rep2, bak = (os.path.join(tmp, n) for n in ("a.json", "b.json", "bak.kicad_pcb"))
    base_unc = len(drc(pcb, rep)["unconnected_items"])
    idx, gone, kept = 0, 0, 0
    for _ in range(400):
        drc(pcb, rep)
        shutil.copy(pcb, bak)
        r = subprocess.run([PY, script, pcb, rep, str(idx)], capture_output=True, text=True).stdout.strip().splitlines()
        r = r[-1] if r else "NONE"
        if r == "NONE":
            break
        if r != "REMOVED":
            idx += 1
            continue
        if len(drc(pcb, rep2)["unconnected_items"]) > base_unc:
            shutil.copy(bak, pcb)
            idx += 1
            kept += 1
        else:
            gone += 1
    rs = os.path.join(tmp, "refill.py")
    open(rs, "w").write(REFILL)
    subprocess.run([PY, rs, pcb], capture_output=True)
    print(f"stubs: {gone} removed, {kept} kept (needed)")


if __name__ == "__main__":
    main(sys.argv[1])
