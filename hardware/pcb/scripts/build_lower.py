"""ESP Heating Control – power/IO board (lower board): 10 relays, HLK-5M05, 230 V and SELV screw terminals.

Usage:   <KiCad python> hardware/pcb/scripts/build_lower.py [freerouting-2.x.jar]
Output:  hardware/pcb/lower/lower.kicad_pcb (+ fab/: gerber/, lower_gerber.zip, jlcpcb_bom.csv, jlcpcb_cpl.csv)

Layout (the board is 170 × 85 mm, inset 2.5 mm inside the enclosure; see docs/):
  top     – SELV: 8 × 4-pole + 1 × 2-pole 3.5 mm terminals (NTC1–8 + GND, TIN1–8 + GND, OpenTherm A/B),
            connector to the logic board, MCP23017 (0x20) + 2 × ULN2003 above the relay coils;
  middle  – isolation band (≥ 6.5 mm, copper-free): relay coils above it, contacts below it;
  bottom  – 230 V: 8 + 8 + 4 + 2-pole 5.08 mm terminals (Z1–Z4, Z5–Z8, P1–P2, L/N supply), relays, HLK-5M05,
            T2A fuse, varistor. Each relay sits above its own L pole; the output runs straight down.
The terminal wire entries face outwards (both rows at 0°: the XY350V footprints have the wire entry on -y, the DB128V
footprints on +y); the different pitch means a 230 V plug cannot be inserted into a SELV terminal.
"""
import os
import sys

import pcbnew

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pcbgen import MM, Gen  # noqa: E402

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))   # hardware/pcb/ (the scripts live in hardware/pcb/scripts/)
OUTDIR = os.path.join(HERE, "lower")
os.makedirs(OUTDIR, exist_ok=True)
W, H = 170.0, 85.0
g = Gen(os.path.join(OUTDIR, "lower.kicad_pcb"), W, H)

BAND_Y0, BAND_Y1 = 48.5, 55.0          # isolation band: SELV y < 48.5; 230 V y > 55
RELAY_Y = 46.0                          # relay coil pins (A1, A2); 13: +11.5; 14: +18.5
Y_TOP, Y_BOT = 3.6, 79.4                # pin row of the top / bottom screw terminals

P = {  # value, footprint, LCSC
    # screw terminals (fixed, not pluggable); wire entry: XY350V on the footprint's -y side (at 0°: towards the top board
    # edge), DB128V on the +y side (at 0°: towards the bottom board edge) – checked against the LCSC 3D models
    "TB35_4": ("XY350V-3.5-4P screw terminal", "XY350V-3.5-4P", "C557656"),
    "TB35_2": ("XY350V-3.5-2P screw terminal", "XY350V-3.5-2P", "C784942"),
    "TB508_8": ("DB128V-5.08-8P-GN-S screw terminal", "DB128V-5.08-8P", "C5143711"),
    "TB508_4": ("DB128V-5.08-4P-GN-S screw terminal", "DB128V-5.08-4P", "C2915641"),
    "TB508_2": ("DB128V-5.08-2P-GN-S screw terminal", "DB128V-5.08-2P", "C2915639"),
    "IDC": ("IDC 2x13 box header", "IDC-26P-P2.54", "C9040"),
    "RELAY": ("G5NB-1A-E-DC5V", "Relay_SPST_Omron_G5NB", "C48746"),
    "HLK": ("HLK-5M05", "Converter_ACDC_Hi-Link_HLK-5Mxx", "C209907"),
    "FUSE": ("T2A 250V TR5 (2010T2A250V)", "Fuse_Littelfuse_372_D8.50mm", "C354902"),
    "MOV": ("07D471K", "RES-TH_L9.0-W6.2-P5.00-D0.7-S6.20", "C28756"),
    "MCP": ("MCP23017-E/SS", "SSOP-28_5.3x10.2mm_P0.65mm", "C506653"),
    "ULN": ("ULN2003A", "SOIC-16_3.9x9.9mm_P1.27mm", "C688859"),
    "C100n": ("100nF 50V X7R", "C_0805_2012Metric", "C49678"),
    "C10u": ("10uF 25V X5R", "C_0805_2012Metric", "C15850"),
    "C220u": ("220uF 16V 6.3x7.7 (RVT1C221M0607)", "CP_Elec_6.3x7.7", "C970695"),
}

# --- mounting holes (for the enclosure bosses, M3) -------------------------------------------------------
# H1–H4: JLC3DP threaded insert in the cover boss (Ø9.8 mm boss – no parts around it); that is why H3 sits higher (between
# J21 and K1) and the top terminal row starts at x = 9.0 (the H1 boss reaches x = 8.4).
for k, xy in enumerate(((3.5, 3.5), (W - 3.5, 3.5), (3.5, 69.0), (W - 3.5, H - 3.5)), 1):
    g.hole(f"H{k}", xy)

