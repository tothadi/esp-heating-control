"""ESP Heating Control v4 – DIN rail enclosure (10 modules), parametric CadQuery model.

Usage:   <venv>/bin/python hardware/enclosure/build_enclosure.py
Output:  base.stl, cover.stl, cover_color.3mf (coloured labels, PAC-HP), enclosure.step and preview SVGs in the same directory.

Coordinates: X along the rail (0..W), Y perpendicular to the rail (0..H, 0 = top edge), Z outwards from the rail plane.
Profile: a 45 mm high front nose in the middle (protrudes through the distribution board cover), with steps above/below it for the
screw terminals (under the cover). Printed parts: base (rear skirt with DIN hook + power/IO board), spring DIN latch, cover (steps + nose), OLED spacer.
Base and cover are held together by 4 × M3 screws from the back into the cover bosses (3 JLC3DP threaded inserts, at H1 directly into the plastic).
Dimensions are parameters: measure the depth and the steps on an existing modular device.
"""
import json
import os
import zipfile

import cadquery as cq
from shapely.affinity import translate
from shapely.geometry import MultiPolygon, Polygon, box
from shapely.ops import unary_union

HERE = os.path.dirname(os.path.abspath(__file__))

# --- main dimensions (mm) -----------------------------------------------------------------
W = 175.0            # 10 modules × 17.5
H = 90.0             # height perpendicular to the rail
NOSE_H = 45.0        # front nose height (in Y), centred
DEPTH = 57.5         # from the rail plane to the front panel
STEP_Z = 27.0        # top surface of the steps (the terminal screws are reached from here)
WALL = 2.0
SPLIT_Z = 12.0       # split plane between base and cover
R = 1.5              # edge fillet

# boards (fitted to the enclosure interior)
LOWER_Z = 6.0        # underside of the power/IO board (HLK and relay pins protrude about 3.3 mm below)
PCB_T = 1.6
UPPER_Z = 44.0       # underside of the logic board, in the nose (directly behind the front panel)
LOWER_W, LOWER_H = W - 2 * WALL - 1.0, H - 2 * WALL - 1.0
UPPER_W, UPPER_H = W - 2 * WALL - 6.0, NOSE_H - 2 * WALL - 1.0

LOWER_X0 = (W - LOWER_W) / 2                     # left edge of the power/IO board in the enclosure (2.5 mm)
# screw terminals (top XY350V-3.5, bottom DB128V-5.08): wire entry behind the wall, screws above the pad row
# (measured from the 3D model). One window per block in the wall at the clamp opening height, screwdriver channels above the screws.
# win: wire hole from a section of the 3D model (XY350V: 1.8 × 1.8 mm, 1.4–3.2 mm high; DB128V: 3.2 × 4.2 mm, 1.9–6.1 mm high)
# + ~0.2 mm all round; one shared opening per block in the wall, at this height
TB = {"top": dict(pitch=3.5, win_w=2.2, win_z=(1.2, 3.4), screw_top=8.6, ch_id=2.6, ch_od=4.6),      # 2.5 mm screwdriver (M2)
      "bottom": dict(pitch=5.08, win_w=3.4, win_z=(1.8, 6.2), screw_top=14.0, ch_id=3.5, ch_od=5.6)}  # 3 mm screwdriver (M2.5)
# the terminal rows (screws sit above the pad row) – from the power/IO board, written by export_labels.py
TERMS = json.load(open(os.path.join(HERE, "..", "lower", "terminals.json")))
LOWER_Y0 = (H - (H - 2 * WALL - 1.0)) / 2           # top edge of the power/IO board in the enclosure (2.5 mm)
ROW_TOP = TERMS["row_y"]["top"] + LOWER_Y0
ROW_BOT = TERMS["row_y"]["bottom"] + LOWER_Y0

# front panel: front.json written by the logic board generator (in enclosure coordinates)
FRONT = json.load(open(os.path.join(HERE, "..", "logic", "front.json")))
WIN_RIM, WIN_T = 1.0, 1.0                           # OLED window glass rim and thickness
LED_D, TUBE_D = 3.1, 5.4                            # light pipe bore and tube (a Ø3 mm acrylic rod can also be pushed in)
ROD_L = 10.5                                        # length of the optional Ø3 mm acrylic light rod (LED top + 0.2 mm -> front panel)
BTN_D = 4.2

# DIN rail (TS35 × 7.5, EN 60715): rail front face at Z = 0, flanges 1 mm thick, 4–5 mm wide
RAIL_W = 35.0
Y_MID = H / 2
RAIL_Y0, RAIL_Y1 = Y_MID - RAIL_W / 2, Y_MID + RAIL_W / 2      # top and bottom rail edge (board coords)
SKIRT = 3.5          # depth of the rear skirt behind the rail plane (Z = -3.5 .. 0): contains the hook and the latch
CLR = 0.3            # clearance around the rail
LIP_Z = -1.3         # top plane of the hook / latch (behind the flange: 1 mm flange + 0.3 clearance)
LIP_OVER = 2.0       # how far it reaches behind the flange
X_C = W / 2          # centre of the latch (DIN slider)
SL_W, SL_RIB = 14.0, 10.0                    # DIN latch: width of the top plate and the lower rib
SL_TRAVEL = 3.0                              # pull-down travel to release (engagement behind the flange is 2.3 mm)
POCKET_Y = (70.0, 78.0)                      # pocket for the spring arms in the skirt
ARM_X = 26.5                                 # end of the spring arm (from the latch centre)

