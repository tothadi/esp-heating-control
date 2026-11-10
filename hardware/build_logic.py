"""ESP Heating Control v4 – logic board (upper board): ESP32-C6, DS3231 RTC, front panel (OLED, 11 LEDs, button), USB-C.
The I/O (NTC measurement, thermostat inputs, OpenTherm interface) is on the power/IO board, next to its screw terminals;
only I²C, OT_TX/OT_RX and the 8 zone LEDs come up the ribbon cable.

Usage:   <KiCad python> hardware/build_logic.py [freerouting-2.x.jar]
Output:  hardware/logic/logic.kicad_pcb (+ gerber, jlcpcb_bom.csv, jlcpcb_cpl.csv, front.json for the front panel)

The board is 165 × 40 mm, in the front nose of the enclosure (top-left corner at enclosure point (5, 25)), top side facing the front panel.
The board-to-board connector sits directly above the power/IO board's connector: nets are matched by the pin
positions of the two connectors (straight ribbon cable).
"""
import json
import os
import sys

import pcbnew

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pcbgen import MM, Gen  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
OUTDIR = os.path.join(HERE, "logic")
os.makedirs(OUTDIR, exist_ok=True)
W, H = 165.0, 40.0
ORIGIN = (5.0, 25.0)                 # top-left corner of the board in the enclosure
LOWER_ORIGIN = (2.5, 2.5)            # same for the power/IO board
g = Gen(os.path.join(OUTDIR, "logic.kicad_pcb"), W, H)

P = {  # value, footprint, LCSC
    "ESP": ("ESP32-C6-WROOM-1-N8", "ESP32-C6-WROOM-1", "C5366877"),
    "USBC": ("TYPE-C-31-M-12", "USB-C-16P", "C165948"),
    "USBLC6": ("USBLC6-2SC6", "SOT-23-6", "C7519"),
    "LDO": ("AMS1117-3.3", "SOT-223", "C6186"),
    "SS34": ("SS34", "D_SMA", "C8678"),
    "ADS": ("ADS1115IDGSR", "MSOP-10", "C37593"),
    "MCP": ("MCP23017-E/SS", "SSOP-28", "C506653"),
    "SRV": ("SRV05-4", "SOT-23-6", "C13612"),
    "RTC": ("DS3231MZ+TRL", "SOIC-8", "C107410"),
    "BAT": ("BS-12-B2AA002 (CR1220)", "BS-12", "C964721"),
    "MT3608": ("MT3608", "SOT-23-6", "C84817"),
    "L22u": ("22uH SWPA4030S220MT", "4x4", "C83472"),
    "Q1": ("BCX56-16,115 (Nexperia)", "SOT-89", "C100026"),
    "NPN": ("MMBT3904", "SOT-23", "C20526"),
    "TL431": ("CJ431", "SOT-23", "C3113"),
    "BAT54S": ("BAT54S", "SOT-23", "C7420333"),
    "Z4V3": ("BZT52C4V3", "SOD-123", "C19077399"),
    "TVS": ("SMAJ24A", "D_SMA", "C19077541"),
    "SW": ("KH-6X6X13H-TJ", "6x6 H13", "C2837523"),
    "HDR4": ("PZ254V-11-04P pin header 1x4 2.54mm (OLED)", "HDR-1x4-P2.54-V", "C492403"),
    "IDC": ("IDC 2x13 box header", "IDC-26P-P2.54", "C9040"),
    "LEDG": ("LED green 0805", "LED_0805", "C2297"),
    "LEDY": ("LED yellow 0805", "LED_0805", "C2296"),
    "LEDR": ("LED red 0805", "LED_0805", "C84256"),
    "R10k01": ("10k 0.1%", "R_0805", "C110775"),
    "R15": ("15R 1%", "R_0805", "C17480"), "R47": ("47R 1%", "R_0805", "C17714"), "R330": ("330R 1%", "R_0805", "C17630"),
    "R1k": ("1k 1%", "R_0805", "C17513"), "R4k7": ("4.7k 1%", "R_0805", "C17673"), "R5k1": ("5.1k 1%", "R_0805", "C27834"),
    "R10k": ("10k 1%", "R_0805", "C17414"), "R68k": ("68k 1%", "R_0805", "C17801"), "R390k": ("390k 1%", "R_0805", "C17656"),
    "R2k2_1206": ("2.2k 1% 1206", "R_1206", "C17948"),
    "C100n": ("100nF 50V X7R", "C_0805", "C49678"), "C1u": ("1uF 50V X7R", "C_0805", "C28323"),
    "C10u": ("10uF 25V X5R", "C_0805", "C15850"), "C22u": ("22uF 25V X5R", "C_0805", "C45783"),
    "C10u50": ("10uF 50V X5R 1206", "C_1206", "C13585"),
}
R0805, C0805 = ("Resistor_SMD", "R_0805_2012Metric"), ("Capacitor_SMD", "C_0805_2012Metric")