# --- top screw terminals: SELV, 3.5 mm (wire entry facing outwards, 0°) ----------------------------------
TOP = [("NTC 1-2", ["NTC1", "GND", "NTC2", "GND"]), ("NTC 3-4", ["NTC3", "GND", "NTC4", "GND"]),
       ("NTC 5-6", ["NTC5", "GND", "NTC6", "GND"]), ("NTC 7-8", ["NTC7", "GND", "NTC8", "GND"]),
       ("TERM 1-2", ["TIN1", "GND", "TIN2", "GND"]), ("TERM 3-4", ["TIN3", "GND", "TIN4", "GND"]),
       ("TERM 5-6", ["TIN5", "GND", "TIN6", "GND"]), ("TERM 7-8", ["TIN7", "GND", "TIN8", "GND"]),
       ("OT", ["OT_A", "OT_B"])]
x = 9.0                                 # left edge of the first top terminal (clear of the H1 insert boss)
for k, (lab, pins) in enumerate(TOP, 1):
    n = len(pins)
    key = "TB35_4" if n == 4 else "TB35_2"
    name = "CONN-TH_XY350V-3.5-4P" if n == 4 else "CONN-TH_XY350V-3.5-2P"
    half = (n - 1) * 3.5 / 2 + 2.12
    cx = x + half
    fp = g.place("lcsc", name, f"J{k}", (cx, Y_TOP), rot=0, value=P[key][0], part=P[key])
    for i, nn in enumerate(pins):
        g.pad_near(fp, (cx - (n - 1) * 1.75 + 3.5 * i, Y_TOP), nn)
    g.silk(lab, (cx, 10.4), 0.9, bold=True)
    x = cx + half + 1.0                 # 1.0 mm between blocks: the RJ45 fits between J9 and the H2 insert boss

# --- bottom screw terminals: 230 V, 5.08 mm (wire entry facing outwards, 0°) ----------------------------
BOT = [("Z1 Z2 Z3 Z4", 8, 28.48, [f"{p}{z}" for z in (1, 2, 3, 4) for p in ("LOUT", "N")]),
       ("Z5 Z6 Z7 Z8", 8, 70.94, [f"{p}{z}" for z in (5, 6, 7, 8) for p in ("LOUT", "N")]),
       ("P1 P2", 4, 103.24, [f"{p}{z}" for z in (9, 10) for p in ("LOUT", "N")]),
       ("230V L N", 2, 150.0, ["L_IN", "N"])]
LPAD = {}
for k, (lab, n, cx, pins) in enumerate(BOT, 21):
    key = f"TB508_{n}"
    name = {8: "CONN-TH_8P-P5.08_DB128V-5.08-8P-GN-S", 4: "CONN-TH_4P-P5.08_DIBO_DB128V-5.08", 2: "CONN-TH_2P-P5.08_DIBO_DB128V-5.08"}[n]
    fp = g.place("lcsc", name, f"J{k}", (cx, Y_BOT), rot=0, value=P[key][0], part=P[key])
    for i, nn in enumerate(pins):
        px = cx - (n - 1) * 2.54 + 5.08 * i
        nn = "N_AC" if nn.startswith("N") else nn
        g.pad_near(fp, (px, Y_BOT), nn)
        if nn.startswith("LOUT"):
            LPAD[int(nn[4:])] = px
    g.silk(lab, (cx, 70.6), 0.9, bold=True)
NPAD = {k: x + 5.08 for k, x in LPAD.items()}
L_IN_X, N_IN_X = 150.0 - 2.54, 150.0 + 2.54

# --- 10 relays: coil on top (SELV), contact below (230 V), each contact above its own L pole ------------
RELAY_X = {k: LPAD[k] + 4.7 for k in range(1, 11)}
for k in range(1, 11):
    fp = g.place("Relay_THT", "Relay_SPST_Omron_G5NB", f"K{k}", (RELAY_X[k], RELAY_Y), rot=-90,
                 value=P["RELAY"][0], anchor="A1", part=P["RELAY"])
    g.pin(fp, "A1", "+5V")
    g.pin(fp, "A2", f"COIL{k}")
    g.pin(fp, "13", "L_F")
    g.pin(fp, "14", f"LOUT{k}")
    g.silk(f"K{k}" if k <= 8 else f"P{k - 8}", (RELAY_X[k] - 2.35, RELAY_Y + 5.5), 0.9, bold=True)