def body_profile():
    """outer body of the enclosure: steps + front nose"""
    steps = cq.Workplane("XY").box(W, H, STEP_Z, centered=(False, False, False)).edges("|Z").fillet(R)
    nose = (cq.Workplane("XY").workplane(offset=STEP_Z - 0.01)
            .center(0, (H - NOSE_H) / 2).box(W, NOSE_H, DEPTH - STEP_Z + 0.01, centered=(False, False, False))
            .edges("|Z").fillet(R))
    return steps.union(nose)


def hollow(solid):
    inner_steps = (cq.Workplane("XY").workplane(offset=WALL).center(WALL, WALL)
                   .box(W - 2 * WALL, H - 2 * WALL, STEP_Z - 2 * WALL, centered=(False, False, False)))
    inner_nose = (cq.Workplane("XY").workplane(offset=WALL).center(WALL, (H - NOSE_H) / 2 + WALL)
                  .box(W - 2 * WALL, NOSE_H - 2 * WALL, DEPTH - 2 * WALL, centered=(False, False, False)))
    return solid.cut(inner_steps).cut(inner_nose)


WIN_FLARE = 1.0      # the wire opening flares outwards: this much larger all round on the outer surface (trapezoidal section)


def _flared_window(x0, x1, z0, z1, y_in, y_out):
    """wire opening in the wall: x0..x1 × z0..z1 on the inner plane (y_in), widening linearly outwards to WIN_FLARE larger
    all round on the outer plane (y_out); extends 0.5 mm past the wall in both directions (with the same slope)"""
    d = (y_out - y_in) / abs(y_out - y_in)
    def rect(y):
        f = WIN_FLARE * (y - y_in) / (y_out - y_in)
        pts = [(x0 - f, y, z0 - f), (x1 + f, y, z0 - f), (x1 + f, y, z1 + f), (x0 - f, y, z1 + f)]
        return cq.Wire.makePolygon([cq.Vector(*p) for p in pts], close=True)
    return cq.Workplane("XY").add(cq.Solid.makeLoft([rect(y_in - 0.5 * d), rect(y_out + 0.5 * d)], True))


def terminal_openings(solid):
    """one shared wire opening per block in the wall, only at the height of the wire holes (not the full height of the
    screw terminal), with an outward-flaring (trapezoidal) section; one hole per screw on top of the step"""
    z_pcb = LOWER_Z + PCB_T
    for blk in TERMS["blocks"]:
        t = TB[blk["side"]]
        x0, x1 = blk["x0"] + LOWER_X0 - t["win_w"] / 2, blk["x1"] + LOWER_X0 + t["win_w"] / 2
        y_in, y_out = (WALL, 0.0) if blk["side"] == "top" else (H - WALL, H)
        solid = solid.cut(_flared_window(x0, x1, z_pcb + t["win_z"][0], z_pcb + t["win_z"][1], y_in, y_out))
    for p in TERMS["poles"]:
        t = TB[p["side"]]
        yc = ROW_TOP if p["side"] == "top" else ROW_BOT
        solid = solid.cut(cq.Workplane("XY").workplane(offset=STEP_Z - WALL - 1.0).center(p["x"] + LOWER_X0, yc)
                          .circle(t["ch_id"] / 2).extrude(WALL + 2.0))
    return solid


def screw_channels():
    """screwdriver channels in the cover (like the light pipes): from the step ceiling down to 0.3 mm above the screw head"""
    out = None
    for side in ("top", "bottom"):
        t = TB[side]
        yc = ROW_TOP if side == "top" else ROW_BOT
        z0 = LOWER_Z + PCB_T + t["screw_top"] + 0.3
        h = STEP_Z - WALL - z0 + 0.5
        pts = [(p["x"] + LOWER_X0, yc) for p in TERMS["poles"] if p["side"] == side]
        # neighbouring channels overlap (e.g. 4.6 mm tube at 3.5 mm pitch): separate cylinders, boolean union
        # (a single extrusion of overlapping circles would give a self-intersecting, invalid solid)
        tubes = [cq.Solid.makeCylinder(t["ch_od"] / 2, h, cq.Vector(x, y, z0)) for x, y in pts]
        bores = [cq.Solid.makeCylinder(t["ch_id"] / 2, h + 0.2, cq.Vector(x, y, z0 - 0.1)) for x, y in pts]
        c = tubes[0].fuse(*tubes[1:]).clean().cut(bores[0].fuse(*bores[1:]))
        out = c if out is None else out.fuse(c)
    return cq.Workplane("XY").add(out)

def front_openings(solid):
    zf = DEPTH - WALL - 1.0
    o = FRONT["oled"]     # window: active area (22.4 × 5.6) + 1.5 mm all round
    oled = (cq.Workplane("XY").workplane(offset=zf).center(o["aa_x"][0] - 1.5, (o["y0"] + o["y1"]) / 2 - 4.0)
            .box(o["aa_x"][1] - o["aa_x"][0] + 3.0, 8.0, WALL + 2.0, centered=(False, False, False)))
    solid = solid.cut(oled)
    # inner recess for the window glass (1 mm acrylic / PC sheet, glued in from inside): 1 mm larger than the window all round, 1 mm deep
    wx0, wx1 = o["aa_x"][0] - 1.5, o["aa_x"][1] + 1.5
    wyc = (o["y0"] + o["y1"]) / 2
    solid = solid.cut(_box(wx0 - WIN_RIM, wx1 + WIN_RIM, wyc - 4.0 - WIN_RIM, wyc + 4.0 + WIN_RIM, DEPTH - WALL - 0.5, DEPTH - WALL + WIN_T))
    for x, y in FRONT["leds"]:
        solid = solid.cut(cq.Workplane("XY").workplane(offset=zf).center(x, y).circle(LED_D / 2).extrude(WALL + 2.0))
    bx, by = FRONT["button"]
    return solid.cut(cq.Workplane("XY").workplane(offset=zf).center(bx, by).circle(BTN_D / 2).extrude(WALL + 2.0))


