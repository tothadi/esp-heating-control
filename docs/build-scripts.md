# Regenerating the design

Everything in `hardware/` is generated from Python scripts. You only need them to change the design. The committed outputs (KiCad boards, Gerbers, BOM/CPL, STL/3MF/STEP, renders) are ready to order.

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
| `hardware/pcbgen.py` | Shared generator library: placement, nets, DRC rules, Freerouting round trip, A* fallback router, JLCPCB BOM/CPL and Gerber export, 3D model fixes (`MODEL_FIX`) |
| `hardware/build_lower.py` | Power/IO board → `hardware/lower/` |
| `hardware/build_logic.py` | Logic board → `hardware/logic/` |
| `hardware/cleanup_stubs.py` | Removes dangling track stubs reported by DRC |
| `hardware/fix_unconnected.py` | Routes the remaining connections with a larger-window A* (`CLR=` sets the clearance margin) |
| `hardware/fix_models.py` | Re-applies 3D model fixes to a finished board |
| `hardware/reroute_partial.py`, `reroute_partial_route.py` | Moves footprints on a finished board and reroutes only the affected nets |
| `hardware/export_labels.py` | Exports terminal label positions from the power/IO board for the enclosure |

`RESUME=1` resumes from the checkpoint saved after Freerouting (`*_routed.kicad_pcb`). The router gives different results on each run, so always check DRC after regenerating (`kicad-cli pcb drc`). The 230 V tracks are placed by hand in the script and locked; only the low-voltage part is autorouted.

## Enclosure

| Script | Purpose |
|---|---|
| `hardware/enclosure/build_enclosure.py` | Base, cover, DIN latch, OLED spacer and window, `enclosure.step`, and the coloured `cover_color.3mf` |
| `hardware/enclosure/export_insert_drawing.py` | Threaded insert drawing (`inserts_drawing.pdf/.png`) from `cover.stl` and `inserts.json` |
| `hardware/enclosure/render_assembly.py` | Assembly line drawings (`renders/asm_*.png`); needs the board STEP files from `kicad-cli pcb export step --subst-models` |
| `hardware/enclosure/export_render_parts.py` | Writes enclosure STLs in assembly position for `render_nice.py` |
| `hardware/enclosure/render_nice.py` | Shaded colour renders with VTK (board GLBs from `kicad-cli pcb export glb` + enclosure STLs) |

## Schematic

`python3 hardware/schematics/opentherm_master.py` regenerates `opentherm_master.svg`. The schematic is drawn by script; the KiCad boards are generated directly from the net lists in the build scripts, without a KiCad schematic.

## Board renders

The board images in `hardware/renders/` (`lower_*`, `logic_*`) were made with `kicad-cli pcb render`, for example:

```sh
kicad-cli pcb render -w 1544 -h 952 --background opaque --quality high --rotate '-55,0,20' --zoom 1.1 \
  -o hardware/renders/lower_iso_front.png hardware/lower/lower.kicad_pcb
```
