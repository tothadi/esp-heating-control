"""ESP Heating Control v4 – OpenTherm master interface schematic (SVG generator).

Usage:  python3 hardware/schematics/opentherm_master.py  ->  opentherm_master.svg

Operation (the master supplies the line voltage, the boiler answers by modulating its current draw):
  * 24 V: MT3608 boost converter from 5 V (390 kΩ / 10 kΩ feedback: 0.6 V × 40 = 24 V).
  * High level: Q1 (BCX56, SOT-89) emitter follower, its base held at 19.5 V by the TL431 (2.495 V × (1 + 68k/10k)).
  * Low level: the ESP OT_TX signal, via Q3, pulls the base down to the 4.3 V Zener (D21): the line is about 2 V.
  * Current limit: R55 15 Ω + Q2 (about 43 mA), short-circuit protection.
  * Receive: R58 47 Ω sense resistor in the return path; above ~12.8 mA Q4 switches (OT_RX low).
  * Polarity-independent: 2 × BAT54S bridge; TVS (SMAJ24A) after the screw terminal.
Levels seen from the boiler (Schottky ~0.45 V, including the drop across the sense resistor):
  high: at 7 mA 19.5 - 0.65 - 0.105 - 0.33 - 0.85 ≈ 17.5 V; at 23 mA 19.5 - 0.65 - 0.35 - 1.08 - 0.95 ≈ 16.4 V (spec: 15-18 V)
  low: 4.3 + 0.1 - 0.65 - 0.85 ≈ 2.9 V (spec: < 7 V)
"""
import os

HERE = os.path.dirname(os.path.abspath(__file__))
W, H = 1500, 900
out = []


def a(s):
    out.append(s)


def text(x, y, s, size=13, anchor="start", weight="normal", color="#222"):
    a(f'<text x="{x}" y="{y}" font-family="Helvetica,Arial,sans-serif" font-size="{size}" text-anchor="{anchor}" font-weight="{weight}" fill="{color}">{s}</text>')


def wire(*pts, color="#222", w=2):
    a(f'<polyline points="{" ".join(f"{x},{y}" for x, y in pts)}" fill="none" stroke="{color}" stroke-width="{w}"/>')


def dot(x, y):
    a(f'<circle cx="{x}" cy="{y}" r="4" fill="#222"/>')


