# Hardware

The controller uses two PCBs connected by a 26-way FFC ribbon:

* the **power/IO board** (`hardware/lower/`, 170 × 85 mm), which covers the whole floor of the enclosure;
* the **logic board** (`hardware/logic/`, 165 × 40 mm), which sits in the front nose directly behind the front panel.

Every input and output sits next to its own screw terminal on the power/IO board. Analog lines and 24 V lines therefore stay short and never pass through the ribbon cable.

| Power/IO board, top | Power/IO board, iso |
|---|---|
| ![](../hardware/renders/lower_top.png) | ![](../hardware/renders/lower_iso_front.png) |

| Logic board, top | Logic board, iso |
|---|---|
| ![](../hardware/renders/logic_top.png) | ![](../hardware/renders/logic_iso.png) |

## 1. Power/IO board (`hardware/lower/`)

### 1.1 Mains section (bottom edge)

* 10 × Omron G5NB relays, each placed directly above its own L terminal.
* HLK-5M05 AC/DC module. It straddles the isolation gap, and the cover has a recess above it.
* T2A fuse (Walter TR5, footprint-compatible with the Littelfuse 372) and a 07D471K varistor.
* Screw terminals with 8 + 8 + 4 + 2 poles: Z1–Z4, Z5–Z8, P1–P2 and the L/N supply (DORABO DB128V-5.08: C5143711 / C2915641 / C2915639). They sit on the bottom edge of the board (pad row at y = 79.4 mm).

### 1.2 Isolation

* A copper-free band of at least 6.5 mm (KiCad keep-out zone) between mains and low voltage.
* DRC rules: 6 mm between mains and low-voltage nets, 2 mm between mains poles.

### 1.3 Low-voltage section (top edge)

Screw terminals on the top edge of the board (pad row at y = 3.6 mm): 8 × 4-pole + 1 × 2-pole 3.5 mm terminals (XINLAIYA XY350V-3.5: C557656 / C784942) for NTC1–8 + GND, TERM1–8 + GND and OpenTherm A/B. Below them:

* **Floor NTC measurement:** 2 × ADS1115 (I²C 0x48, 0x49) with dividers: 3V3 – 10 kΩ 0.1 % – input – NTC – GND, and a 10 µF X5R capacitor from each input to GND.
  * The 10 µF capacitors (C21–C28) replaced the original 100 nF parts. Probes often run in old thermostat cables next to 230 V conductors and pick up 50 Hz hum: on the predecessor board the raw input voltage jumped by ±47 mV, about ±1.2 °C. With roughly 5 kΩ source impedance (10 kΩ ∥ 10 kΩ NTC) the RC time constant is about 50 ms, which attenuates 50 Hz by about 24 dB. The firmware adds a median filter on top (see [firmware.md](firmware.md)).
  * Two chips are enough: they sample all 8 channels in about 35 ms, so with one reading per minute they are busy less than 1 % of the time. Addresses 0x4A and 0x4B are free if more inputs are ever needed.
* **Thermostat inputs (dry contact):** ESD protection (2 × SRV05-4), 1 kΩ pull-up (≈ 3.3 mA wetting current), series 1 kΩ + 100 nF. They go to MCP23017 #2 (0x21). GPA0–7 are TERM8–TERM1: the order is reversed to keep the routing free of crossings, so the firmware has to map it. GPB drives the zone LEDs on the logic board.
* **OpenTherm master:** see the [schematic](../hardware/schematics/opentherm_master.svg) and section 3.
* **Relay drivers:** MCP23017 #1 (0x20) + 2 × ULN2003A.

### 1.4 FFC connector to the logic board (J30)

26-pin, 1.0 mm pitch, 90° JUSHUO AFA07-S26FCA (LCSC C2681942). Its opening faces the logic board.

| Pin | Signal |
|---|---|
| 1–2 | +5V |
| 3–4 | GND |
| 5 | SDA |
| 6 | SCL |
| 7 | OT_TX |
| 8 | OT_RX |
| 9–16 | Zone LED 1–8 |
| 17 | MCP_INT |
| 18–24 | GND |
| 25 | +3V3 (supplied by the logic board) |
| 26 | GND |

## 2. Logic board (`hardware/logic/`)

It sits in the nose of the enclosure, directly behind the front panel (Z = 44 mm above the base).