# USB-C opening in the lower side wall of the nose, stepped: outside, a recess for the plug overmould (at most
# 12.35 × 6.5 mm per the USB-C spec) so it seats fully (the receptacle face is 1.2 mm below the outer surface); inside, a tight rim
# around the receptacle's metal shell (8.94 × 4.1 mm + 0.3 mm clearance all round), so no large gap shows the PCB.
USB_W, USB_H, USB_R, USB_DEPTH = 13.5, 8.0, 1.5, 1.4         # plug overmould recess
USB_SHELL_W, USB_SHELL_R = 9.6, 1.0                         # rim opening: receptacle shell (8.94 mm) + 0.3 mm all round
USB_SHELL_Z = (UPPER_Z + PCB_T - 0.85 - 0.3, UPPER_Z + PCB_T + 3.25 + 0.3)   # the shell's front lip extends 0.85 mm below the board plane
USB_ZC = UPPER_Z + PCB_T + 1.63                     # receptacle centre (3.26 mm high body)


def _rounded_y(x, z, w, h, r, y0, y1):
    """rounded-corner box cutout along Y (y0 < y1)"""
    return (cq.Workplane("XZ", origin=(0, y0, 0)).center(x, z).rect(w, h).extrude(-(y1 - y0)).edges("|Y").fillet(r))


def usb_opening(solid):
    ux = FRONT["usb"][0]
    y_out = (H + NOSE_H) / 2                        # outer plane of the nose's lower wall
    solid = solid.cut(_rounded_y(ux, USB_ZC, USB_W, USB_H, USB_R, y_out - USB_DEPTH, y_out + 1.0))
    zc, h = (USB_SHELL_Z[0] + USB_SHELL_Z[1]) / 2, USB_SHELL_Z[1] - USB_SHELL_Z[0]
    return solid.cut(_rounded_y(ux, zc, USB_SHELL_W, h, USB_SHELL_R, y_out - WALL - 1.0, y_out - USB_DEPTH + 0.01))


def light_tubes():
    """light pipes from the front panel down to the LEDs (printed into the cover, so the LEDs do not bleed into each other)"""
    z0 = UPPER_Z + PCB_T + 0.1          # reaches the board (0.1 mm): the LED's side light cannot leak into the neighbouring pipe
    h = DEPTH - WALL - z0
    t = None
    for x, y in FRONT["leds"]:
        c = (cq.Workplane("XY").workplane(offset=z0).center(x, y).circle(TUBE_D / 2).circle(LED_D / 2).extrude(h))
        t = c if t is None else t.union(c)
    return t


def logic_bosses():
    """screw bosses for the logic board (M2.5 self-tapping, screwed in from the board underside)"""
    z0 = UPPER_Z + PCB_T
    h = DEPTH - WALL - z0
    pts = [tuple(p) for p in FRONT["holes"]]
    b = cq.Workplane("XY").workplane(offset=z0).pushPoints(pts).circle(2.8).extrude(h)
    return b.cut(cq.Workplane("XY").workplane(offset=z0).pushPoints(pts).circle(1.1).extrude(h))


def _yz(pts, x0, x1):
    """Y-Z profile extruded along X (x0..x1)"""
    return cq.Workplane("YZ", origin=(x0, 0, 0)).polyline(pts).close().extrude(x1 - x0)


def _box(x0, x1, y0, y1, z0, z1):
    return cq.Workplane("XY").box(x1 - x0, y1 - y0, z1 - z0, centered=False).translate((x0, y0, z0))


def din_skirt():
    """rear skirt: fixed hook at the top (behind the upper rail flange), guide and pocket for the spring DIN latch at the bottom.
    Printing: back face on the bed; the rail channel ceiling (the floor plate) is a 35.6 mm bridge."""
    sk = _box(0, W, 0, H, -SKIRT, 0).edges("|Z").fillet(R)
    # lightening pockets (ribs, frame and functional parts remain)
    for xa, xb in ((2.5, 42.5), (44.5, 85.5), (89.5, 130.5), (132.5, 172.5)):
        sk = sk.cut(_box(xa, xb, 2.5, RAIL_Y0 - 4.5, -SKIRT - 1, 0))
    for xa, xb in ((2.5, 42.5), (44.5, X_C - 32.0), (X_C + 32.0, 130.5), (132.5, 172.5)):
        sk = sk.cut(_box(xa, xb, RAIL_Y1 + 3.5, H - 2.5, -SKIRT - 1, 0))
    y0, y1 = RAIL_Y0 - CLR, RAIL_Y1 + CLR
    sk = sk.cut(_box(-1, W + 1, y0, y1, -SKIRT - 1, 0))                                   # rail channel
    # guide for the DIN latch: wide slot on top (Z -2.5..0), narrow throat below (Z -3.5..-2.5)
    sk = sk.cut(_box(X_C - SL_W / 2 - 0.2, X_C + SL_W / 2 + 0.2, y1, H + 1, -2.5, 0))
    sk = sk.cut(_box(X_C - SL_RIB / 2 - 0.2, X_C + SL_RIB / 2 + 0.2, y1, H + 1, -SKIRT - 1, -2.5))
    sk = sk.cut(_box(X_C - SL_W / 2 - 0.2, X_C + SL_W / 2 + 0.2, y1, RAIL_Y1 + 1.5, -SKIRT - 1, 0))   # room for the latch lip
    # pocket for the spring arms + stops that the arm ends press against
    sk = sk.cut(_box(X_C - ARM_X - 1.5, X_C + ARM_X + 1.5, POCKET_Y[0], POCKET_Y[1], -SKIRT - 1, 0))
    for sgn in (-1, 1):
        xa, xb = sorted((X_C + sgn * (ARM_X - 1.5), X_C + sgn * (ARM_X + 1.5)))
        sk = sk.union(_box(xa, xb, 74.6, POCKET_Y[1], -SKIRT, 0))
    # fixed hook on top: reaches behind the flange, chamfered tip (hooks on more easily when tilting on)
    hook = [(y0, LIP_Z), (RAIL_Y0 + LIP_OVER, LIP_Z), (RAIL_Y0 + LIP_OVER, -2.5), (RAIL_Y0 + LIP_OVER - 1.0, -SKIRT), (y0, -SKIRT)]
    return sk.union(_yz(hook, 10.0, W - 10.0))