def two(ref, key, xy, n1, n2, rot=90, fpkind=None):
    lib, name = fpkind or (R0805 if key.startswith("R") else C0805)
    fp = g.place(lib, name, ref, xy, rot=rot, value=P[key][0], part=P[key])
    g.pin(fp, 1, n1)
    g.pin(fp, 2, n2)
    return fp


def part(lib, name, ref, key, xy, rot, pins, **kw):
    fp = g.place(lib, name, ref, xy, rot=rot, value=P[key][0], part=P[key], **kw)
    for p_, nn in pins.items():
        g.pin(fp, p_, nn)
    return fp


# --- mounting holes (for the cover screw bosses) ----------------------------------------------------
HOLES = [(3.5, 3.5), (3.5, 36.5), (161.5, 3.5), (162.5, 37.5)]
for k, xy in enumerate(HOLES, 1):
    g.hole(f"H{k}", xy)

# --- ESP32-C6-WROOM-1 (90°: antenna to the left, at the board edge; no copper underneath) ----------
# pins: 1 GND, 2 3V3, 3 EN, 6 IO6 SDA, 7 IO7 SCL, 10 IO8 (strap, pulled up), 11 IO10 OT_TX, 12 IO11 OT_RX,
# 13 IO12 USB D-, 14 IO13 USB D+, 15 IO9 button (BOOT), 16 IO18 LED P1, 17 IO19 LED P2, 18 IO20 status LED,
# 19 IO21 RTC INT, 20 IO22 MCP INT, 28 GND, 29-37 thermal pad (GND)
esp = {1: "GND", 2: "+3V3", 3: "EN", 6: "SDA", 7: "SCL", 10: "IO8", 11: "OT_TX", 12: "OT_RX", 13: "USB_DN", 14: "USB_DP",
       15: "BTN", 16: "LED_P1", 17: "LED_P2", 18: "LED_ST", 19: "RTC_INT", 20: "MCP_INT", 28: "GND"}
esp.update({n: "GND" for n in range(29, 38)})
fp = part("lcsc", "WIRELM-SMD_ESP32-C6-WROOM-1", "U1", "ESP", (16.5, 20.0), 90, esp)
for pd in fp.Pads():          # the thermal pad vias (unnumbered pads) are GND too
    if pd.GetNumber() == "":
        pd.SetNet(g.net("GND"))
g.area([(0.2, 8.5), (7.2, 8.5), (7.2, 31.5), (0.2, 31.5)], "ANT")
two("R17", "R10k", (12.5, 31.7), "+3V3", "EN", rot=0)
two("C44", "C1u", (12.5, 33.9), "EN", "GND", rot=0)
two("R18", "R10k", (24.5, 31.7), "+3V3", "IO8", rot=0)
two("C43", "C100n", (9.8, 31.7), "+3V3", "GND", rot=90)
two("C47", "C22u", (9.8, 35.7), "+3V3", "GND", rot=90)

# --- power: AMS1117-3.3, USB-C (VBUS through a diode), ESD ----------------------------------------
part("Package_TO_SOT_SMD", "SOT-223-3_TabPin2", "U2", "LDO", (19.0, 34.9), 90, {1: "GND", 2: "+3V3", 3: "+5V"})
two("C40", "C10u", (24.6, 38.6), "+5V", "GND", rot=0)
two("C41", "C10u", (13.4, 38.6), "+3V3", "GND", rot=0)
usb = {"A4B9": "VBUS", "B4A9": "VBUS", "A1B12": "GND", "B1A12": "GND", "1": "GND", "2": "GND", "3": "GND", "4": "GND",
       "A5": "CC1", "B5": "CC2", "A6": "USB_DP", "B6": "USB_DP", "A7": "USB_DN", "B7": "USB_DN"}
part("lcsc", "USB-C_SMD-TYPE-C-31-M-12_1", "J2", "USBC", (33.0, 36.0), 0, usb)
two("R21", "R5k1", (28.0, 31.4), "CC1", "GND", rot=90)
two("R22", "R5k1", (38.4, 31.4), "CC2", "GND", rot=90)
part("Diode_SMD", "D_SMA", "D1", "SS34", (33.0, 29.8), 0, {1: "+5V", 2: "VBUS"})

