# Ordering and assembly

## 1. Ordering

### 1.1 PCBs (JLCPCB)

For each board, upload:

| Board | Gerber | BOM | CPL |
|---|---|---|---|
| Power/IO | [`hardware/lower/lower_gerber.zip`](../hardware/lower/lower_gerber.zip) | [`jlcpcb_bom.csv`](../hardware/lower/jlcpcb_bom.csv) | [`jlcpcb_cpl.csv`](../hardware/lower/jlcpcb_cpl.csv) |
| Logic | [`hardware/logic/logic_gerber.zip`](../hardware/logic/logic_gerber.zip) | [`jlcpcb_bom.csv`](../hardware/logic/jlcpcb_bom.csv) | [`jlcpcb_cpl.csv`](../hardware/logic/jlcpcb_cpl.csv) |

* PCB: 2 layers, FR-4, 1.6 mm, HASL lead-free, 1 oz copper.
* Assembly: **Standard PCBA** (both boards have through-hole parts), PCBA side **Top**.
* After uploading, check the rotation of every part in the placement preview, especially the USB-C receptacle, the FFC connectors and the J3 pin header.

### 1.2 Enclosure (JLC3DP)

| Part | File | Material |
|---|---|---|
| Cover | [`cover_color.3mf`](../hardware/enclosure/cover_color.3mf) | MJF PAC-HP (full colour), with the threaded insert service: 3 × type 5 (M3\*4\*5), drawing [`inserts_drawing.pdf`](../hardware/enclosure/inserts_drawing.pdf) |
| Base | [`base.stl`](../hardware/enclosure/base.stl) | MJF PA12-HP |
| DIN latch | [`din_slider.stl`](../hardware/enclosure/din_slider.stl) | MJF PA12-HP |
| OLED spacer | [`oled_spacer.stl`](../hardware/enclosure/oled_spacer.stl) | MJF PA12-HP |

### 1.3 Sourced separately

* 0.91" I²C OLED module (SSD1306, 128 × 32)
* FFC cable: 26 conductors, 1.0 mm pitch, type A (same-side contacts), 80 mm
* 4 × M3 × 12 screws (cover), 4 × M2.5 self-tapping screws (logic board)
* 1 mm clear acrylic or polycarbonate pane for the OLED window (27.1 × 9.7 mm, [`oled_window.dxf`](../hardware/enclosure/oled_window.dxf))
* optional: Ø3 mm clear acrylic rod for the light pipes (11 × 10.5 mm)
* CR1220 coin cell

## 2. Assembly

1. **OLED:** push the spacer onto the J3 header, put the module on top (square pad on square pad) and solder it from above.
   ![OLED spacer](../hardware/renders/asm_oled_spacer.png)
2. **Cover** (JLC3DP fits the threaded inserts): glue the window pane into the inner pocket. Optionally push the acrylic rods into the light pipes.
3. **Logic board** onto the cover bosses on the front-panel side (4 self-tapping screws).
4. **Power/IO board** onto the base bosses. Slide the DIN latch into its guide from below; the spring arms spread in the pocket.
   ![Power/IO board in the base](../hardware/renders/asm_lower_in_base.png)
5. **FFC:** plug it into the lower connector, then slide the ribbon sideways into the notch of the logic board and plug it into the upper connector.
   ![FFC routing](../hardware/renders/asm_boards_ffc_notch.png)
6. **Close** the cover and fit the 4 screws from the back.
7. **Install:** hook onto the rail, tilt in, and connect the wires (screwdriver through the holes on top of the steps).

## 3. Wiring

| Terminal | Connect to |
|---|---|
| NTC 1–8 (S/G) | 10 kΩ NTC floor probes (B = 3950), one per zone, two wires each. Unused inputs read open (> 3.2 V) and report *unknown*. |
| THERMOSTAT 1–8 (S/G) | Dry contacts (relay output of a room thermostat, or a switch). Never connect a voltage. |
| OT (A/B) | OpenTherm boiler terminals; polarity does not matter. |
| 230V (L/N) | Mains supply, protected by a breaker in the distribution board. |
| Z1–Z8 (L/N) | 230 V thermoelectric valve actuators of the zones. |
| P1–P2 (L/N) | Circulation pumps (e.g. floor-circuit mixing pump). Check the relay rating against the pump's inrush current. |

If the NTC probes run in existing multi-core cables next to 230 V conductors, isolate the used cores from 230 V at **both** ends and mark them. The 10 µF input capacitors and the firmware median filter handle the 50 Hz pickup.
