# Enclosure

A 10-module DIN-rail housing with the usual circuit-breaker profile, generated with CadQuery by [`hardware/enclosure/scripts/build_enclosure.py`](../hardware/enclosure/scripts/build_enclosure.py). It reads the terminal positions from `hardware/pcb/lower/terminals.json` and the front-panel layout from `hardware/pcb/logic/front.json`, so it follows changes to the boards.

![Closed, front](../hardware/renders/asm_side_bot_render.png)
![Closed, top](../hardware/renders/asm_side_top_render.png)

## 1. Dimensions

All values are parameters in `build_enclosure.py`. Check them against an existing modular device before printing.

| Dimension | Value | Note |
|---|---|---|
| Width (along the rail) | 175 mm | 10 × 17.5 mm |
| Height (across the rail) | 90 mm | |
| Nose (the part that comes out through the panel cut-out) | 45 mm high, centred | |
| Depth from rail plane to front face | 57.5 mm | same as the Gainta D9MG instrument housing; to be verified |
| Terminal steps | below and above the nose, 27 mm deep | stay under the distribution board cover |
| Rear foot behind the rail plane | 3.5 mm | holds the DIN hook and the latch |
| Wall thickness | 2.0 mm | never less than 1.15 mm on printed parts |

## 2. Screw terminals

* **Top step: low voltage, 3.5 mm screw terminals (XINLAIYA XY350V, M2, 1 mm²), 34 poles:** 8 NTC + GND, 8 thermostat inputs (dry contact) + GND, OpenTherm A/B.
* **RJ45 (LAN):** at the right end of the top step, next to OT: a straight opening the size of the jack's front face (16.2 × 13.5 mm) in the top step wall, with the jack face just behind it; "LAN" is engraved on the step right next to the opening, and below it, after a row of spacing, the S=SIG / G=GND legend of the terminals.
* **Bottom step: 230 V, 5.08 mm screw terminals (DORABO DB128V, M2.5, 2.5 mm², 16 A / 300 V), 22 poles:** L/N supply, 8 zones L + N, 2 pumps L + N.
* **Placement:** the terminals sit on the board edge, with the wire entry less than 1 mm behind the enclosure wall. Each block has one common opening in the wall, only at the height of the wire holes (inside: 2.2 mm high at the top step, 4.4 mm at the bottom). The opening flares outwards (trapezoidal section, 1 mm larger all round on the outer face) to guide the wire in. The top of each step has one hole per screw, with a screwdriver channel printed into the cover down to the screw head (top Ø2.6 mm for a 2.5 mm screwdriver, bottom Ø3.5 mm for a 3 mm screwdriver). Adjacent channels merge into one solid block per terminal block.

### 2.1 Labels

![Top step labels](../hardware/renders/labels_top_step.png)
![Bottom step labels](../hardware/renders/labels_bottom_step.png)

All labels sit below the screwdriver holes, towards the nose (there is only about 4.8 mm of space above the holes).

* **Top step:** holes → **S/G** (signal/GND; A/B for OpenTherm) → a U-shaped line under each pair → **channel number** (1–8) → a U-shaped line for the block group → **title** (NTC, THERMOSTAT, OT). The legend "S=SIG / G=GND" sits at the right end, below the "LAN" label of the RJ45 jack.
* **Bottom step:** holes → **L/N** → a U-shaped line per pair → the pair name (Z1–Z8, P1, P2, 230V).
* Engraved 0.6 mm deep in Barlow Bold (SIL OFL, `hardware/enclosure/fonts/`). Pole letters are 3.4 mm high (≈ 0.51 mm stroke), the other labels have ≈ 0.58 mm strokes, and the U-lines are 0.6 mm wide. `hardware/pcb/scripts/export_labels.py` reads the pole, block and row positions from the power/IO board (`lower/terminals.json`), so the labels follow PCB changes. `BANK_TITLES` replaces the short PCB group name (TERM) with the enclosure label.

## 3. Front panel (the nose)