def din_slider():
    """spring DIN latch (separate printed part, slid in at the bottom of the enclosure; the spring arms spread in the pocket and retain it).
    When snapping on, the sloped lip pushes it aside; to release, pull it down by its tab with a screwdriver (3 mm)."""
    yl = RAIL_Y1 - LIP_OVER
    lip = [(yl, LIP_Z), (RAIL_Y1 + 1.5, LIP_Z), (RAIL_Y1 + 1.5, -SKIRT), (RAIL_Y1 + 1.0, -SKIRT)]   # sloped lower edge: pushed by the flange
    sl = _yz(lip, X_C - SL_W / 2, X_C + SL_W / 2)
    sl = sl.union(_box(X_C - SL_W / 2, X_C + SL_W / 2, RAIL_Y1 + CLR, H + 4.0, -2.3, -0.2))          # top plate
    sl = sl.union(_box(X_C - SL_RIB / 2, X_C + SL_RIB / 2, RAIL_Y1 + 1.0, H + 4.0, -SKIRT, -2.3))    # rib in the throat
    sl = sl.union(_box(X_C - 10.0, X_C + 10.0, POCKET_Y[0], POCKET_Y[0] + 3.0, -SKIRT, -0.2))       # stop tabs
    for sgn in (-1, 1):                                                                              # spring arms (1 mm)
        (ax, ay), (bx, by) = (X_C + sgn * 9.5, 72.0), (X_C + sgn * ARM_X, 73.5)
        nx, ny = -(by - ay), (bx - ax)
        k = 0.5 / (nx * nx + ny * ny) ** 0.5
        pts = [(ax + nx * k, ay + ny * k), (bx + nx * k, by + ny * k), (bx - nx * k, by - ny * k), (ax - nx * k, ay - ny * k)]
        arm = cq.Workplane("XY").workplane(offset=-SKIRT).polyline(pts).close().extrude(SKIRT - 0.2)
        knob = cq.Workplane("XY").workplane(offset=-SKIRT).center(bx, 74.0).circle(0.6).extrude(SKIRT - 0.2)
        sl = sl.union(arm).union(knob)
    return sl.cut(_box(X_C - 3.0, X_C + 3.0, H + 0.8, H + 3.0, -SKIRT - 1, 1))                         # screwdriver tab

# cover fastening: 4 × M3 × 12 screws from the back (from the skirt pockets), through the base boss and the power/IO board hole into
# the cover boss – clamping the board and the cover at once. The enclosure can only be opened off the rail (230 V: live parts tool-access only).
# H2–H4: threaded insert installed by JLC3DP, type 5 (M3*4*5: 5 mm long, Ø4.0; hole Ø3.6 × 7 mm, at least 3 mm wall
# all round -> Ø9.8 mm boss). H1: no room for such a boss (the NTC 1-2 terminal starts 2.3 mm away), so a flatted Ø5.6 mm boss,
# the screw cuts its thread directly into the plastic. Corners are the power/IO board holes (build_lower.py), in enclosure coordinates.
CORNERS = [(x + LOWER_X0, y + LOWER_Y0) for x, y in ((3.5, 3.5), (166.5, 3.5), (3.5, 69.0), (166.5, 81.5))]
SCREW_CLR = 1.7      # Ø3.4 clearance hole
INSERT_D, INSERT_L = 3.6, 7.0                       # insert hole diameter and depth (JLC3DP type 5)
INSERT_BOSS_R = 4.9                                 # 3.1 mm wall around the insert (JLC3DP requires at least 3 mm)
PILOT_D = 2.7                                       # in the flatted (H1) boss: the M3 screw cuts directly into the PA12
BOSS_R = 2.8         # base bosses and the H1 boss in the cover
BOSS_FLAT = {0: 2.2}   # corner index -> flat on the +X side (the NTC 1-2 terminal starts 2.3 mm from H1)


def standoffs():
    s = cq.Workplane("XY").workplane(offset=WALL).pushPoints(CORNERS).circle(BOSS_R).extrude(LOWER_Z - WALL)
    holes = cq.Workplane("XY").workplane(offset=-SKIRT - 1).pushPoints(CORNERS).circle(SCREW_CLR).extrude(LOWER_Z + SKIRT + 2)
    return s, holes