# --- HLK-5M05 vertical, spanning the band: AC below (230 V), DC above (SELV) ----------------------------
HLK_AC1 = (124.0, 62.0)            # 90°: AC1 (124, 62), AC2 (130, 62), -Vo (118, 28.4), +Vo (136, 28.4)
fp = g.place("Converter_ACDC", "Converter_ACDC_Hi-Link_HLK-5Mxx", "PS1", HLK_AC1, rot=90, value="HLK-5M05", anchor="1", part=P["HLK"])
g.pin(fp, 1, "L_F")
g.pin(fp, 2, "N_AC")
g.pin(fp, 3, "GND")
g.pin(fp, 4, "+5V")
g.silk("HLK-5M05", (127.0, 44.0), 1.0, bold=True, rot=90)
FUSE1, FUSE2 = (L_IN_X, 61.0), (L_IN_X + 5.08, 61.0)
fp = g.place("Fuse", "Fuse_Littelfuse_372_D8.50mm", "F1", FUSE1, value="T2A", anchor="1", part=P["FUSE"])
g.pin(fp, 1, "L_IN")
g.pin(fp, 2, "L_F")
# the C28756 leads are splayed (5.0 mm along, 6.2 mm across) -> its LCSC footprint, pins diagonal; pin 2 below pin 1
VAR1, VAR2 = (157.5, 56.8), (162.5, 63.0)
fp = g.place("lcsc", "RES-TH_L9.0-W6.2-P5.00-D0.7-S6.20", "RV1", VAR1, value="07D471K", anchor="1", part=P["MOV"])
g.pin(fp, 1, "L_F")
g.pin(fp, 2, "N_AC")
# (below the RJ45, in the corner freed by the OpenTherm boost converter: the space above the HLK is the Ethernet block)
for ref, xy, key, rot in (("C10", (157.8, 22.3), "C10u", 0), ("C11", (154.2, 22.3), "C100n", 0), ("C12", (144.5, 26.0), "C220u", 90)):   # C10/C11 clear of the RJ45 body (it reaches y 21.2, beyond its courtyard)
    lib, name = ("Capacitor_SMD", P[key][1])
    fp = g.place(lib, name, ref, xy, rot=rot, value=P[key][0], part=P[key])
    g.pin(fp, 1, "+5V")
    g.pin(fp, 2, "GND")

# --- SELV section: connector to the logic board, MCP23017, 2 × ULN2003 ---------------------------------
# FFC: 1-2 +5V, 3-4 GND, 5 SDA, 6 SCL, 7 OT_TX, 8 OT_RX, 9-16 zone LED 1..8 (MCP #2 GPB), 17 MCP_INT,
#      18 ETH_SCK, 19 GND, 20 ETH_MOSI, 21 ETH_MISO, 22 ETH_CS, 23 ETH_INT, 24 GND (W5500 SPI), 25 +3V3 (from the logic board LDO), 26 GND
IDC = {1: "+5V", 2: "+5V", 3: "GND", 4: "GND", 5: "SDA", 6: "SCL", 7: "OT_TX", 8: "OT_RX", 17: "MCP_INT", 25: "+3V3", 26: "GND"}
IDC.update({9 + i: f"LEDZ{i + 1}" for i in range(8)})
IDC.update({18: "ETH_SCK", 19: "GND", 20: "ETH_MOSI", 21: "ETH_MISO", 22: "ETH_CS", 23: "ETH_INT", 24: "GND"})
# FFC 26P 1.0 mm (JUSHUO AFA07-S26FCA-00, bottom contact, 2.5 mm high); the cable leaves in +y (towards the relays)
# loops back over the connector and rises through the logic board notch to the connector on the logic board's top side
# (type A cable: contacts on the same side at both ends; both connectors sit on the top side of their board and the
# ribbon never turns over, see docs/hardware.md)
fp = g.place("Connector_FFC-FPC", "JUSHUO_AFA07-S26FCA-00_1x26-1MP_P1.0mm_Horizontal", "J30", (60.0, 26.0), value="AFA07-S26FCA-00",
             part=("AFA07-S26FCA-00 FFC 26P 1.0mm", "JUSHUO_AFA07-S26FCA-00", "C2681942"))
for p_, nn in IDC.items():
    g.pin(fp, p_, nn)
for pd in fp.Pads():
    if pd.GetNumber() == "MP":
        pd.SetNet(g.net("GND"))
g.silk("FFC -> LOGIC (type A)", (60.0, 33.4), 0.9)