* **MCU:** ESP32-C6-WROOM-1-N8. The antenna is at the left edge of the board, over a copper-free area. AMS1117-3.3 regulator; USB-C with VBUS through an SS34 diode.
* **RTC:** DS3231MZ with a CR1220 backup cell (BS-12 holder). The holder's polarity is not documented, so the cell is connected through a series Schottky diode: if the cell goes in reversed, nothing is damaged and there is simply no backup.
* **OLED:** a 0.91" module (SSD1306, I²C 0x3C) on a 4 mm printed spacer, mounted on a 1 × 4 pin header (J3: PZ254V-11-04P, LCSC C492403, soldered by JLCPCB). **The module itself must be soldered by hand**: JLCPCB supplies it without pins, so they cannot place it, and a bare FPC display is not available as a part. Any common 0.91" module works (e.g. HS91L02W2C01, C5248081). Pin order with the pins bent left, bottom to top: GND, VCC, SCL, SDA. The square pad 1 is GND on both the module and the PCB.
* **LEDs and button:** 8 green, 2 yellow and 1 red 0805 LED (330 Ω), plus a 13 mm tactile switch (KH-6X6X13H-TJ) on IO9, which is also BOOT.
* **Single-side assembly:** all parts are on the top side. A 27.5 × 4.5 mm notch in the top edge holds the 90° FFC connector (J1), which sits at the notch edge facing into it. This is why the OLED sits 5.8 mm lower; the front-panel window and the spacer follow it.

### 2.1 ESP32-C6 pin assignment

| GPIO | Function |
|---|---|
| IO6 | I²C SDA |
| IO7 | I²C SCL |
| IO10 | OpenTherm TX |
| IO11 | OpenTherm RX |
| IO12 / IO13 | USB D− / D+ (built-in USB-serial/JTAG) |
| IO9 | Button (BOOT) |
| IO18 / IO19 / IO20 | LED pump 1 / pump 2 / status |
| IO21 | RTC interrupt |
| IO22 | MCP23017 interrupt |

### 2.2 I²C devices

| Address | Device | Board |
|---|---|---|
| 0x20 | MCP23017 #1: relay drivers | power/IO |
| 0x21 | MCP23017 #2: thermostat inputs (GPA), zone LEDs (GPB) | power/IO |
| 0x3C | SSD1306 OLED | logic |
| 0x48 | ADS1115 #1: NTC 1–4 (AIN3…AIN0 = NTC1…NTC4) | power/IO |
| 0x49 | ADS1115 #2: NTC 5–8 (AIN3…AIN0 = NTC5…NTC8) | power/IO |
| 0x68 | DS3231M RTC | logic |

### 2.3 Ribbon cable

26 conductors, 1.0 mm pitch, **type A (contacts on the same side at both ends)**, 80 mm long. It leaves the lower connector, loops back over it, rises vertically through the notch in the logic board and turns horizontally into the upper connector at the top ([render](../hardware/renders/asm_boards_ffc_notch.png)). The notch is open, so the ribbon slides in from the side, and its whole path stays on the low-voltage side. **Why type A:** J30 and J1 are the same part (bottom contact), and each sits on the top side of its board. The ribbon leaves J30 heading +Y with its contact face down. The loop back over the connector turns that face up. Rising through the notch turns it to face +Y, and the final bend into J1 turns it down again. So the same face of the ribbon touches the contacts at both ends. Type B would only be needed if one connector sat on the underside of its board, as in an earlier draft. The ribbon also does not twist sideways, so each conductor keeps its X position. J30 pin *n* therefore meets J1 pin 27 − *n* (J1 is rotated 180°), and the nets are assigned by pad X position. All 26 positions were checked against both board files.

## 3. OpenTherm master interface

[Schematic](../hardware/schematics/opentherm_master.svg) (generated by `hardware/schematics/opentherm_master.py`).

The master supplies the line voltage, and the boiler answers by modulating its current draw.

* **24 V supply:** MT3608 boost converter from 5 V (390 kΩ / 10 kΩ feedback).
* **High level:** Q1 (Nexperia BCX56, SOT-89) as an emitter follower. Its base is held at 19.5 V by a TL431 (CJ431) with a 68 kΩ / 10 kΩ divider.
* **Low level:** the ESP's OT_TX pulls the base down to a 4.3 V Zener through Q3.
* **Current limit:** about 43 mA (R5 15 Ω + Q2), as short-circuit protection.
* **Receive:** a 47 Ω shunt in the return path; Q4 switches above about 12.8 mA and drives OT_RX.
* **Polarity-independent:** 2 × BAT54S bridge, plus an SMAJ24A TVS after the terminal.

Calculated levels at the boiler: high 16.4–17.5 V, low ≈ 2.9 V, receive threshold ≈ 12.8 mA. These still need a prototype test (see [open-issues.md](open-issues.md)).

## 4. Assembly data notes

* **JLCPCB CPL positions:** for KiCad standard footprints the position is the courtyard centre. For LCSC (EasyEDA) footprints it is the footprint origin, which is the JLCPCB part centre. For the AFA07 FFC connector it is the point between the pad row and the mounting-tab row.
* **3D models:** all LCSC models are referenced as STEP. The WRL origins differ, which earlier made the KiCad render and the STEP export disagree. Missing or wrong models are replaced by `pcbgen.MODEL_FIX`: the AFA07 FFC (LCSC model), the ESP32-C6 (the dimensionally identical ESP32-S3-WROOM-1 package) and the push button (KiCad's 13 mm model). The J3 header also carries the OLED module model, for visualisation only.
* The JLCPCB preview and the LCSC package show a generic 5 mm button model; the real part is 13 mm tall.