# --- FFC on the top side (JUSHUO AFA07, 26P 1.0 mm, right-angle), at the edge of the notch in the top board edge ----
# The ribbon arcs up from the power/IO board connector (directly below), rises vertically through the notch and turns 90°
# into the connector (its opening faces the notch); the notch is open, so the ribbon can be slid in from the side.
# Single-sided assembly only. The ribbon does not twist sideways and the same side faces up at both ends ->
# type A cable (contacts on the same side); nets are matched by the X positions of the two connectors' pads.
lower = pcbnew.LoadBoard(os.path.join(HERE, "lower", "lower.kicad_pcb"))
ljc = lower.FindFootprintByReference("J30")
dx, dy = LOWER_ORIGIN[0] - ORIGIN[0], LOWER_ORIGIN[1] - ORIGIN[1]
lpads = [(p.GetPosition().x / 1e6 + dx, p.GetNetname()) for p in ljc.Pads() if p.GetNetname() and p.GetNumber() != "MP"]
cx = ljc.GetPosition().x / 1e6 + dx
NOTCH = (cx - 13.75, 0.0, cx + 13.75, 4.5)       # 27.5 × 4.5 mm notch in the top edge (the 26-way ribbon is 26 mm wide)
FFC_Y = NOTCH[3] + 1.5 + 4.53                     # opening 1.5 mm from the notch edge (the footprint body extends to -4.53)
for d in list(g.board.GetDrawings()):             # board outline with the notch
    if d.GetLayer() == pcbnew.Edge_Cuts:
        g.board.Remove(d)
outline = [(0, 0), (NOTCH[0], 0), (NOTCH[0], NOTCH[3]), (NOTCH[2], NOTCH[3]), (NOTCH[2], 0), (W, 0), (W, H), (0, H)]
for a_, b_ in zip(outline, outline[1:] + outline[:1]):
    g.line(a_, b_, layer=pcbnew.Edge_Cuts, w=0.1)
fp = g.place("Connector_FFC-FPC", "JUSHUO_AFA07-S26FCA-00_1x26-1MP_P1.0mm_Horizontal", "J1", (cx, FFC_Y), rot=180,
             value="AFA07-S26FCA-00", part=("AFA07-S26FCA-00 FFC 26P 1.0mm", "JUSHUO_AFA07-S26FCA-00", "C2681942"))
for pd in fp.Pads():
    if pd.GetNumber() == "MP":
        pd.SetNet(g.net("GND"))
        continue
    x_ = pd.GetPosition().x / 1e6
    m = min(lpads, key=lambda q: abs(q[0] - x_))
    if abs(m[0] - x_) < 0.05:
        pd.SetNet(g.net(m[1]))
g.area([(NOTCH[0] - 0.6, 0.0), (NOTCH[2] + 0.6, 0.0), (NOTCH[2] + 0.6, NOTCH[3] + 0.6), (NOTCH[0] - 0.6, NOTCH[3] + 0.6)], "FFC_NOTCH")
print("FFC on the logic board (next to the notch): x =", round(cx, 2))

# --- I²C pull-ups (the ADCs, MCPs and the RTC on the power/IO board / here, on one bus) ------------------------
two("R9", "R4k7", (78.0, 12.0), "+3V3", "SDA")      # next to the FFC connector
two("R10", "R4k7", (81.0, 12.0), "+3V3", "SCL")

# --- OLED: 1 × 4 pin header (soldered by JLCPCB), with the 0.91" 128×32 SSD1306 module (I²C 0x3C) on it – the module is hand-soldered ----
# (JLCPCB supplies the OLED module without pins, so it cannot be assembled). Pins: 1 GND, 2 VCC, 3 SCL, 4 SDA – same as common modules.
# The module sits on a 4 mm printed spacer (enclosure/oled_spacer.stl) above the header: the glass is 0.8 mm below the inner face of the front panel.
OLED_DY = 5.81                                  # OLED moved 5.81 mm down so the FFC connector fits above it
hdr = part("lcsc", "HDR-TH_4P-P2.54-V-M", "J3", "HDR4", (31.0, 20.0 + OLED_DY), 90, {1: "GND", 2: "+3V3", 3: "SCL", 4: "SDA"}, anchor="1")
oled3d = pcbnew.FP_3DMODEL()            # for the render only: the OLED module on the header
oled3d.m_Filename = os.path.join(HERE, "lib", "lcsc.3dshapes", "OLED-TH_L38.0-W12.0_HS91L02W2C01.step")
oled3d.m_Offset, oled3d.m_Rotation = pcbnew.VECTOR3D(0.0, -17.5, 6.5), pcbnew.VECTOR3D(0, 0, 90)   # 2.5 mm header + 4 mm spacer
hdr.Models().push_back(oled3d)
g.rect(29.5, 10.19 + OLED_DY, 67.5, 22.19 + OLED_DY)
g.silk("OLED 0.91in (hand-solder)", (48.5, 22.19 + OLED_DY + 1.2), size=0.8)