# MCP23017 (0x20): GPA0..7 -> relay 1..8, GPB0..1 -> relay 9..10; at power-up every pin is an input (high impedance),
# so the ULN input is held low by its internal pull-down: no relay energises at power-up / ESP reboot.
mcp = {9: "+3V3", 10: "GND", 12: "SCL", 13: "SDA", 15: "GND", 16: "GND", 17: "GND", 18: "+3V3", 1: "RIN9", 2: "RIN10"}
mcp.update({21 + i: f"RIN{i + 1}" for i in range(8)})
fp = g.place("Package_SO", P["MCP"][1], "U1", (60.0, 37.5), rot=-90, value=P["MCP"][0], part=P["MCP"])
for p_, nn in mcp.items():
    g.pin(fp, p_, nn)
fp = g.place("Capacitor_SMD", "C_0805_2012Metric", "C1", (67.5, 37.5), rot=90, value=P["C100n"][0], part=P["C100n"])
g.pin(fp, 1, "+3V3")
g.pin(fp, 2, "GND")
# ULN2003 (-90°: inputs on top, outputs at the bottom towards the relays). Channels arranged so K1..K10 run left to right.
for ref, x_, relays, first in (("U2", 28.0, range(1, 8), 7), ("U3", 94.0, range(8, 11), 3)):
    pins = {8: "GND", 9: "+5V"}
    for n_, k in enumerate(relays):
        c = first - n_
        pins[c] = f"RIN{k}"
        pins[17 - c] = f"COIL{k}"
    for c in range(1, 8):
        pins.setdefault(c, "GND")
    fp = g.place("Package_SO", P["ULN"][1], ref, (x_, 39.5), rot=-90, value=P["ULN"][0], part=P["ULN"])
    for p_, nn in pins.items():
        g.pin(fp, p_, nn)

P.update({
    "ADS": ("ADS1115IDGSR", "MSOP-10", "C37593"), "R10k01": ("10k 0.1%", "R_0805", "C110775"),
    "SRV": ("SRV05-4", "SOT-23-6", "C13612"), "R1k": ("1k 1%", "R_0805", "C17513"),
    "MT3608": ("MT3608", "SOT-23-6", "C84817"), "L22u": ("22uH SWPA4030S220MT", "4x4", "C83472"),
    "SS34": ("SS34", "D_SMA", "C8678"), "Q1": ("BCX56-16,115 (Nexperia)", "SOT-89", "C100026"),
    "NPN": ("MMBT3904", "SOT-23", "C20526"), "TL431": ("CJ431", "SOT-23", "C3113"), "BAT54S": ("BAT54S", "SOT-23", "C7420333"),
    "Z4V3": ("BZT52C4V3", "SOD-123", "C19077399"), "TVS": ("SMAJ24A", "D_SMA", "C19077541"),
    "R15": ("15R 1%", "R_0805", "C17480"), "R47": ("47R 1%", "R_0805", "C17714"), "R4k7": ("4.7k 1%", "R_0805", "C17673"),
    "R10k": ("10k 1%", "R_0805", "C17414"), "R68k": ("68k 1%", "R_0805", "C17801"), "R390k": ("390k 1%", "R_0805", "C17656"),
    "R2k2_1206": ("2.2k 1% 1206", "R_1206", "C17948"), "C10u50": ("10uF 50V X5R 1206", "C_1206", "C13585"),
})
R0805, C0805 = ("Resistor_SMD", "R_0805_2012Metric"), ("Capacitor_SMD", "C_0805_2012Metric")


def two(ref, key, xy, n1, n2, rot=90, fpkind=None):
    lib, name = fpkind or (R0805 if key.startswith("R") else C0805)
    fp = g.place(lib, name, ref, xy, rot=rot, value=P[key][0], part=P[key])
    g.pin(fp, 1, n1)
    g.pin(fp, 2, n2)
    return fp


def part(lib, name, ref, key, xy, rot, pins):
    fp = g.place(lib, name, ref, xy, rot=rot, value=P[key][0], part=P[key])
    for p_, nn in pins.items():
        g.pin(fp, p_, nn)
    return fp


# --- NTC measurement: 2 × ADS1115 + dividers (10 kΩ 0.1% + 10 µF) directly below the NTC terminals --------------
# 10 µF (2026-11-08, previously 100 nF): τ ≈ 5 kΩ × 10 µF = 50 ms, 50 Hz noise attenuated by about −24 dB (filtering proven on v2)
# Channels match v2: 0x48 AIN3..0 = NTC1..4 (zones 1–4), 0x49 AIN3..0 = NTC5..8.
for k in range(1, 9):
    x_ = 8.0 + 4.2 * (k - 1) + (10.0 if k > 4 else 0.0)
    two(f"R{20 + k}", "R10k01", (x_, 14.0), "+3V3", f"NTC{k}")
    two(f"C{20 + k}", "C10u", (x_, 18.4), f"NTC{k}", "GND")
