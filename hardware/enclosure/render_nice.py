"""ESP Heating Control v4 – shaded colour assembly images (VTK): exploded and closed views.

Preparation (coloured board GLBs from KiCad, enclosure parts as STL in the right position):
  kicad-cli pcb export glb --subst-models --include-pads --include-silkscreen --include-soldermask -o <dir>/lower.glb lower/lower.kicad_pcb
  (same for logic.glb), then the enclosure parts: see docs/ / export_render_parts.py
Usage:   <vtk venv>/bin/python hardware/enclosure/render_nice.py <dir> [exploded|closed|closed_top|side_bot|side_top]
Output:  ../renders/asm_<mode>_render.png (closed: from the 230 V terminals, closed_top: from the SELV terminals; side_bot / side_top: the same
         straight on, with parallel projection)
"""
import json
import os
import sys

import vtk

HERE = os.path.dirname(os.path.abspath(__file__))
D = sys.argv[1]
MODE = sys.argv[2] if len(sys.argv) > 2 else "exploded"
L = json.load(open(os.path.join(D, "layout.json")))
H, OFF = L["H"], (L["off"] if MODE == "exploded" else {k: 0.0 for k in L["off"]})
SIDE = MODE.startswith("side")

ren = vtk.vtkRenderer()
win = vtk.vtkRenderWindow()
win.SetOffScreenRendering(1)
win.AddRenderer(ren)
W_PX, H_PX = (1800, 2400) if MODE == "exploded" else ((2600, 1100) if SIDE else (2400, 1600))
TOPSIDE = MODE in ("closed_top", "side_top")
win.SetSize(W_PX, H_PX)
win.SetMultiSamples(0)

PA12 = (0.70, 0.71, 0.73)


def stl(name, color, dz=0.0, opacity=1.0, spec=0.15, metal=False):
    """enclosure parts: the STLs are already in exploded position; in the closed view they are moved back"""
    path = os.path.join(D, name + ".stl")
    r = vtk.vtkSTLReader()
    r.SetFileName(path)
    nrm = vtk.vtkPolyDataNormals()
    nrm.SetInputConnection(r.GetOutputPort())
    nrm.SetFeatureAngle(35)
    nrm.SplittingOn()
    m = vtk.vtkPolyDataMapper()
    m.SetInputConnection(nrm.GetOutputPort())
    a = vtk.vtkActor()
    a.SetMapper(m)
    p = a.GetProperty()
    p.SetColor(*color)
    p.SetOpacity(opacity)
    p.SetSpecular(0.6 if metal else spec)
    p.SetSpecularPower(40 if metal else 12)
    p.SetDiffuse(0.85)
    p.SetAmbient(0.12)
    a.SetPosition(0, 0, dz)
    ren.AddActor(a)


def board(name, x0, y0, z0):
    """KiCad GLB: metres, Y up; x = board x, z = board y, y = height above the board bottom -> enclosure physical coordinates"""
    imp = vtk.vtkGLTFImporter()
    imp.SetFileName(os.path.join(D, name + ".glb"))
    w = vtk.vtkRenderWindow()
    w.SetOffScreenRendering(1)
    imp.SetRenderWindow(w)
    imp.Update()
    T = vtk.vtkMatrix4x4()
    for i, row in enumerate(((1000, 0, 0, x0), (0, 0, -1000, H - y0), (0, 1000, 0, z0), (0, 0, 0, 1))):
        for j, v in enumerate(row):
            T.SetElement(i, j, v)
    acts = imp.GetRenderer().GetActors()
    acts.InitTraversal()
    for _ in range(acts.GetNumberOfItems()):
        a = acts.GetNextActor()
        um = a.GetUserMatrix()
        M = vtk.vtkMatrix4x4()
        if um is not None:
            vtk.vtkMatrix4x4.Multiply4x4(T, um, M)
        else:
            M.DeepCopy(T)
        a.SetUserMatrix(M)
        a.GetProperty().SetAmbient(0.12)
        ren.AddActor(a)


# --- scene -------------------------------------------------------------------------------------------------
ex = MODE == "exploded"
stl("base", PA12)
stl("slider", tuple(c * 0.92 for c in PA12))
stl("cover", PA12, dz=OFF["cover"] - L["off"]["cover"])
stl("window", (0.80, 0.92, 1.0), dz=OFF["cover"] - L["off"]["cover"], opacity=0.35, spec=0.9)
stl("spacer", (0.18, 0.18, 0.19), dz=OFF["logic"] - L["off"]["logic"])
if ex:
    stl("screws", (0.62, 0.63, 0.66), metal=True)
    stl("inserts", (0.80, 0.62, 0.28), metal=True)
board("lower", 2.5, 2.5, L["LOWER_Z"] + OFF["lower"])
board("logic", L["origin"][0], L["origin"][1], L["UPPER_Z"] + OFF["logic"])

# --- light, background, shading ------------------------------------------------------------------------------
ren.GradientBackgroundOn()
ren.SetBackground(0.86, 0.87, 0.89)
ren.SetBackground2(1.0, 1.0, 1.0)
lk = vtk.vtkLightKit()
lk.SetKeyLightIntensity(0.55 if SIDE else 0.95)
lk.AddLightsToRenderer(ren)
if SIDE:      # straight-on light is flat: add a light from above/side so openings and edges stand out
    sl = vtk.vtkLight()
    sl.SetLightTypeToSceneLight()
    sl.SetPosition(L["W"] * 0.2, (-300.0 if not TOPSIDE else H + 300.0), L["DEPTH"] + 250.0)
    sl.SetFocalPoint(L["W"] / 2, H / 2, L["DEPTH"] / 2)
    sl.SetIntensity(0.35)
    ren.AddLight(sl)
ren.SetUseFXAA(True)
basic = vtk.vtkRenderStepsPass()
ssao = vtk.vtkSSAOPass()
ssao.SetRadius(5.0)
ssao.SetBias(0.6)
ssao.SetKernelSize(128)
ssao.BlurOn()
ssao.SetDelegatePass(basic)
ren.SetPass(ssao)

cam = ren.GetActiveCamera()
cam.SetViewUp(0, 0, 1)
cx, cy = L["W"] / 2, H / 2
cz = (OFF["cover"] + L["DEPTH"] - 25.0) / 2 if ex else L["DEPTH"] / 2
cam.SetFocalPoint(cx, cy, cz)
if SIDE:      # straight on: view perpendicular to the terminal wall, front panel up
    d = (0.0, 1.0, 0.0) if TOPSIDE else (0.0, -1.0, 0.0)
    cz = L["DEPTH"] / 2
    cam.SetFocalPoint(cx, cy, cz)
else:
    d = (-0.55, -1.0, 0.62) if ex else ((0.6, 1.0, 0.9) if TOPSIDE else (-0.6, -1.0, 0.9))
cam.SetPosition(cx + d[0] * 600, cy + d[1] * 600, cz + d[2] * 600)
cam.SetViewAngle(24)
if SIDE:
    cam.ParallelProjectionOn()
ren.ResetCamera()
cam.Zoom(1.25 if ex else (2.05 if SIDE else 1.35))
ren.ResetCameraClippingRange()

win.Render()
w2i = vtk.vtkWindowToImageFilter()
w2i.SetInput(win)
w2i.ReadFrontBufferOff()
w2i.Update()
out = os.path.join(HERE, "..", "renders", f"asm_{MODE}_render.png")
wr = vtk.vtkPNGWriter()
wr.SetFileName(out)
wr.SetInputConnection(w2i.GetOutputPort())
wr.Write()
print("done:", out)