# --- front panel: 11 LEDs (8 zones, 2 pumps, status), series 330 Ω; push button ----------------------------
LED_X = [76.0 + 6.5 * i for i in range(11)]
LED_Y = 20.0
leds = [(f"LEDZ{i + 1}", "LEDG") for i in range(8)] + [("LED_P1", "LEDY"), ("LED_P2", "LEDY"), ("LED_ST", "LEDR")]
for i, (src, key) in enumerate(leds):
    fp = g.place("LED_SMD", "LED_0805_2012Metric", f"D{20 + i}", (LED_X[i], LED_Y), rot=0, value=P[key][0], part=P[key])
    g.pin(fp, 1, "GND")
    g.pin(fp, 2, f"LEDA{i + 1}")
    two(f"R{50 + i}", "R330", (LED_X[i], 23.9), f"LEDA{i + 1}", src)
BTN_XY = (152.0, 20.0)
part("lcsc", "SW-TH_4P-L6.0-W6.0-P4.50-LS6.5", "SW1", "SW", BTN_XY, 0, {1: "BTN", 4: "GND"})
two("R19", "R10k", (146.0, 14.0), "+3V3", "BTN", rot=0)

# --- RTC: DS3231MZ + CR1220; battery reverse-polarity protection: one diode of a BAT54S in series ------------
part("Package_SO", "SOIC-8_3.9x4.9mm_P1.27mm", "U6", "RTC", (155.0, 8.0), 0,
     {2: "+3V3", 3: "RTC_INT", 5: "GND", 6: "VBAT", 7: "SDA", 8: "SCL"})
two("C45", "C100n", (161.0, 8.0), "+3V3", "GND")
two("R20", "R10k", (149.0, 8.0), "+3V3", "RTC_INT")
part("lcsc", "BAT-SMD_BS-12-B2AA002", "BT1", "BAT", (152.5, 32.5), 0, {1: "BATP", 2: "GND"})
part("Package_TO_SOT_SMD", "SOT-23", "D14", "BAT54S", (161.5, 14.5), 0, {1: "BATP", 3: "VBAT"})

g.silk("ESP Heating Control v4 · logic board · 2026", (100.0, 38.6), 0.9, layer=pcbnew.B_SilkS)

# --- front panel data for the enclosure (in enclosure coordinates) ----------------------------------------
front = {"origin": ORIGIN, "board": [W, H],
         # OLED module (38 × 12) on the J3 header; active area per datasheet 7.62–30.0 mm from the pin end, centred
         "oled": {"x0": 29.5 + ORIGIN[0], "x1": 67.5 + ORIGIN[0], "y0": 10.19 + OLED_DY + ORIGIN[1], "y1": 22.19 + OLED_DY + ORIGIN[1],
                  "aa_x": [29.5 + 7.62 + ORIGIN[0], 29.5 + 30.0 + ORIGIN[0]],
                  "pin_x": 31.0 + ORIGIN[0], "pins_y": [20.0 + OLED_DY - 2.54 * i + ORIGIN[1] for i in range(4)]},
         "leds": [[x_ + ORIGIN[0], LED_Y + ORIGIN[1]] for x_ in LED_X],
         "button": [BTN_XY[0] + ORIGIN[0], BTN_XY[1] + ORIGIN[1]],
         "usb": [33.0 + ORIGIN[0], 36.0 + ORIGIN[1]],
         "holes": [[x_ + ORIGIN[0], y_ + ORIGIN[1]] for x_, y_ in HOLES]}
json.dump(front, open(os.path.join(OUTDIR, "front.json"), "w"), indent=1)

g.rules([("PWR", 0.4, 0.15, 0.6, 0.3, ["+5V", "+24V", "VBUS"])],
        dru='(version 1)\n(rule "usbc_fine_pitch"\n  (condition "A.memberOfFootprint(\'J2\') && B.memberOfFootprint(\'J2\')")\n'
            '  (constraint clearance (min 0.1mm)))\n')
g.save()
print("placed:", g.out, "| footprints:", len(g.board.GetFootprints()))

if len(sys.argv) > 1:
    if os.environ.get("RESUME"):
        g.resume()
    else:
        g.fanout_gnd()
        g.freeroute(sys.argv[1], minutes=int(os.environ.get("FR_MIN", "15")))
    g.pour_gnd([[(0.5, 0.5), (W - 0.5, 0.5), (W - 0.5, H - 0.5), (0.5, H - 0.5)]])
    g.via_grid((2.5, 2.5, W - 2.5, H - 2.5), step=5.0, keep_out=[(0, 7, 8.5, 33), (NOTCH[0] - 1, 0, NOTCH[2] + 1, NOTCH[3] + 1)])
    g.finish()
    g.finish(include_gnd=True)
    g.dedupe()
    g.fill()
    g.save()
    g.cleanup()
    print("routed:", g.out)

g.export_jlc(OUTDIR)