for ref, x_, addr, ain in (("U10", 26.6, "GND", ["NTC4", "NTC3", "NTC2", "NTC1"]), ("U11", 53.6, "+3V3", ["NTC8", "NTC7", "NTC6", "NTC5"])):
    part("Package_SO", "MSOP-10_3x3mm_P0.5mm", ref, "ADS", (x_, 16.2), 0,
         {1: addr, 3: "GND", 4: ain[0], 5: ain[1], 6: ain[2], 7: ain[3], 8: "+3V3", 9: "SDA", 10: "SCL"})
    two(f"C{19 if ref == 'U10' else 29}", "C100n", (x_ + 4.6, 16.2), "+3V3", "GND")

# --- thermostat inputs (dry contact): ESD, 1 kΩ pull-up, series 1 kΩ + 100 nF; MCP23017 #2 (0x21) -------------
for i in range(8):
    x_ = 89.0 + 3.2 * i
    two(f"R{30 + i}", "R1k", (x_, 13.0), "+3V3", f"TIN{i + 1}")
    two(f"R{40 + i}", "R1k", (x_, 16.4), f"TIN{i + 1}", f"TINF{i + 1}")
    two(f"C{30 + i}", "C100n", (x_, 19.8), f"TINF{i + 1}", "GND")
part("Package_TO_SOT_SMD", "SOT-23-6", "U12", "SRV", (93.0, 23.0), 0, {1: "TIN1", 3: "TIN2", 4: "TIN3", 6: "TIN4", 2: "GND", 5: "+5V"})
part("Package_TO_SOT_SMD", "SOT-23-6", "U13", "SRV", (104.0, 23.0), 0, {1: "TIN5", 3: "TIN6", 4: "TIN7", 6: "TIN8", 2: "GND", 5: "+5V"})
mcp2 = {9: "+3V3", 10: "GND", 12: "SCL", 13: "SDA", 15: "+3V3", 16: "GND", 17: "GND", 18: "+3V3", 20: "MCP_INT"}
mcp2.update({21 + i: f"TINF{8 - i}" for i in range(8)})       # GPA0 = TERM8 … GPA7 = TERM1 (avoids crossings)
mcp2.update({1 + i: f"LEDZ{i + 1}" for i in range(8)})
part("Package_SO", "SSOP-28_5.3x10.2mm_P0.65mm", "U14", "MCP", (102.0, 30.3), 90, mcp2)
two("C4", "C100n", (109.2, 30.3), "+3V3", "GND")

# --- OpenTherm master (see schematics/opentherm_master.svg), next to the HLK, below the OT terminal --------------
# 24 V boost converter: in the free area above the relays (its old place at the top right is now the RJ45)
part("Package_TO_SOT_SMD", "SOT-23-6", "U20", "MT3608", (71.0, 38.0), 0, {1: "SW_BOOST", 2: "GND", 3: "FB", 4: "+5V", 5: "+5V"})
fp = g.place("lcsc", "IND-SMD_L4.0-W4.0_SLW4010S", "L1", (76.5, 38.0), value=P["L22u"][0], part=P["L22u"])
g.pin(fp, 1, "+5V")
g.pin(fp, 2, "SW_BOOST")
part("Diode_SMD", "D_SMA", "D20", "SS34", (83.5, 38.0), 0, {1: "+24V", 2: "SW_BOOST"})
two("C40", "C10u", (71.0, 42.2), "+5V", "GND", rot=0)
two("C41", "C10u50", (83.5, 42.2), "+24V", "GND", rot=0, fpkind=("Capacitor_SMD", "C_1206_3216Metric"))
two("R50", "R390k", (74.0, 34.0), "+24V", "FB", rot=0)          # feedback divider next to the MT3608
two("R51", "R10k", (77.4, 34.0), "FB", "GND", rot=0)
two("R52", "R2k2_1206", (150.0, 25.0), "+24V", "OT_BASE", fpkind=("Resistor_SMD", "R_1206_3216Metric"))
part("Package_TO_SOT_SMD", "SOT-23", "U21", "TL431", (154.5, 25.0), 0, {1: "OT_REF", 2: "OT_BASE", 3: "GND"})
two("R53", "R68k", (158.5, 25.0), "OT_BASE", "OT_REF")
two("R54", "R10k", (161.5, 25.0), "OT_REF", "GND")
part("Package_TO_SOT_SMD", "SOT-89-3", "Q1", "Q1", (165.8, 25.5), 0, {1: "OT_E", 2: "+24V", 3: "OT_BASE"})     # Nexperia: E-C-B
two("R55", "R15", (143.0, 33.0), "OT_E", "OT_LINE")
part("Package_TO_SOT_SMD", "SOT-23", "Q2", "NPN", (147.0, 33.0), 0, {1: "OT_E", 2: "OT_LINE", 3: "OT_BASE"})         # B-E-C
part("Diode_SMD", "D_SOD-123", "D21", "Z4V3", (151.0, 33.0), 90, {1: "OT_BASE", 2: "OT_LOW"})
part("Package_TO_SOT_SMD", "SOT-23", "Q3", "NPN", (155.0, 33.0), 0, {1: "OT_TXB", 2: "GND", 3: "OT_LOW"})
two("R56", "R1k", (159.0, 33.0), "OT_TX", "OT_TXB")
two("R57", "R10k", (162.0, 33.0), "OT_TXB", "GND")
part("Package_TO_SOT_SMD", "SOT-23", "D22", "BAT54S", (166.0, 33.0), 0, {1: "OT_LINE", 2: "OT_RET", 3: "OT_A"})
part("Package_TO_SOT_SMD", "SOT-23", "D23", "BAT54S", (143.5, 41.0), 0, {1: "OT_LINE", 2: "OT_RET", 3: "OT_B"})
part("Diode_SMD", "D_SMA", "D24", "TVS", (149.5, 41.0), 0, {1: "OT_LINE", 2: "GND"})
two("R58", "R47", (155.0, 41.0), "OT_RET", "GND")
part("Package_TO_SOT_SMD", "SOT-23", "Q4", "NPN", (158.5, 41.0), 0, {1: "OT_RXB", 2: "GND", 3: "OT_RX"})
two("R59", "R1k", (162.0, 41.0), "OT_RET", "OT_RXB")
two("R60", "R4k7", (165.0, 41.0), "+3V3", "OT_RX")

