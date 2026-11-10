"""ESP Heating Control – assembly views (enclosure + both boards), SVG line drawings from several viewpoints.

Usage:   <cadquery venv>/bin/python hardware/enclosure/scripts/render_assembly.py <lower.step> <logic.step>
The board STEPs are exported by KiCad:  kicad-cli pcb export step --subst-models -o lower.step hardware/pcb/lower/lower.kicad_pcb
Output:  hardware/renders/asm_*.svg
"""
import os
import sys

import cadquery as cq

HERE = os.path.dirname(os.path.abspath(__file__))
sys.argv, args = sys.argv[:1], sys.argv[1:]
import build_enclosure as E  # noqa: E402  (builds the base and the cover)

OUT = os.path.join(HERE, "..", "..", "renders")       # hardware/renders
os.makedirs(OUT, exist_ok=True)


def board(step, x, y, z):
    """KiCad STEP: X = x, Y = -y (true right-handed) -> into the enclosure's physical coordinates (y_phys = H - y_pcb), no mirroring"""
    s = cq.importers.importStep(step).val()
    return s.translate(cq.Vector(x, E.H - y, z))


lower = board(args[0], 2.5, 2.5, E.LOWER_Z)
logic = board(args[1], E.FRONT["origin"][0], E.FRONT["origin"][1], E.UPPER_Z)
base, cover, slider, spacer, window = E.base.val(), E.cover.val(), E.slider.val(), E.spacer.val(), E.window.val()


def screws(dz=0.0):
    """4 × M3 × 12 screws from the back, heads in the base pockets (illustration)"""
    return [cq.Workplane("XY").workplane(offset=-1.8 + dz).center(x, E.H - y).circle(2.75).extrude(1.8)
            .faces(">Z").workplane().circle(1.5).extrude(12.0).val() for x, y in E.CORNERS]


def inserts():
    """the M3 threaded inserts installed by JLC3DP in the cover bosses (none in a flatted boss, see BOSS_FLAT)"""
    return [cq.Workplane("XY").workplane(offset=E.LOWER_Z + E.PCB_T).center(x, E.H - y).circle(2.0).circle(1.5).extrude(5.0).val()
            for i, (x, y) in enumerate(E.CORNERS) if i not in E.BOSS_FLAT]


def din_rail(length=215.0):
    """TS35 × 7.5 rail (illustration): 1 mm sheet, front face at Z = 0"""
    y0, y1, x0 = E.RAIL_Y0, E.RAIL_Y1, (E.W - length) / 2
    parts = [E._box(x0, x0 + length, y0, y0 + 5.0, -1.0, 0), E._box(x0, x0 + length, y1 - 5.0, y1, -1.0, 0),
             E._box(x0, x0 + length, y0 + 4.0, y0 + 5.0, -7.5, 0), E._box(x0, x0 + length, y1 - 5.0, y1 - 4.0, -7.5, 0),
             E._box(x0, x0 + length, y0 + 4.0, y1 - 4.0, -7.5, -6.5)]
    r = parts[0]
    for q in parts[1:]:
        r = r.union(q)
    return r.mirror("XZ").translate((0, E.H, 0)).val()


rail = din_rail()
SPIN_NOTCH = -90.0   # the projection would rotate the image: this keeps the power/IO board at the bottom, the logic board on top


def ffc_cable():
    """26-way FFC (type A): leaves the power/IO board connector (in +y), arcs back over it, rises vertically through the notch
    in the logic board edge, and turns 90° at the top into the top-side connector (its opening faces the notch)"""
    x_c = 60.0 + 2.5                                    # FFC centre in the enclosure (power/IO board J30: x = 60)
    # (the path is given in board coordinates and mirrored into the physical frame at the end)
    oy = E.FRONT["origin"][1]
    z0 = E.LOWER_Z + E.PCB_T + 1.25                     # height of the lower connector slot
    zt = E.UPPER_Z + E.PCB_T + 1.25                     # same for the upper connector (on top of the board)
    yn = oy + 2.25                                      # notch centre
    pts = [(30.0, z0), (36.0, z0 + 0.5), (39.5, z0 + 6.0), (37.5, z0 + 15.0), (31.0, z0 + 21.0), (yn, z0 + 28.0),
           (yn, E.UPPER_Z - 1.0), (yn + 0.3, E.UPPER_Z + E.PCB_T + 0.4), (oy + 4.5, zt), (oy + 8.5, zt)]
    path = cq.Workplane("YZ").spline([cq.Vector(y, z) for y, z in pts], tangents=[(1, 0), (1, 0)])
    prof = cq.Workplane("XZ", origin=(x_c, pts[0][0], pts[0][1])).rect(26.0, 0.3)
    return prof.sweep(path).val().mirror("XZ").translate(cq.Vector(0, E.H, 0))

try:
    cable = ffc_cable()
except Exception as exc:          # the ribbon is illustration only
    print("FFC ribbon:", exc)
    cable = None