def cover_bosses():
    """screw bosses in the cover: from the top of the power/IO board up to the step ceiling, with the insert hole at the bottom"""
    z0, z1 = LOWER_Z + PCB_T, STEP_Z - WALL + 0.5
    out = None
    for i, (x, y) in enumerate(CORNERS):
        r = BOSS_R if i in BOSS_FLAT else INSERT_BOSS_R
        b = cq.Workplane("XY").workplane(offset=z0).center(x, y).circle(r).extrude(z1 - z0)
        if i in BOSS_FLAT:
            b = b.cut(_box(x + BOSS_FLAT[i], x + BOSS_R + 1, y - BOSS_R - 1, y + BOSS_R + 1, z0 - 1, z1 + 1))
        d = PILOT_D if i in BOSS_FLAT else INSERT_D        # no room for an insert in the flatted boss: thread cut into the plastic
        b = b.cut(cq.Workplane("XY").workplane(offset=z0 - 0.1).center(x, y).circle(d / 2).extrude(INSERT_L + 0.1))
        out = b if out is None else out.union(b)
    return out


# OLED spacer (separate printed part): sits on the pin header plastic, with the module on top – so the glass is 0.8 mm below
# the inner face of the front panel (sitting on the header alone it would be 4.8 mm). Keeps the module straight while soldering.
HDR_H, OLED_LIFT, OLED_T = 2.5, 4.0, 2.6


def oled_spacer():
    o = FRONT["oled"]
    z0, h = UPPER_Z + PCB_T, HDR_H + OLED_LIFT
    px, pys = o["pin_x"], o["pins_y"]
    yc = (pys[0] + pys[-1]) / 2
    sp = _box(o["x0"] + 0.2, o["x1"] - 0.2, o["y0"] + 0.2, o["y1"] - 0.2, z0, z0 + h)
    sp = sp.cut(_box(px + 3.0, o["x1"] - 1.4, o["y0"] + 1.4, o["y1"] - 1.4, z0 - 1, z0 + h + 1))      # frame: hollow inside
    # room for the pin header: open across the full width (otherwise 0.47 mm side walls would remain) – the 3.8 mm
    # plate above it is carried by the frame
    sp = sp.cut(_box(px - 1.27 - 0.25, px + 1.27 + 0.25, o["y0"] - 1, o["y1"] + 1, z0 - 1, z0 + HDR_H + 0.2))
    for y in pys:
        sp = sp.cut(cq.Workplane("XY").workplane(offset=z0).center(px, y).circle(0.65).extrude(h + 1))  # the pins (0.64 mm □)
    return sp


shell = usb_opening(terminal_openings(front_openings(hollow(body_profile()))))
base_cut = cq.Workplane("XY").box(W + 2, H + 2, SPLIT_Z, centered=(False, False, False)).translate((-1, -1, 0))
st, screw_holes = standoffs()
base = shell.intersect(base_cut).union(st).union(din_skirt()).cut(screw_holes)
slider = din_slider()
spacer = oled_spacer()
_o = FRONT["oled"]
WIN_W = _o["aa_x"][1] - _o["aa_x"][0] + 3.0 + 2 * WIN_RIM - 0.3       # 0.15 mm clearance all round
WIN_H = 8.0 + 2 * WIN_RIM - 0.3
cq.exporters.export(cq.Workplane("XY").rect(WIN_W, WIN_H), os.path.join(HERE, "oled_window.dxf"))
window = _box(_o["aa_x"][0] - 1.5 - WIN_RIM + 0.15, _o["aa_x"][0] - 1.5 - WIN_RIM + 0.15 + WIN_W,
              (_o["y0"] + _o["y1"]) / 2 - WIN_H / 2, (_o["y0"] + _o["y1"]) / 2 + WIN_H / 2, DEPTH - WALL, DEPTH - WALL + WIN_T)
cover = shell.cut(base_cut).union(light_tubes()).union(logic_bosses()).union(cover_bosses()).union(screw_channels())
# the HLK-5M05 top (Z ≈ 25.7) pokes out 2 mm from under the nose into the step ceiling (Z = 25): recess in the ceiling (1.0 mm left, 0.3 mm clearance)
cover = cover.cut(_box(117.3, 141.7, 64.0, 68.2, STEP_Z - WALL - 1.0, STEP_Z - WALL + 1.0))

# --- true (physical) coordinates --------------------------------------------------------------------------
# The geometry above was built in board coordinates (y down). CadQuery is right-handed, so it must be mirrored: this way the
# enclosure seen from the front (from +Z) looks the same as the board in KiCad's top view (x right, top step at the top).
#   y_phys = H - y_pcb
base = base.mirror("XZ").translate((0, H, 0))
cover = cover.mirror("XZ").translate((0, H, 0))
slider = slider.mirror("XZ").translate((0, H, 0))
spacer = spacer.mirror("XZ").translate((0, H, 0))
window = window.mirror("XZ").translate((0, H, 0))

# --- engraved labels (made during printing) -------------------------------------------------------------------
# MJF/SLS (JLC3DP): engraved detail is recommended to be at least 0.8 mm deep and 0.8 mm wide. The depth matches; the strokes
# are ~0.5 mm (Barlow Bold, SIL OFL, fonts/) – a 0.8 mm stroke would be too heavy, so the JLC warning is acceptable.
# Letters are placed one by one (TXT_GAP letter spacing); TXT_GROW > 0 thickens them all round (shapely).
ENGRAVE = 0.8
FONT = os.path.join(HERE, "fonts", "Barlow-Bold.ttf")
TXT_GROW = 0.0       # letter thickening all round (mm)
TXT_GAP = 0.6        # letter spacing after thickening (mm)
TXT_POLE, TXT_GROUP, TXT_FRONT = 3.4, 4.3, 4.0      # font size (em, mm): pole marks (S, G, L, N …), group titles, front panel
_glyphs = {}