# --- Ethernet: WIZnet W5500 (SPI to the ESP32-S3 through the FFC) + RJ45 magjack, SELV, top right ---------------------
# Circuit after the WIZnet W5500 datasheet v1.1.0 (EXRES1 12.4k 1 %, TOCAP 4.7 uF, 1V2O 10 nF, VBG floating, RSVD 23 to GND,
# 38-42 open, PMODE open = all capable with auto-negotiation, 25 MHz crystal with 18 pF / 1 M / 0 R) and the PHY side of the
# WIZnet W5500-EVB-Pico V1.0 schematic (same magjack J1B1211CCD): TX 3.3 R series + 49.9 R to 3.3 V on the jack side,
# TCT 10 R to 3.3 V + 22 nF; RX 6.8 nF series + 3.3 R, 2 x 49.9 R to a common node with 100 nF, RCT on that node;
# shield to chassis ground with 1 nF / 2 kV to GND; LED anodes on 3.3 V, cathodes through 470 R to LINKLED / ACTLED.
# Supply: +3V3 (from the logic board regulator, through the FFC) via a ferrite bead, as on the WIZnet board (~130 mA).
P.update({
    "W5500": ("W5500", "LQFP-48", "C32843"), "RJ45": ("J1B1211CCD RJ45 10/100 magjack", "RJ45-TH_J1B1211CCD", "C910371"),
    "X25M": ("25MHz 12pF X322525MOB4SI", "3225", "C9006"), "FB600": ("Ferrite 600R@100MHz 500mA", "0805", "C1017"),
    "R49R9": ("49.9R 1%", "R_0805", "C17720"), "R3R3": ("3.3R 1%", "R_0805", "C2907250"), "R10R": ("10R 1%", "R_0805", "C17415"),
    "R12k4": ("12.4k 1%", "R_0805", "C30908"), "R470": ("470R 1%", "R_0805", "C17710"), "R1M": ("1M 1%", "R_0805", "C17514"),
    "R0": ("0R", "R_0805", "C17477"), "C18p": ("18pF C0G 50V", "C_0805", "C1797"), "C6n8": ("6.8nF X7R 50V", "C_0805", "C1755"),
    "C22n": ("22nF X7R 50V", "C_0805", "C1729"), "C10n": ("10nF X7R 50V", "C_0805", "C1710"), "C4u7": ("4.7uF X5R 25V", "C_0805", "C1779"),
    "C1n2kV": ("1nF 2kV X7R 1206", "C_1206", "C9196"),
})
w5500 = {1: "ETH_TXN", 2: "ETH_TXP", 5: "ETH_RXN", 6: "ETH_RXP", 10: "ETH_EXRES", 20: "ETH_TOCAP", 22: "ETH_1V2O",
         25: "ETH_LINK", 27: "ETH_ACT", 30: "ETH_XI", 31: "ETH_XO", 32: "ETH_CS", 33: "ETH_SCK", 34: "ETH_MISO", 35: "ETH_MOSI",
         36: "ETH_INT", 37: "ETH_RST", 23: "GND"}
