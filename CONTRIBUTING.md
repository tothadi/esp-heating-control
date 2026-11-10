# Contributing

Contributions of any kind are welcome: bug reports, questions, corrections to the documentation, firmware fixes, and
design changes.

The project has not been built or tested yet (see [docs/open-issues.md](docs/open-issues.md)), so the most valuable
help right now is a **second pair of eyes on the hardware**. If you have experience with PCB design, mains circuits,
OpenTherm, Ethernet or ESP32 hardware, a review before the first boards are ordered can save a lot of time and money.

## Reviewing the boards and the circuitry

Both boards are complete KiCad projects (`.kicad_pro` + `.kicad_pcb`, KiCad 9 or 10) in `hardware/pcb/lower/` and
`hardware/pcb/logic/`. Open them in KiCad to inspect the layout, the nets of every pad, the design rules and the 3D
view, or to run DRC. The projects have no schematic sheet (`.kicad_sch`): the boards are generated from Python
scripts, and the circuits are defined there as net lists next to the part placement. For a review, these are the
places to look:

| What | Where |
|---|---|
| KiCad projects (layout, nets, DRC, 3D view) | [hardware/pcb/lower/](hardware/pcb/lower) (`lower.kicad_pro`), [hardware/pcb/logic/](hardware/pcb/logic) (`logic.kicad_pro`) |
| Circuit description, pin assignments, design decisions | [docs/hardware.md](docs/hardware.md) |
| Power/IO board: parts, nets, placement | [hardware/pcb/scripts/build_lower.py](hardware/pcb/scripts/build_lower.py) |
| Logic board: parts, nets, placement | [hardware/pcb/scripts/build_logic.py](hardware/pcb/scripts/build_logic.py) |
| OpenTherm master interface | [schematic (SVG)](hardware/pcb/schematics/opentherm_master.svg) |
| Board renders | [hardware/renders/](hardware/renders) (`lower_*`, `logic_*`) |
| Bill of materials (with LCSC part numbers) | `hardware/pcb/*/fab/jlcpcb_bom.csv` |

Areas where a review is especially useful:

* **Mains section:** creepage and clearance between 230 V and low voltage, relay contact ratings, fuse and varistor
  choice, track widths, the HLK-5M05 supply.
* **OpenTherm master interface:** the discrete 24 V boost and line driver, protection, and compliance with the
  OpenTherm physical layer.
* **Ethernet (W5500):** the PHY side, magnetics, termination and layout of the two pairs.
* **ESP32-S3 / ESP32-H2:** strapping pins, reset and boot circuits, USB, antenna keep-out areas.
* **Inputs:** NTC measurement (dividers, filtering, ADS1115) and the dry-contact thermostat inputs (ESD protection,
  wetting current).
* **Power budget and thermal:** the 3.3 V regulator now supplies the S3, the H2 and the W5500.
* **Manufacturability:** footprints, part orientation in the CPL, and whether the chosen LCSC parts are in stock.

The enclosure ([docs/enclosure.md](docs/enclosure.md)) and the firmware ([docs/firmware.md](docs/firmware.md)) are
just as open for review.

## Reporting a problem

Open an issue and include:

* the board (power/IO or logic) and the **reference designators** (e.g. `U30`, `R82`, `J40`) or net names involved;
* what you think is wrong and why, with a datasheet reference if you have one;
* how serious it is: will the board not work, is it a safety issue, or is it an improvement.

**Safety issues** in the mains section take priority. Please say so in the issue title.

If you build the controller or a part of it, test reports are very welcome too, including "it works".

## Changing the design

* **Boards:** change the generator scripts in `hardware/pcb/scripts/`, not the `.kicad_pcb` files. Hand edits are
  lost the next time the boards are generated. Small fixes on a routed board can be done with
  `reroute_partial.py` / `fix_unconnected.py`. See [docs/build-scripts.md](docs/build-scripts.md).
* **Enclosure:** change `hardware/enclosure/scripts/build_enclosure.py`. It reads the terminal and front-panel
  positions from the boards, so regenerate the boards first if they changed.
* **Regenerate the outputs** your change affects (boards, Gerbers, BOM/CPL, STL/STEP, drawings, renders) and commit
  them together with the script change, so the repository always contains a consistent, orderable set.
* Run KiCad's DRC on changed boards and mention the result in the pull request.
* **Firmware:** say which firmware you changed (ESPHome controller, Thread/Matter, H2 RCP, Zigbee coordinator) and
  whether it compiles; the README of each firmware lists the toolchain version used.
* Update the documentation in `docs/` when a change affects it.

Small pull requests are easier to review than large ones. For larger changes, opening an issue first to discuss the
approach is a good idea.

## License of contributions

By contributing you agree that your contribution is published under the license of the part you change:
[CERN-OHL-S-2.0](LICENSES/CERN-OHL-S-2.0.txt) for hardware, [MIT](LICENSES/MIT.txt) for software, and
[CC BY-SA 4.0](LICENSES/CC-BY-SA-4.0.txt) for documentation and images (see the [README](README.md#license)).

## Safety

This device switches 230 V mains. Do not test the mains section unless you are qualified to work on mains wiring.
Contributions and reviews are provided "as is", like the rest of the project (see the
[disclaimer](README.md#disclaimer)).