def _pts(w, step=0.02):
    n = max(16, int(w.Length() / step))
    return [(v.x, v.y) for v in (w.positionAt(i / n) for i in range(n))]


def _glyph(ch, size):
    """outline of one letter (shapely), thickened; space is None"""
    if (ch, size) not in _glyphs:
        fs = cq.Workplane("XY").text(ch, size, 1.0, fontPath=FONT, halign="left", valign="bottom").faces("<Z").vals()
        g = unary_union([Polygon(_pts(f.outerWire()), [_pts(w) for w in f.innerWires()]) for f in fs]) if fs else None
        _glyphs[(ch, size)] = g.buffer(TXT_GROW, join_style=1) if g is not None else None   # even with 0: repairs the outline
    return _glyphs[(ch, size)]


def label_solid(txt, size, halign="center"):
    """engraving solid of a label in the local XY plane: in the Z = -ENGRAVE .. +0.2 band, (0, 0) is the label centre (or left edge)"""
    x, parts = 0.0, []
    for ch in txt:
        g = _glyph(ch, size) if ch != " " else None
        if g is None:
            x += 0.3 * size
            continue
        parts.append(translate(g, x - g.bounds[0]))
        x += g.bounds[2] - g.bounds[0] + TXT_GAP
    u = unary_union(parts)
    x0, y0, x1, y1 = u.bounds
    u = translate(u, -(x0 if halign == "left" else (x0 + x1) / 2), -(y0 + y1) / 2)
    solids = []
    for p in (u.geoms if isinstance(u, MultiPolygon) else [u]):
        p = p.simplify(0.01)                        # fewer edges: the boolean cut takes seconds instead of minutes
        wire = lambda ring: cq.Wire.makePolygon([cq.Vector(a, b, -ENGRAVE) for a, b in ring.coords[:-1]], close=True)
        face = cq.Face.makeFromWires(wire(p.exterior), [wire(r) for r in p.interiors])
        solids.append(cq.Solid.extrudeLinear(face, cq.Vector(0, 0, ENGRAVE + 0.2)))
    return cq.Compound.makeCompound(solids)


CUTS = []            # the engraving solids; cut into the cover at the end


def engrave(txt, x, y, z, size):
    CUTS.append(label_solid(txt, size).moved(cq.Location(cq.Vector(x, y, z))))


def _height(txt, size):
    return label_solid(txt, size).BoundingBox().ylen


LINE_W = 0.6         # width of the engraved lines (U-shaped brackets)
U_H = 1.4            # total bracket height (including the legs)
ROW_GAP = 0.6        # line gap between the labels on the top step


def engrave_bracket(x0, x1, y_open, z, up=True):
    """engraved U-shaped bracket in physical coordinates: a horizontal line with legs at both ends (up: towards +y, otherwise
    towards -y); y_open is the leg end, the line is U_H further in"""
    yb = y_open - U_H if up else y_open + U_H - LINE_W
    y0, y1 = (yb, y_open) if up else (y_open, yb + LINE_W)
    shape = unary_union([box(x0, yb, x1, yb + LINE_W), box(x0, y0, x0 + LINE_W, y1), box(x1 - LINE_W, y0, x1, y1)])
    wire = cq.Wire.makePolygon([cq.Vector(a, b, z - ENGRAVE) for a, b in shape.exterior.coords[:-1]], close=True)
    CUTS.append(cq.Solid.extrudeLinear(cq.Face.makeFromWires(wire), cq.Vector(0, 0, ENGRAVE + 0.2)))


# top step (SELV), everything below the screw holes (towards the nose; there is only ~4.8 mm above the holes):
#   holes → S/G (signal/GND) → per-channel U-bracket under the pair → channel number → bank U-bracket → title (NTC, THERMOSTAT, OT)
# Row by row in board coordinates (y down); physical y = H - y.
TXT_TOP_CH, TXT_TOP_TITLE = 3.6, 3.6                # channel number and group title (so all five rows fit in the band)
top_poles = sorted((p for p in TERMS["poles"] if p["side"] == "top"), key=lambda p: p["x"])
y = ROW_TOP + TB["top"]["ch_id"] / 2 + 0.9          # hole edge + 0.9 mm
h_pole = _height("S", TXT_POLE)
y_pole = y + h_pole / 2
y = y + h_pole + ROW_GAP
y_pair = y                                          # leg ends of the pair brackets
y = y + U_H + ROW_GAP
h_ch = _height("8", TXT_TOP_CH)
y_ch = y + h_ch / 2
y = y + h_ch + ROW_GAP
y_bank = y
y = y + U_H + ROW_GAP
y_title = y + _height("NTC", TXT_TOP_TITLE) / 2
assert y_title + _height("NTC", TXT_TOP_TITLE) / 2 < (H - NOSE_H) / 2 - 0.5, "the top step labels do not fit"
for i, p in enumerate(top_poles):
    x = p["x"] + LOWER_X0
    lab = p["label"]
    engrave("S" if lab.isdigit() else lab, x, H - y_pole, STEP_Z, TXT_POLE)
    if lab.isdigit():                               # signal + the following GND form one channel
        x_g = top_poles[i + 1]["x"] + LOWER_X0
        engrave_bracket(x - 1.4, x_g + 1.4, H - y_pair, STEP_Z)
        engrave(lab, (x + x_g) / 2, H - y_ch, STEP_Z, TXT_TOP_CH)