* **LEDs:** 8 green zone LEDs, 2 yellow pump LEDs and 1 red status LED, with engraved markings below (1–8, P1, P2, ST). Above each LED, a light pipe printed into the cover (Ø3.1 / Ø5.4 mm) reaches down to 0.1 mm above the board, so the LEDs' side emission does not leak into the neighbouring pipe ([section](../hardware/renders/asm_led_section.png)). The pipes work empty (visible head-on), but they are brighter and visible from a wider angle with clear rods pushed in: `led_rods.stl` prints all 11 (Ø2.8 × 10.5 mm, 8001 clear resin) on one carrier bar with thin necks, so the part meets the SLA minimum size. Snip them off and sand both ends matt with fine sandpaper; the rods end flush with the front face. Ø3 mm clear acrylic rod cut to 10.5 mm works as well.
* **OLED (0.91", 128 × 32):** the window matches the active area from the datasheet, plus 1.5 mm all round. A 1 mm clear acrylic (PMMA) or polycarbonate pane (`hardware/enclosure/models/oled_window.dxf`, 27.1 × 9.7 mm) is glued from the inside into a 1 mm deep pocket in the cover. A 1 mm frame covers its edge from the outside. The display glass sits 0.8 mm behind the pane ([section](../hardware/renders/asm_oled_section.png)).
* **Push button** (KH-6X6X13H-TJ, 13 mm tall): protrudes 1.1 mm from the front face through a Ø4.2 mm hole, labelled "OK".
* **USB-C (2 ×, side by side):** two stepped openings in the lower side wall of the nose, labelled "MAIN" (ESP32-S3) and "RADIO" (ESP32-H2) below each, with "USB · FLASH / LOG" to the right of the pair (firmware upload and serial log via each chip's built-in USB-serial port). Outside, a 13.5 × 8 mm, 1.4 mm deep recess per port takes the plug overmould (max. 12.35 × 6.5 mm per the USB-C spec), so the plug seats fully. Inside, a tight rim surrounds each receptacle shell (9.6 × 4.7 mm, 0.3 mm gap all round; the shell's front lip reaches 0.85 mm below the board plane), so there is no large gap through which the PCB is visible. The receptacles overhang the logic board edge by 1.3 mm, and their faces are 1.2 mm behind the outer surface. Once installed, the openings may be under the distribution board cover, depending on the nose length. In that case they are only reachable with the cover removed, close to the 230 V terminals.

## 4. Mounting

* **DIN rail (TS35 × 7.5):** the rail channel runs in the rear foot. A fixed hook runs along the top and grips 2 mm behind the rail's upper flange (chamfered tip). A separate printed spring latch (`din_slider.stl`) grips the lower flange.
  * Mounting: hook onto the rail and tilt the housing in. The latch's ramp moves aside and snaps in.
  * Removal: pull the tab at the bottom 3 mm down with a screwdriver and tilt the housing off.
  * The latch slides into its guide from the bottom of the housing. Its two 1 mm spring arms spread inside the pocket, bear against the stops and hold the latch captive ([renders](../hardware/renders/asm_din_slider.png), [section](../hardware/renders/asm_din_section.png)).

  ![DIN latch](../hardware/renders/asm_din_back.png)
* **Cover and base:** 4 × M3 × 12 screws from the back, from pockets in the foot. Each screw passes through the base boss (Ø3.4 mm hole) and the power/IO board corner hole (Ø3.2 mm, `MountingHole_3.2mm_M3`) into the cover boss, so one screw clamps both the board and the cover. The housing can only be opened when it is off the rail (230 V: live parts are reachable only with a tool). The split between base and cover is 4 mm above the back (below the power/IO board): the base is a shallow tray whose Ø5.6 mm standoffs reach up to the board, and the cover's walls enclose the board, so the terminal windows and the insert bosses are entirely in the cover and nothing of the cover reaches into the base walls.
  * **H1–H4:** threaded inserts fitted by JLC3DP, type 5 (M3\*4\*5: M3, 5 mm long, Ø4.0 mm; hole Ø3.6 × 7 mm, already in the model). JLC3DP asks for at least 3 mm of wall around an insert, so these bosses are Ø9.8 mm with no parts underneath (checked against the board 3D model). H3 was moved up for this (3.5; 69.0, between terminal J21 and relay K1).
  * **H1:** the top terminal row starts at x = 9.0 mm on the board, 0.6 mm clear of the H1 boss (which reaches x = 8.4 mm), so H1 has a full insert boss as well.
  * The 2D drawing required for ordering is `hardware/enclosure/drawings/inserts_drawing.pdf` (and `.png`): the cover seen from below, with insert positions and types. `export_insert_drawing.py` creates it from `cover.stl` and `inserts.json`.
* **Logic board:** 4 × M3 × 6 screws from the board underside into threaded inserts (JLC3DP type 5, M3\*4\*5) in Ø9.8 mm cover bosses on the front-panel side (LB1–LB4 on the insert drawing). The bosses are 9.9 mm long, enough for the 7 mm insert hole, so the board stays where it was; no part on the logic board is within 5.2 mm of a hole. The cover lifts off together with the logic board; unplug the FFC at one end.

![Open](../hardware/renders/asm_open.png)

## 5. Printing

* **Parts:** `base.stl` (base with foot), `cover.stl` (cover), `din_slider.stl` (DIN latch), `oled_spacer.stl` (OLED spacer), `led_rods.stl` (light rods), plus `enclosure.step` (assembly). All are generated from CadQuery.
* **Cover and base: JLC3DP, SLA, CBY resin** (HDT 72–80 °C at 0.45 MPa, 12–20 % elongation at break: the most heat-resistant and the toughest of the JLC3DP resins). Alternatives with lower heat resistance: 9000HE (white; HDT ≈ 60 °C, 7–12 % elongation) or Grey Resin (the usual DIN-device grey; HDT ≈ 59 °C). Order `cover.stl` (single colour) and fill the labels with paint by hand (see Labels).
* **DIN latch and OLED spacer: JLC3DP, MJF PA12-HP** (grey). The latch's spring arms have to flex, which resin is too brittle for. The OLED module is soldered to its header while it lies on the spacer, so the spacer gets the heat of the soldering iron through the module's pads; the resins soften at 50–80 °C, PA12 has an HDT of about 175 °C (0.45 MPa). Both parts are small, so MJF stays cheap.
* **Labels:** engraved 0.6 mm deep (JLC3DP's SLA minimum for engraved detail is 0.5 mm deep and 0.5 mm wide); the letters are thickened so the thinnest strokes are ≈ 0.5 mm. To colour them, brush acrylic paint (or a paint marker) into the engraving and wipe the surface clean with a cloth while the paint is still wet, or sand it lightly once dry.
* **Own FDM printing:** ASA or PETG. Print the base back-down on the bed (the rail channel ceiling is a 35.6 mm bridge) and the latch flat.
* **Wall thickness:** at least 1.15 mm everywhere (the recommended minimum is above 0.8 mm for SLA and about 1 mm for MJF). The exceptions are the 1 mm latch spring arms, which have to flex, and the 0.9 mm thinnest point between the top screwdriver channels (3.5 mm pitch, merged into a block). Warnings about these are acceptable.
* **Flammability and heat:** none of the JLC3DP materials (resins, PA12, FDM plastics) is flame-rated, nor is ASA/PETG, and the resins soften at their HDT: about 72–80 °C for CBY, 50–60 °C for the others. This is acceptable for personal use in a distribution board, but check that the cover stays well below that next to the relays and the HLK-5M05; a product for sale needs a flame-retardant material.

## 6. Collision check

The cover, the spacer and the base were checked against the 3D models of both boards and touch no component, including the Ø9.8 mm insert bosses.

| Exploded (with screws) | Boards and FFC |
|---|---|
| ![](../hardware/renders/asm_exploded.png) | ![](../hardware/renders/asm_boards_ffc.png) |
