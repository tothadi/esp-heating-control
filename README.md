# ESP Heating Control v4

An open-source, DIN-rail mounted controller for hydronic underfloor heating: 8 zone valve outputs, 2 pump outputs, a built-in OpenTherm master for the boiler, 8 floor NTC probe inputs, 8 dry-contact thermostat inputs, an RTC, and a front panel with LEDs, an OLED and a button. It is built around an ESP32-C6 and is meant to run ESPHome, with Home Assistant as the user interface. It also works without Home Assistant.

![Exploded view](hardware/renders/asm_exploded_render.png)

| Closed, 230 V side | Closed, low-voltage side |
|---|---|
| ![](hardware/renders/asm_closed_render.png) | ![](hardware/renders/asm_closed_top_render.png) |

> **Status: design complete, not yet built.** Both PCBs are routed and pass DRC, and the enclosure passes a collision check against the 3D board models. Nothing has been manufactured or tested yet, and the v4 firmware is written but untested. See [docs/open-issues.md](docs/open-issues.md).
>
> **Safety:** the lower board switches mains voltage (230 V AC). Build and install it only if you are qualified to work on mains wiring. The printed enclosure materials are not flame-rated (UL94 V-0), so the design is not suitable for sale as is.

## Features

* **Housing:** 10-module DIN-rail enclosure (175 × 90 mm), the usual circuit-breaker profile. 3D printed (MJF nylon) with engraved terminal labels, a printed spring latch for the DIN rail and printed light pipes.
* **Mains side:** 8 zone outputs (L + N each) for 230 V valve actuators and 2 pump outputs. 10 × Omron G5NB relays, HLK-5M05 power supply, fuse and varistor. 5.08 mm screw terminals rated 16 A / 300 V. At least 6.5 mm creepage/clearance between mains and low voltage.
* **Low-voltage side:** 8 floor NTC inputs (2 × ADS1115, 10 kΩ 0.1 % dividers, 10 µF filter capacitors), 8 dry-contact thermostat inputs (ESD-protected, 3.3 mA wetting current), and a discrete OpenTherm master interface (24 V boost, current-limited line driver, polarity-independent). 3.5 mm screw terminals.
* **Logic board:** ESP32-C6-WROOM-1 (Wi-Fi 6, BLE, 802.15.4), DS3231M RTC with CR1220 backup, 0.91" 128 × 32 OLED, 11 status LEDs, one button, and USB-C for flashing and logging.
* **Manufacturing:** both boards are single-side assembled by JLCPCB (BOM and CPL included). The enclosure is printed by JLC3DP, which also fits the threaded inserts. Hand work is limited to soldering the OLED module, fitting the window and final assembly.
* **Parametric:** the PCBs (KiCad) and the enclosure (CadQuery) are generated from Python scripts. The enclosure reads terminal and front-panel positions from the boards, so the two stay in sync.

## Repository layout

| Path | Contents |
|---|---|
| [docs/](docs) | Documentation (see below) |
| [hardware/lower/](hardware/lower) | Power/IO board (170 × 85 mm): KiCad project, Gerbers, JLCPCB BOM/CPL, terminal map |
| [hardware/logic/](hardware/logic) | Logic board (165 × 40 mm): KiCad project, Gerbers, JLCPCB BOM/CPL, front-panel map |
| [hardware/enclosure/](hardware/enclosure) | Enclosure: STL/3MF/STEP, OLED window DXF, threaded-insert drawing, CadQuery scripts |
| [hardware/schematics/](hardware/schematics) | OpenTherm master interface schematic (SVG + generator) |
| [hardware/renders/](hardware/renders) | Board renders, assembly line drawings, shaded renders |
| [hardware/lib/](hardware/lib) | Footprints and 3D models (LCSC/EasyEDA) |
| [hardware/*.py](hardware) | PCB generators and post-processing scripts |
| [firmware/](firmware) | v4 ESPHome firmware (all control runs on the controller) and the predecessor gateway's configuration for reference |
| [home-assistant/](home-assistant) | Home Assistant dashboard (the controller works without Home Assistant; HA is only dashboard and remote control) and optional notifications |

## Documentation

1. [Hardware: boards, pinout, OpenTherm interface](docs/hardware.md)
2. [Enclosure: dimensions, terminals, front panel, mounting, printing](docs/enclosure.md)
3. [Ordering and assembly](docs/assembly.md)
4. [Firmware and control logic](docs/firmware.md)
5. [Regenerating the design (build scripts)](docs/build-scripts.md)
6. [Open issues](docs/open-issues.md)

## Background

v4 grew out of a working home installation: an OpenTherm gateway (ESP32 + DIYLess OpenTherm Master Shield + 2 × ADS1115 on a custom PCB), a Zigbee 6-channel relay for the zone valves, Zigbee room thermostats and Home Assistant automations. v4 puts all of this into one DIN-rail device that can control the heating locally. The Home Assistant packages in [home-assistant/](home-assistant) are the reference implementation of the control logic, which the v4 firmware will port to the ESP.

## License

* Hardware (KiCad files, Gerbers, enclosure models and drawings): [CERN-OHL-S-2.0](LICENSES/CERN-OHL-S-2.0.txt)
* Software (Python scripts, ESPHome and Home Assistant configuration): [MIT](LICENSES/MIT.txt)
* Documentation and images: [CC BY-SA 4.0](LICENSES/CC-BY-SA-4.0.txt)

Third-party content keeps its own license: the Barlow font (SIL OFL 1.1, [hardware/enclosure/fonts/OFL.txt](hardware/enclosure/fonts/OFL.txt)), the KiCad standard library footprints and 3D models (CC BY-SA 4.0 with the KiCad library exception), and the LCSC/EasyEDA footprints and 3D models in [hardware/lib/](hardware/lib) (provided by LCSC, subject to their terms).