# banks: group name from the board labels (NTC 1-2 -> NTC), extent from its blocks
banks = {}
for gr in (g for g in TERMS["groups"] if g["side"] == "top"):
    blk = next(b for b in TERMS["blocks"] if b["side"] == "top" and b["x0"] - 0.1 <= gr["x"] <= b["x1"] + 0.1)
    banks.setdefault(gr["label"].split(" ")[0], []).append(blk)
BANK_TITLES = {"TERM": "THERMOSTAT"}                # short group name on the board -> enclosure label
for name, blks in banks.items():
    x0, x1 = min(b["x0"] for b in blks) + LOWER_X0 - 1.6, max(b["x1"] for b in blks) + LOWER_X0 + 1.6
    engrave_bracket(x0, x1, H - y_bank, STEP_Z)
    engrave(BANK_TITLES.get(name, name), (x0 + x1) / 2, H - y_title, STEP_Z, TXT_TOP_TITLE)
# legend at the right end of the top step (next to OT)
_xl = (max(b["x1"] for b in banks["OT"]) + LOWER_X0 + 1.6 + W - WALL) / 2
engrave("S=SIG", _xl, H - (y_ch + y_bank) / 2, STEP_Z, TXT_TOP_CH)
engrave("G=GND", _xl, H - y_title, STEP_Z, TXT_TOP_CH)
# bottom step (230 V), likewise below the holes (towards the nose): holes → L/N → U-bracket per pair → pair name (Z1–Z8, P1, P2, 230V)
bot_poles = sorted((p for p in TERMS["poles"] if p["side"] == "bottom"), key=lambda p: p["x"])
y = ROW_BOT - TB["bottom"]["ch_id"] / 2 - 0.9       # hole edge - 0.9 mm (moving up, towards the nose)
h_pole = _height("N", TXT_POLE)
y_bpole = y - h_pole / 2
y = y - h_pole - ROW_GAP
y_bpair = y
y = y - U_H - ROW_GAP
y_bname = y - _height("Z8", TXT_GROUP) / 2
assert y_bname - _height("Z8", TXT_GROUP) / 2 > (H + NOSE_H) / 2 + 0.5, "the bottom step labels do not fit"
for p in bot_poles:
    engrave(p["label"], p["x"] + LOWER_X0, H - y_bpole, STEP_Z, TXT_POLE)
for gr in (g for g in TERMS["groups"] if g["side"] == "bottom"):
    xl, xn = sorted(p["x"] for p in bot_poles if abs(p["x"] - gr["x"]) < 3.0)       # the two poles of the pair (L, N)
    engrave_bracket(xl + LOWER_X0 - 1.6, xn + LOWER_X0 + 1.6, H - y_bpair, STEP_Z, up=False)
    engrave(gr["label"], gr["x"] + LOWER_X0, H - y_bname, STEP_Z, TXT_GROUP)
# front panel: 1-8, P1, P2, ST below the LEDs
LED_LABELS = [str(i) for i in range(1, 9)] + ["P1", "P2", "ST"]
for (x, y), lab in zip(FRONT["leds"], LED_LABELS):
    engrave(lab, x, H - y - 4.6, DEPTH, TXT_FRONT)
bx, by = FRONT["button"]
engrave("OK", bx, H - by - 5.2, DEPTH, TXT_FRONT)
# USB-C: label next to the opening, on the lower side wall of the nose (vertical surface, readable from outside, from -y)
USB_LABEL = "USB · FLASH / LOG"                     # firmware upload and serial log (the ESP32-C6 built-in USB serial port)
_yf = H - (H + NOSE_H) / 2                          # outer plane of the nose's lower wall in physical coordinates
_lbl = label_solid(USB_LABEL, TXT_FRONT, halign="left").moved(cq.Location(
    cq.Plane(origin=(FRONT["usb"][0] + USB_W / 2 + 2.0, _yf, USB_ZC), xDir=(1, 0, 0), normal=(0, -1, 0))))

CUTS.append(_lbl)
for c in CUTS:       # one at a time and without clean: otherwise some labels left a closed void below the surface
    cover = cover.cut(c, clean=False)

# boards (for preview / fit check only)
lower_pcb = cq.Workplane("XY").workplane(offset=LOWER_Z).center((W - LOWER_W) / 2, (H - LOWER_H) / 2).box(
    LOWER_W, LOWER_H, PCB_T, centered=(False, False, False))
upper_pcb = cq.Workplane("XY").workplane(offset=UPPER_Z).center(FRONT["origin"][0], H - FRONT["origin"][1] - FRONT["board"][1]).box(
    FRONT["board"][0], FRONT["board"][1], PCB_T, centered=(False, False, False))

cq.exporters.export(base, os.path.join(HERE, "base.stl"))
cq.exporters.export(cover, os.path.join(HERE, "cover.stl"))


# --- colour 3MF (JLC3DP PAC-HP, colour MJF): the body is one colour, the engraving (floor and walls) in the label colour ------------
COLOR_BODY, COLOR_LABEL = "#D7D7D7", "#1E1E1E"       # light grey enclosure (usual for DIN devices), dark labels