def svg(shape, name, direction, w=1400, h=900, hidden=False, spin=0.0):
    # the enclosure "lies on its back" (front panel up): the SVG exporter has no "up" direction, so this orients the image correctly;
    # spin: image rotation (degrees) about the view direction, in case the projection turns it sideways or upside down
    shape = shape.rotate(cq.Vector(0, 0, 0), cq.Vector(1, 0, 0), -90)
    if spin:
        c = shape.BoundingBox().center
        shape = shape.rotate(c, c + cq.Vector(*direction), spin)
    cq.exporters.export(cq.Workplane().add(shape), os.path.join(OUT, name + ".svg"),
                        opt=dict(width=w, height=h, showAxes=False, projectionDir=direction, strokeWidth=0.2, showHidden=hidden))
    print("done:", name)


closed = cq.Compound.makeCompound([base, cover, slider, window] + screws() + inserts())
svg(closed, "asm_closed_front", (1.0, 1.0, 1.0))
svg(closed, "asm_closed_top", (1.0, 1.0, -1.0))         # the top (SELV) terminal side, with the labels
svg(closed, "asm_closed_back", (-1.0, -1.0, 1.0))       # the back, DIN rail side
svg(closed, "asm_closed_side", (1.0, 0.0, 0.0), hidden=True, spin=180.0)   # front panel up

# exploded view: base, power/IO board, logic board, cover pulled apart vertically
dz = 70.0
exploded = cq.Compound.makeCompound([s_.translate(cq.Vector(0, 0, -25.0)) for s_ in screws()] + [base, slider, lower.translate(cq.Vector(0, 0, dz)), logic.translate(cq.Vector(0, 0, 2 * dz)), spacer.translate(cq.Vector(0, 0, 2 * dz)),
                                     cover.translate(cq.Vector(0, 0, 3 * dz + 40.0))])   # the cover is tall: keep it from hiding the logic board
if cable:   # both boards and the cable together, in place, viewed from the top terminals (the ribbon is visible between the boards)
    stack = cq.Compound.makeCompound([lower, logic, spacer, cable])
    svg(stack, "asm_boards_ffc", (0.4, 0.3, -1.0), w=1500, h=900)
    svg(stack, "asm_boards_ffc_notch", (0.15, 0.45, -1.0), w=1500, h=900, spin=SPIN_NOTCH)   # boards horizontal
    svg(stack, "asm_boards_ffc_side", (1.0, 0.35, -1.0), w=1500, h=900)
svg(exploded, "asm_exploded", (1.0, 1.0, 1.0), w=1200, h=1500)

# open enclosure: base + both boards in place, no cover
opened = cq.Compound.makeCompound([base, slider, lower, logic, spacer] + inserts() + ([cable] if cable else []))
svg(opened, "asm_open", (1.0, 1.0, 1.2))
svg(cq.Compound.makeCompound([base, slider, lower]), "asm_lower_in_base", (1.0, 1.0, 1.4))

# OLED on its spacer: section of the logic board, spacer and cover through the OLED centre (glass and front panel window)
xo = (E.FRONT["oled"]["aa_x"][0] + E.FRONT["oled"]["aa_x"][1]) / 2
cut = E._box(xo - 0.01, E.W + 5, -5, E.H + 10, -20, 80).val()
osec = cq.Compound.makeCompound([s_.intersect(cut) for s_ in (cover, window, logic, spacer)])
osec = osec.rotate(cq.Vector(0, E.H / 2, 0), cq.Vector(1, E.H / 2, 0), 180)
svg(osec, "asm_oled_section", (-1.0, 0.0, 0.0), w=1400, h=900)
# LED row section: the light pipes reach down to the board, each LED in its own pipe
yl = E.H - E.FRONT["leds"][0][1]
lcut = E._box(70.0, E.W - 2.0, yl - 0.01, yl + 30.0, 30.0, 70.0).val()
lsec = cq.Compound.makeCompound([s_.intersect(lcut) for s_ in (cover, logic)])
svg(lsec, "asm_led_section", (0.0, 0.0, 1.0), w=1600, h=700)
svg(cq.Compound.makeCompound([logic, spacer]), "asm_oled_spacer", (1.0, 0.8, 0.9), w=1400, h=900)

# DIN mounting: back with the rail, back without the rail (DIN latch pulled out), and a section through the latch centre
svg(cq.Compound.makeCompound([base, cover, slider, rail] + screws() + inserts()), "asm_din_back", (-0.35, -1.0, 0.3), w=1400, h=1000)
pulled = slider.translate(cq.Vector(0, -12.0, 0))     # DIN latch pulled out (slides in from below during assembly)
svg(cq.Compound.makeCompound([base, pulled]), "asm_din_slider", (-0.35, -1.0, 0.25), w=1400, h=1000)
half = E._box(E.X_C - 0.01, E.W + 5, -5, E.H + 10, -20, 80).val()
sec = cq.Compound.makeCompound([s.intersect(half) for s in (base, cover, slider, rail)])
sec = sec.rotate(cq.Vector(0, E.H / 2, 0), cq.Vector(1, E.H / 2, 0), 180)   # front panel up, rail at the bottom
svg(sec, "asm_din_section", (-1.0, 0.0, 0.0), w=1400, h=900)