w5500.update({n: "+3V3E" for n in (4, 8, 11, 15, 17, 21, 28)})
w5500.update({n: "GND" for n in (3, 9, 14, 16, 19, 29, 48)})
part("lcsc", "LQFP-48_L7.0-W7.0-P0.50-LS9.0-BL", "U30", "W5500", (127.5, 16.0), 90, w5500)   # 90°: TX/RX (pins 1-12) towards the RJ45
# magjack: 180° = opening at the top board edge (front face flush with the edge), like the terminal wire entries
part("lcsc", "RJ45-TH_J1B1211CCD", "J40", "RJ45", (152.4, 10.58), 180,
     {1: "ETH_TDP", 2: "ETH_TCT", 3: "ETH_TDN", 4: "ETH_RDP", 5: "ETH_RXCM", 6: "ETH_RDN", 8: "GND",
      9: "ETH_GLEDK", 10: "+3V3E", 11: "ETH_YLEDK", 12: "+3V3E", 13: "CHGND", 14: "CHGND"})
# PHY side, between the W5500 and the jack (5 rows x 3 columns)
ETH = [("R70", "R3R3", "ETH_TXN", "ETH_TDN"), ("R71", "R3R3", "ETH_TXP", "ETH_TDP"), None,     # slot next to the jack shield pad left free
       ("R72", "R49R9", "ETH_TDN", "+3V3E"),
       ("R73", "R49R9", "ETH_TDP", "+3V3E"), ("R74", "R10R", "ETH_TCT", "+3V3E"), ("C70", "C22n", "ETH_TCT", "GND"),
       ("C71", "C6n8", "ETH_RDN", "ETH_RXN_M"), ("C72", "C6n8", "ETH_RDP", "ETH_RXP_M"), ("R75", "R3R3", "ETH_RXN_M", "ETH_RXN"),
       ("R76", "R3R3", "ETH_RXP_M", "ETH_RXP"), ("R77", "R49R9", "ETH_RXN_M", "ETH_RXCM"), ("R78", "R49R9", "ETH_RXP_M", "ETH_RXCM"),
       ("C73", "C100n", "ETH_RXCM", "GND"), ("R79", "R12k4", "ETH_EXRES", "GND")]