def export_color_3mf(shape, cuts, path, tol=0.02, ang=0.1):
    """cover mesh coloured per triangle: faces lying on the engraving solids (letter floors and walls) get the label colour"""
    solid = shape.val()
    solid.mesh(tol, ang)
    boxes = [(c, c.BoundingBox()) for c in cuts]
    verts, index, tris = [], {}, []
    for f in solid.Faces():
        vs, ts = f.tessellate(tol, ang)
        if not ts:
            continue
        # a point guaranteed to lie on the face: centroid of its largest triangle
        a, b, c = max(((vs[i], vs[j], vs[k]) for i, j, k in ts), key=lambda t: (t[1] - t[0]).cross(t[2] - t[0]).Length)
        p = (a + b + c) * (1 / 3)
        lab = any(bb.xmin - 0.01 <= p.x <= bb.xmax + 0.01 and bb.ymin - 0.01 <= p.y <= bb.ymax + 0.01 and
                  bb.zmin - 0.01 <= p.z <= bb.zmax + 0.01 and cut.isInside(p, 1e-3) for cut, bb in boxes)
        ids = []
        for v in vs:
            key = (round(v.x, 4), round(v.y, 4), round(v.z, 4))
            if key not in index:
                index[key] = len(verts)
                verts.append(key)
            ids.append(index[key])
        tris += [(ids[i], ids[j], ids[k], 1 if lab else 0) for i, j, k in ts if len({ids[i], ids[j], ids[k]}) == 3]
    model = ['<?xml version="1.0" encoding="UTF-8"?>',
             '<model unit="millimeter" xml:lang="en-US" xmlns="http://schemas.microsoft.com/3dmanufacturing/core/2015/02" '
             'xmlns:m="http://schemas.microsoft.com/3dmanufacturing/material/2015/02">',
             '<resources>',
             f'<m:colorgroup id="1"><m:color color="{COLOR_BODY}FF"/><m:color color="{COLOR_LABEL}FF"/></m:colorgroup>',
             '<object id="2" type="model" pid="1" pindex="0"><mesh><vertices>']
    model += [f'<vertex x="{x}" y="{y}" z="{z}"/>' for x, y, z in verts]
    model.append('</vertices><triangles>')
    model += [f'<triangle v1="{i}" v2="{j}" v3="{k}" pid="1" p1="{c}"/>' for i, j, k, c in tris]
    model += ['</triangles></mesh></object>', '</resources>', '<build><item objectid="2"/></build>', '</model>']
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("[Content_Types].xml", '<?xml version="1.0" encoding="UTF-8"?><Types xmlns="http://schemas.openxmlformats.org/'
                   'package/2006/content-types"><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.'
                   'relationships+xml"/><Default Extension="model" ContentType="application/vnd.ms-package.3dmanufacturing-'
                   '3dmodel+xml"/></Types>')
        z.writestr("_rels/.rels", '<?xml version="1.0" encoding="UTF-8"?><Relationships xmlns="http://schemas.openxmlformats.org/'
                   'package/2006/relationships"><Relationship Target="/3D/3dmodel.model" Id="rel0" Type="http://schemas.'
                   'microsoft.com/3dmanufacturing/2013/01/3dmodel"/></Relationships>')
        z.writestr("3D/3dmodel.model", "\n".join(model))
    return len(verts), len(tris), sum(t[3] for t in tris)


# threaded insert positions for the JLC3DP drawing (export_insert_drawing.py): in the cover's physical coordinates, plus the insert face (Z)
json.dump({"W": W, "H": H, "z_face": LOWER_Z + PCB_T, "z_rim": SPLIT_Z, "insert_d": INSERT_D, "insert_l": INSERT_L, "boss_d": 2 * INSERT_BOSS_R,
           "pilot_d": PILOT_D, "holes": [{"ref": f"H{i + 1}", "x": x, "y": H - y, "insert": i not in BOSS_FLAT}
                                         for i, (x, y) in enumerate(CORNERS)]},
          open(os.path.join(HERE, "inserts.json"), "w"), indent=1)
print("cover_color.3mf: %d vertices, %d triangles, %d of them label-coloured" % export_color_3mf(cover, CUTS, os.path.join(HERE, "cover_color.3mf")))
cq.exporters.export(slider, os.path.join(HERE, "din_slider.stl"))
cq.exporters.export(spacer, os.path.join(HERE, "oled_spacer.stl"))
asm = (cq.Assembly().add(base, name="base", color=cq.Color(0.25, 0.25, 0.28))
       .add(slider, name="din_slider", color=cq.Color(0.9, 0.5, 0.1))
       .add(spacer, name="oled_spacer", color=cq.Color(0.2, 0.2, 0.2))
       .add(window, name="oled_window", color=cq.Color(0.8, 0.9, 1.0, 0.4))
       .add(cover, name="cover", color=cq.Color(0.85, 0.85, 0.82, 0.6))
       .add(lower_pcb, name="lower_pcb", color=cq.Color(0.1, 0.45, 0.2))
       .add(upper_pcb, name="upper_pcb", color=cq.Color(0.1, 0.35, 0.6)))
asm.save(os.path.join(HERE, "enclosure.step"))
opts = dict(width=900, height=500, showAxes=False, projectionDir=(1.0, -1.4, 0.9), strokeWidth=0.25, showHidden=False)
cq.exporters.export(cover.union(base), os.path.join(HERE, "preview_iso.svg"), opt=opts)
cq.exporters.export(cover.union(base), os.path.join(HERE, "preview_side.svg"),
                    opt=dict(width=600, height=400, showAxes=False, projectionDir=(1, 0, 0), strokeWidth=0.3, showHidden=True))
print("done: base.stl, cover.stl, din_slider.stl, oled_spacer.stl, oled_window.dxf, enclosure.step, preview_*.svg")
