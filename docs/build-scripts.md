# Regenerating the design

Everything in `hardware/` is generated from Python scripts. You only need them to change the design. The committed outputs (KiCad boards, Gerbers, BOM/CPL, STL/STEP, renders) are ready to order.

## Requirements

* **KiCad 9 or 10** (the PCB scripts use KiCad's bundled Python with `pcbnew`, plus `kicad-cli`). The path to `kicad-cli` is set in `pcbgen.py` (`CLI`); adjust it for your OS.
* **Freerouting** (optional, `.jar`) for autorouting the low-voltage nets.
* **CadQuery** for the enclosure, **VTK** for the shaded renders, **matplotlib** and **shapely** for drawings and labels. A virtual environment works well:

  ```sh
  python3.11 -m venv .venv && .venv/bin/pip install cadquery vtk matplotlib shapely
  ```

## PCBs

| Script | Purpose |
|---|---|
| `hardware/pcb/scripts/pcbgen.py` | Shared generator library: placement, nets, DRC rules, Freerouting round trip, A* fallback router, JLCPCB BOM/CPL and Gerber export, 3D model fixes (`MODEL_FIX`) |
| `hardware/pcb/scripts/build_lower.py` | Power/IO board → `hardware/pcb/lower/` (board) and `fab/` (Gerbers + ZIP, BOM/CPL) |
| `hardware/pcb/scripts/build_logic.py` | Logic board → `hardware/pcb/logic/` (board, `front.json`) and `fab/` (Gerbers + ZIP, BOM/CPL) |
| `hardware/pcb/scripts/cleanup_stubs.py` | Removes dangling track stubs reported by DRC |
| `hardware/pcb/scripts/fix_unconnected.py` | Routes the remaining connections with a larger-window A* (`CLR=` sets the clearance margin) |
| `hardware/pcb/scripts/fix_models.py` | Re-applies 3D model fixes to a finished board |
| `hardware/pcb/scripts/reroute_partial.py`, `reroute_partial_route.py` | Moves footprints on a finished board and reroutes only the affected nets |
| `hardware/pcb/scripts/export_labels.py` | Exports terminal label positions from the power/IO board for the enclosure |

Both boards are routed with `ROUTE_GND=1`: Freerouting then routes GND as tracks as well (by default GND is left to the copper pour, and on the denser boards the router fenced some GND pads in so the pour could not reach them); the pour is added afterwards. The logic board uses a 0.15 mm and the power/IO board a 0.12 mm minimum track width, because Freerouting necks tracks down between the fine-pitch module and W5500 leads (JLCPCB's minimum is 0.1 mm). Freerouting 2.x needs Java 25 (`FR_JAVA` points to it). After routing, check DRC; any leftover connection can be routed with `fix_unconnected.py <board> 0.05 15` (finer grid).

`RESUME=1` resumes from the checkpoint saved after Freerouting (`*_routed.kicad_pcb`). The router gives different results on each run, so always check DRC after regenerating (`kicad-cli pcb drc`). The 230 V tracks are placed by hand in the script and locked; only the low-voltage part is autorouted.

## Enclosure

| Script | Purpose |
|---|---|
| `hardware/enclosure/scripts/build_enclosure.py` | Base, cover, DIN latch, OLED spacer and window, LED light rods, `enclosure.step` |
| `hardware/enclosure/scripts/export_insert_drawing.py` | Threaded insert drawing (`inserts_drawing.pdf/.png`) from `cover.stl` and `inserts.json` |
| `hardware/enclosure/scripts/render_assembly.py` | Assembly line drawings (`hardware/renders/asm_*.svg`, converted to PNG); needs the board STEP files from `kicad-cli pcb export step --subst-models` |
| `hardware/enclosure/scripts/export_render_parts.py` | Writes enclosure STLs in assembly position for `render_nice.py` |
| `hardware/enclosure/scripts/render_nice.py` | Shaded colour renders with VTK (board GLBs from `kicad-cli pcb export glb` + enclosure STLs) |

## Schematic

`python3 hardware/pcb/schematics/opentherm_master.py` regenerates `opentherm_master.svg`. The schematic is drawn by script; the KiCad boards are generated directly from the net lists in the build scripts, without a KiCad schematic.

## Board renders

The board images in `hardware/renders/` were made with `kicad-cli pcb render`, for example:

```sh
kicad-cli pcb render -w 1544 -h 952 --background opaque --quality high --rotate '-55,0,20' --zoom 1.1 \
  -o hardware/renders/lower_iso_front.png hardware/pcb/lower/lower.kicad_pcb
```