def box(x, y, w, h, title, sub="", fill="#f4f6f8"):
    a(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="6" fill="{fill}" stroke="#555" stroke-width="1.5"/>')
    text(x + w / 2, y + h / 2 - (4 if sub else -5), title, 14, "middle", "bold")
    if sub:
        text(x + w / 2, y + h / 2 + 14, sub, 12, "middle")


def res(x, y, label, vertical=True):
    if vertical:
        a(f'<rect x="{x - 7}" y="{y}" width="14" height="40" fill="white" stroke="#222" stroke-width="2"/>')
        text(x + 14, y + 25, label, 12)
    else:
        a(f'<rect x="{x}" y="{y - 7}" width="40" height="14" fill="white" stroke="#222" stroke-width="2"/>')
        text(x + 20, y - 12, label, 12, "middle")


def gnd(x, y):
    wire((x, y), (x, y + 10))
    wire((x - 10, y + 10), (x + 10, y + 10))
    wire((x - 6, y + 14), (x + 6, y + 14))
    wire((x - 2, y + 18), (x + 2, y + 18))


def npn(x, y, label, lx=None, ly=None):
    """base to the left (x-20), collector on top (x, y-25), emitter at the bottom (x, y+25)"""
    a(f'<circle cx="{x}" cy="{y}" r="18" fill="white" stroke="#222" stroke-width="2"/>')
    wire((x - 20, y), (x - 6, y))
    wire((x - 6, y - 10), (x - 6, y + 10), w=3)
    wire((x - 6, y - 4), (x, y - 25))
    wire((x - 6, y + 4), (x, y + 25))
    a(f'<polygon points="{x - 2},{y + 18} {x},{y + 25} {x - 7},{y + 22}" fill="#222"/>')
    text(x + 22 if lx is None else lx, y + 5 if ly is None else ly, label, 12)


def zener(x, y, label):
    """vertical, cathode on top"""
    a(f'<polygon points="{x - 9},{y + 18} {x + 9},{y + 18} {x},{y + 4}" fill="white" stroke="#222" stroke-width="2"/>')
    wire((x - 9, y + 4), (x + 9, y + 4))
    wire((x - 9, y + 4), (x - 12, y + 1))
    wire((x + 9, y + 4), (x + 12, y + 7))
    text(x + 16, y + 16, label, 12)


a(f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}">')
a(f'<rect width="{W}" height="{H}" fill="white"/>')
text(30, 40, "ESP Heating Control v4 – OpenTherm master interface (power/IO board)", 22, weight="bold")
text(30, 64, "The master supplies the line voltage (high 15–18 V, low &lt; 7 V); the boiler answers with its current draw (5–7 / 17–23 mA).", 14, color="#555")

# --- 24 V boost converter --------------------------------------------------------------------
box(40, 110, 240, 110, "MT3608 (U20)", "5 V → 24 V, L 22 µH, SS34")
text(50, 245, "feedback 390 kΩ / 10 kΩ → 24.0 V", 12)
text(50, 262, "Cin 10 µF / 25 V, Cout 10 µF / 50 V (1206)", 12)
wire((280, 140), (360, 140))
text(300, 132, "+24V", 13, weight="bold", color="#c62828")
wire((360, 140), (1080, 140), color="#c62828")

# --- base reference: R52 + TL431 -------------------------------------------------------------
dot(470, 140)
wire((470, 140), (470, 175))
res(470, 175, "R52 2.2 kΩ (1206)")
wire((470, 215), (470, 300))
dot(470, 300)
text(380, 296, "base", 12, color="#555")
wire((470, 300), (470, 330))
box(420, 330, 100, 56, "TL431 (U21)", "19.5 V")
text(395, 410, "R53 68 kΩ / R54 10 kΩ (1%)", 12)
wire((470, 386), (470, 420))
gnd(470, 420)

# --- TX: Q3 + D21 (4.3 V) pull the base down --------------------------------------------------
wire((470, 300), (360, 300), (360, 470))
zener(360, 470, "D21 4.3 V")
wire((360, 492), (360, 535))
npn(360, 560, "Q3 MMBT3904")
wire((360, 585), (360, 610))
gnd(360, 610)
wire((340, 560), (300, 560))
res(260, 560, "R56 1 kΩ", vertical=False)
wire((260, 560), (200, 560))
text(110, 565, "ESP OT_TX", 14, weight="bold", color="#1565c0")
text(110, 585, "(high = line low)", 11, color="#555")

# --- Q1 emitter follower + current limit ---------------------------------------------------------
wire((470, 300), (640, 300))
npn(660, 300, "Q1 BCX56 (SOT-89)", lx=600, ly=262)
wire((660, 275), (660, 140))
dot(660, 140)
wire((660, 325), (660, 360))
dot(660, 360)
res(660, 360, "R55 15 Ω")
wire((660, 400), (660, 440))
dot(660, 440)
npn(760, 400, "Q2 MMBT3904 – current limit ~43 mA", lx=690, ly=470)
wire((660, 360), (720, 360), (720, 400), (740, 400))
wire((760, 425), (760, 440), (660, 440))
wire((760, 375), (760, 300), (680, 300))
dot(640, 300)
text(800, 432, "line +", 12, color="#555")

# --- bridge + TVS + screw terminal ------------------------------------------------------------------
wire((660, 440), (900, 440))
box(900, 400, 170, 140, "Bridge 2 × BAT54S", "polarity-independent")
wire((1070, 430), (1200, 430))
wire((1070, 510), (1200, 510))
dot(1150, 430)
dot(1150, 510)
wire((1150, 430), (1150, 450))
a('<rect x="1140" y="450" width="20" height="40" fill="white" stroke="#222" stroke-width="2"/>')
wire((1150, 490), (1150, 510))
text(1135, 475, "TVS SMAJ24A", 12, "end")
a('<rect x="1200" y="410" width="90" height="120" rx="4" fill="#e8f5e9" stroke="#2e7d32" stroke-width="2"/>')
text(1245, 440, "OT A", 14, "middle", "bold")
text(1245, 515, "OT B", 14, "middle", "bold")
text(1245, 555, "→ boiler", 13, "middle", color="#2e7d32")
text(1245, 575, "(screw terminal J9, OT A/B)", 11, "middle", color="#555")

# --- receive: R58 47 Ω + Q4 -------------------------------------------------------------------------
wire((900, 510), (840, 510), (840, 560))
dot(840, 560)
res(840, 560, "R58 47 Ω (sense)")
wire((840, 600), (840, 640))
gnd(840, 640)
wire((840, 560), (960, 560), (960, 640))
res(960, 640, "R59 1 kΩ")
wire((960, 680), (960, 710), (1000, 710))
npn(1020, 710, "Q4 MMBT3904 (~12.8 mA)")
wire((1020, 735), (1020, 760))
gnd(1020, 760)
wire((1020, 685), (1020, 640))
dot(1020, 640)
wire((1020, 640), (1020, 600))
res(1020, 560, "R60 4.7 kΩ → 3V3")
wire((1020, 640), (1150, 640))
text(1160, 645, "ESP OT_RX", 14, weight="bold", color="#1565c0")
text(1160, 665, "(low = boiler 17–23 mA)", 11, color="#555")

# --- levels -------------------------------------------------------------------------------------------
a('<rect x="40" y="690" width="560" height="180" rx="6" fill="#fffde7" stroke="#f9a825" stroke-width="1.5"/>')
text(55, 715, "Calculated levels seen from the boiler", 15, weight="bold")
text(55, 740, "High (TX = 0): ≈ 17.5 V at 7 mA, ≈ 16.4 V at 23 mA   (spec: 15–18 V)", 13)
text(55, 762, "Low (TX = 1): ≈ 2.9 V   (spec: &lt; 7 V)", 13)
text(55, 784, "Receive threshold: 0.6 V / 47 Ω ≈ 12.8 mA   (boiler: 5–7 / 17–23 mA)", 13)
text(55, 806, "Short circuit: R55 15 Ω + Q2 → ≈ 43 mA; Q1 SOT-89 (1 W)", 13)
text(55, 828, "R52 at low level: (24 − 4.4) V / 2.2 kΩ ≈ 9 mA → 0.17 W, hence 1206", 13)
text(55, 850, "Firmware: OT_TX/OT_RX polarity can be set in ESPHome with the `inverted:` option.", 13, color="#555")
a('</svg>')
open(os.path.join(HERE, "opentherm_master.svg"), "w").write("\n".join(out))
print("done:", os.path.join(HERE, "opentherm_master.svg"))