for i, e in enumerate(ETH):
    if e:
        two(e[0], e[1], (135.7 + 3.45 * (i % 3), 11.8 + 2.0 * (i // 3)), e[2], e[3], rot=0)
two("R80", "R470", (138.4, 8.6), "ETH_YLEDK", "ETH_LINK", rot=0)      # LED resistors, below J8 / J9
two("R81", "R470", (134.5, 8.6), "ETH_GLEDK", "ETH_ACT", rot=0)
two("C74", "C1n2kV", (164.5, 18.0), "CHGND", "GND", fpkind=("Capacitor_SMD", "C_1206_3216Metric"))
# crystal and reset (west of the W5500)
part("lcsc", "CRYSTAL-SMD_4P-L3.2-W2.5-BL", "Y30", "X25M", (120.0, 14.6), 0, {1: "ETH_XI", 2: "GND", 3: "ETH_XO2", 4: "GND"})
two("R82", "R1M", (120.0, 12.0), "ETH_XI", "ETH_XO", rot=0)
two("R83", "R0", (120.0, 17.2), "ETH_XO", "ETH_XO2", rot=0)
two("C75", "C18p", (116.4, 13.0), "ETH_XI", "GND", rot=0)
two("C76", "C18p", (116.4, 15.6), "ETH_XO2", "GND", rot=0)
two("R84", "R10k", (116.4, 9.6), "+3V3E", "ETH_RST", rot=0)          # RSTn: RC power-on reset (no reset line on the FFC)
two("C77", "C100n", (120.0, 9.6), "ETH_RST", "GND", rot=0)
# 3.3 V for the W5500: ferrite bead from +3V3
two("FB1", "FB600", (116.4, 18.2), "+3V3", "+3V3E", rot=0, fpkind=("Inductor_SMD", "L_0805_2012Metric"))
# W5500 decoupling, 1V2O and TOCAP (south of the chip)
for i, (ref, key, n1) in enumerate((("C80", "C100n", "+3V3E"), ("C81", "C100n", "+3V3E"), ("C82", "C100n", "+3V3E"),
                                     ("C83", "C10n", "ETH_1V2O"), ("C84", "C4u7", "ETH_TOCAP"), ("C85", "C10u", "+3V3E"))):
    two(ref, key, (121.5 + 3.4 * i, 22.4), n1, "GND", rot=0)

# --- 230 V tracks, manual (locked) ---------------------------------------------------------------
LBUS_Y = RELAY_Y + 10.8            # through relay pin 13 (RELAY_Y + 11.5 ± 0.9)
g.track("L_IN", [(L_IN_X, Y_BOT), FUSE1], 1.5)
g.track("L_F", [FUSE2, (FUSE2[0], LBUS_Y), (LPAD[1], LBUS_Y)], 2.0)
g.track("L_F", [(FUSE2[0], LBUS_Y), VAR1], 2.0)
g.track("L_F", [(HLK_AC1[0], LBUS_Y), HLK_AC1], 1.0)
NBUS_Y = 69.0
g.track("N_AC", [(NPAD[1], NBUS_Y), (VAR2[0], NBUS_Y), VAR2], 2.0, pcbnew.B_Cu)
g.track("N_AC", [(N_IN_X, Y_BOT), (N_IN_X, NBUS_Y)], 1.5, pcbnew.B_Cu)
g.track("N_AC", [(HLK_AC1[0] + 6.0, NBUS_Y), (HLK_AC1[0] + 6.0, HLK_AC1[1])], 1.0, pcbnew.B_Cu)
for k in range(1, 11):
    g.track("N_AC", [(NPAD[k], NBUS_Y), (NPAD[k], Y_BOT)], 1.5, pcbnew.B_Cu)
    g.track(f"LOUT{k}", [(LPAD[k], RELAY_Y + 18.5), (LPAD[k], Y_BOT)], 1.5)

# --- isolation band and marking ------------------------------------------------------------------
g.area([(0.3, BAND_Y0), (W - 0.3, BAND_Y0), (W - 0.3, BAND_Y1), (0.3, BAND_Y1)], "ISO")
for yy in (BAND_Y0 + 0.4, BAND_Y1 - 0.4):
    g.line((0.8, yy), (W - 0.8, yy), w=0.3)
g.silk("! 230 V ~", (150.0, (BAND_Y0 + BAND_Y1) / 2), 1.4, bold=True)
g.silk("ESP Heating Control · power/IO board · 2026", (60.0, (BAND_Y0 + BAND_Y1) / 2), 1.0, layer=pcbnew.B_SilkS)

# --- rules: 230 V class (2 mm between each other), 6 mm from SELV ---------------------------------
g.rules([("PWR", 0.5, 0.15, 0.6, 0.3, ["+5V"]),
         ("MAINS", 1.5, 2.0, 0.8, 0.4, ["L_IN", "L_F", "N_AC", "LOUT*"])],
        dru='(version 1)\n(rule "mains_to_selv"\n'
            "  (condition \"A.NetClass == 'MAINS' && B.NetClass != 'MAINS' && B.NetName != ''\")\n"
            '  (constraint clearance (min 6mm)))\n',
        min_track=0.12)   # Freerouting necks tracks (down to ~0.125 mm) between the W5500's 0.5 mm pitch leads
g.save()
print("placed:", g.out, "| footprints:", len(g.board.GetFootprints()))

if len(sys.argv) > 1:
    M_ = 0.3
    g.tmp_areas = [g.area([(0.3, BAND_Y1), (W - 0.3, BAND_Y1), (W - 0.3, H - 0.3), (0.3, H - 0.3)], "MAINS_TMP"),
                   g.area([(0.3, BAND_Y0 - M_), (W - 0.3, BAND_Y0 - M_), (W - 0.3, BAND_Y1), (0.3, BAND_Y1)], "ISO_TMP")]
    if os.environ.get("RESUME"):
        g.resume()
    else:
        g.fanout_gnd()
        g.freeroute(sys.argv[1], minutes=int(os.environ.get("FR_MIN", "10")))
    g.pour_gnd([[(0.5, 0.5), (W - 0.5, 0.5), (W - 0.5, BAND_Y0 - 0.5), (0.5, BAND_Y0 - 0.5)]])
    g.via_grid((2.5, 2.5, W - 2.5, BAND_Y0 - 1.5), step=5.0)
    g.finish()
    g.finish(include_gnd=True)
    g.dedupe()
    g.fill()
    g.save()
    g.cleanup()
    print("routed:", g.out)

g.export_jlc(os.path.join(OUTDIR, "fab"))
g.gerbers(os.path.join(OUTDIR, "fab"), "lower_gerber.zip")
