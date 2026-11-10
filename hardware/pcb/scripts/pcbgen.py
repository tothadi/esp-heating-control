"""Shared PCB generator for the boards (KiCad 10 pcbnew API).

The proven flow of the previous (v3) gateway board generator, organised into a class:
placement, manual (locked) 230 V tracks, keepout zones, Freerouting (without GND), GND fill,
SMD GND fan-outs, via grid, via stitching of islands, A* supplementary router, DRC-based stub cleanup,
JLCPCB BOM/CPL (with centroid and rotation correction).
Runs with KiCad's bundled Python:
  /Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/3.9/bin/python3
"""
import csv
import heapq
import json
import math
import os
import re
import subprocess
import tempfile

import pcbnew

# KiCad install paths (macOS defaults; override with environment variables on Linux/Windows)
KICAD_FP = os.environ.get("KICAD_FP", "/Applications/KiCad/KiCad.app/Contents/SharedSupport/footprints/")
CLI = os.environ.get("KICAD_CLI", "/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli")
JAVA = os.environ.get("FR_JAVA", "/opt/homebrew/opt/openjdk@21/bin/java")
LCSC_LIB = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "components", "footprints.pretty")   # LCSC/EasyEDA footprints (KiCad nickname "lcsc")
LCSC_3D = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "components", "3d-models")           # their STEP models
# 3D model addition / replacement by footprint name: (file, offset x, y, z) – the STEP origin is the body centre,
# the offset comes from matching the pins / mounting tabs to the pads (WRL origins differ, so STEP everywhere)
MODEL_FIX = {
    "JUSHUO_AFA07-S26FCA-00_1x26-1MP_P1.0mm_Horizontal": ("FPC-SMD_AFA07-S26FCA-00.step", 0.0, 0.76, 0.0),   # KiCad has no model
    # KH-6X6X13H-TJ: the LCSC package only has the generic 5 mm model -> use KiCad's 13 mm model (origin at pin 1)
    "SW-TH_4P-L6.0-W6.0-P4.50-LS6.5": ("${KICAD10_3DMODEL_DIR}/Button_Switch_THT.3dshapes/SW_PUSH_6mm_H13mm.step", -3.25, 2.25, 0.0),
    # Littelfuse 372 / TR5: KiCad 10 ships no model for this footprint -> the LCSC model of C354902 (origin at pin 1)
    "Fuse_Littelfuse_372_D8.50mm": ("FUSE-TH_L8.5-W4.0-H8.5-P5.08.step", 2.54, 0.0, 0.0),
}


def fix_models(fp):
    m = MODEL_FIX.get(str(fp.GetFPID().GetLibItemName()))
    if not m:
        return
    fp.Models().clear()
    mod = pcbnew.FP_3DMODEL()
    # project-relative path, so the boards open on any machine (the board files live in hardware/pcb/lower|logic/)
    mod.m_Filename = m[0] if m[0].startswith("${") else "${KIPRJMOD}/../components/3d-models/" + m[0]
    mod.m_Offset = pcbnew.VECTOR3D(m[1], m[2], m[3])
    fp.Models().push_back(mod)
MM = pcbnew.FromMM
ROT_FIX = [(r"^(SOIC|SSOP|MSOP|TSSOP|SOT-23-6)", 270), (r"^CP_Elec_", 180)]


class Gen:
    def __init__(self, out, w, h):
        self.out, self.w, self.h = out, w, h
        self.board = pcbnew.BOARD()
        self.nets = {}
        self.assy = {}       # ref -> (value, footprint, LCSC)
        self.tmp_areas = []
        self.edge()

    # --- basics ------------------------------------------------------------------------------
    def net(self, name):
        if name not in self.nets:
            found = self.board.FindNet(name)
            if found is not None and found.GetNetCode() > 0:
                self.nets[name] = found
                return found
            n = pcbnew.NETINFO_ITEM(self.board, name)
            self.board.Add(n)
            self.nets[name] = n
        return self.nets[name]

    def _load(self, lib, name, ref, value):
        path = LCSC_LIB if lib == "lcsc" else KICAD_FP + lib + ".pretty"
        fp = pcbnew.FootprintLoad(path, name)
        if fp is None:
            raise RuntimeError(f"no such footprint: {lib}:{name}")
        fix_models(fp)
        fp.SetReference(ref)
        fp.SetValue(value)
        self.board.Add(fp)
        fp.Reference().SetVisible(False)
        fp.Value().SetVisible(False)
        return fp

    def place(self, lib, name, ref, xy, rot=0, value="", anchor=None, part=None):
        """anchor = number of the pad placed at the given position (None: the footprint origin)"""
        fp = self._load(lib, name, ref, value or name)
        fp.SetPosition(pcbnew.VECTOR2I(0, 0))
        if rot:
            fp.SetOrientationDegrees(rot)
        off = pcbnew.VECTOR2I(0, 0)
        if anchor is not None:
            off = [p for p in fp.Pads() if p.GetNumber() == str(anchor)][0].GetPosition()
        fp.SetPosition(pcbnew.VECTOR2I(MM(xy[0]) - off.x, MM(xy[1]) - off.y))
        for pd in fp.Pads():          # unnumbered holes without an annular ring are mounting pegs: NPTH
            if pd.GetNumber() == "" and pd.GetAttribute() == pcbnew.PAD_ATTRIB_PTH and \
                    pd.GetSizeX() <= pd.GetDrillSizeX() + 1000:
                pd.SetAttribute(pcbnew.PAD_ATTRIB_NPTH)
                pd.SetLayerSet(pcbnew.PAD.UnplatedHoleMask())
        if part:
            self.assy[ref] = part
        return fp

    def pin(self, fp, number, netname):
        if not netname:
            return
        for p in fp.Pads():
            if p.GetNumber() == str(number):
                p.SetNet(self.net(netname))

    def pad_near(self, fp, xy, netname):
        if not netname:
            return
        tx, ty = MM(xy[0]), MM(xy[1])
        p = min(fp.Pads(), key=lambda p: (p.GetPosition().x - tx) ** 2 + (p.GetPosition().y - ty) ** 2)
        p.SetNet(self.net(netname))

    def silk(self, txt, xy, size=1.0, layer=pcbnew.F_SilkS, bold=False, rot=0):
        t = pcbnew.PCB_TEXT(self.board)
        t.SetText(txt)
        t.SetPosition(pcbnew.VECTOR2I(MM(xy[0]), MM(xy[1])))
        t.SetLayer(layer)
        t.SetTextSize(pcbnew.VECTOR2I(MM(size), MM(size)))
        t.SetTextThickness(MM(size * (0.2 if bold else 0.15)))
        if rot:
            t.SetTextAngleDegrees(rot)
        if layer == pcbnew.B_SilkS:
            t.SetMirrored(True)
        self.board.Add(t)

    def line(self, a, b, layer=pcbnew.F_SilkS, w=0.15):
        s = pcbnew.PCB_SHAPE(self.board)
        s.SetShape(pcbnew.SHAPE_T_SEGMENT)
        s.SetStart(pcbnew.VECTOR2I(MM(a[0]), MM(a[1])))
        s.SetEnd(pcbnew.VECTOR2I(MM(b[0]), MM(b[1])))
        s.SetLayer(layer)
        s.SetWidth(MM(w))
        self.board.Add(s)

    def rect(self, x0, y0, x1, y1, layer=pcbnew.F_SilkS, w=0.15):
        for a, b in (((x0, y0), (x1, y0)), ((x1, y0), (x1, y1)), ((x1, y1), (x0, y1)), ((x0, y1), (x0, y0))):
            self.line(a, b, layer, w)

    def edge(self):
        self.rect(0, 0, self.w, self.h, layer=pcbnew.Edge_Cuts, w=0.1)

    def hole(self, ref, xy):
        self.place("MountingHole", "MountingHole_3.2mm_M3", ref, xy)

    def track(self, netname, pts, width, layer=pcbnew.F_Cu, locked=True):
        for a, b in zip(pts, pts[1:]):
            t = pcbnew.PCB_TRACK(self.board)
            t.SetStart(pcbnew.VECTOR2I(MM(a[0]), MM(a[1])))
            t.SetEnd(pcbnew.VECTOR2I(MM(b[0]), MM(b[1])))
            t.SetWidth(MM(width))
            t.SetLayer(layer)
            t.SetNet(self.net(netname))
            t.SetLocked(locked)
            self.board.Add(t)

    def area(self, poly, name):
        z = pcbnew.ZONE(self.board)
        z.SetIsRuleArea(True)
        z.SetDoNotAllowTracks(True)
        z.SetDoNotAllowVias(True)
        z.SetDoNotAllowZoneFills(True)
        z.SetDoNotAllowPads(False)
        z.SetDoNotAllowFootprints(False)
        ls = pcbnew.LSET()
        ls.AddLayer(pcbnew.F_Cu)
        ls.AddLayer(pcbnew.B_Cu)
        z.SetLayerSet(ls)
        z.SetZoneName(name)
        ol = z.Outline()
        ol.NewOutline()
        for x, y in poly:
            ol.Append(MM(x), MM(y))
        self.board.Add(z)
        return z

    def rules(self, classes, dru=None, min_track=None):
        """classes: [(name, track width, clearance, via diameter, via drill, [patterns])]; default: 0.25 / 0.15 / 0.6 / 0.3.
        min_track: board minimum track width (mm), if not KiCad's default 0.2"""
        ds = self.board.GetDesignSettings()
        if min_track:
            ds.m_TrackMinWidth = MM(min_track)
        ns = ds.m_NetSettings
        d = ns.GetDefaultNetclass()
        d.SetTrackWidth(MM(0.25))
        d.SetClearance(MM(0.15))
        d.SetViaDiameter(MM(0.6))
        d.SetViaDrill(MM(0.3))
        for name, tw, clr, vd, vdr, pats in classes:
            nc = pcbnew.NETCLASS(name)
            nc.SetTrackWidth(MM(tw))
            nc.SetClearance(MM(clr))
            nc.SetViaDiameter(MM(vd))
            nc.SetViaDrill(MM(vdr))
            ns.SetNetclass(name, nc)
            for p in pats:
                ns.SetNetclassPatternAssignment(p, name)
        ns.RecomputeEffectiveNetclasses()
        ds.SetCopperLayerCount(2)
        if dru:
            open(self.out.replace(".kicad_pcb", ".kicad_dru"), "w").write(dru)

    def save(self):
        pcbnew.SaveBoard(self.out, self.board)

    # --- routing --------------------------------------------------------------------------------
    def fanout_gnd(self):
        """a short track + via from every SMD GND pad (so the fill is sure to reach it)"""
        others = list(self.board.GetPads())
        placed = []
        for p in [p for p in self.board.GetPads() if p.GetNetname() == "GND" and p.GetAttribute() == pcbnew.PAD_ATTRIB_SMD]:
            c = p.GetPosition()
            fc = p.GetParentFootprint().GetPosition()
            base = math.atan2(c.y - fc.y, c.x - fc.x)
            bb = p.GetBoundingBox()
            if max(bb.GetWidth(), bb.GetHeight()) > 1.5 * min(bb.GetWidth(), bb.GetHeight()):
                # elongated pad (e.g. QFP / SOIC lead): leave along its long axis, away from the package centre, so the
                # track does not pass in front of the neighbouring leads (radial directions do at fine pitch)
                if bb.GetWidth() > bb.GetHeight():
                    base = 0.0 if c.x >= fc.x else math.pi
                else:
                    base = math.pi / 2 if c.y >= fc.y else -math.pi / 2
            done = False
            for dist in (1.2, 1.6, 2.0, 2.6, 3.2):
                for da in (0, 30, -30, 60, -60, 90, -90, 135, -135, 180):
                    a = base + math.radians(da)
                    vx, vy = c.x + MM(dist) * math.cos(a), c.y + MM(dist) * math.sin(a)
                    pt = pcbnew.VECTOR2I(int(vx), int(vy))
                    if not (MM(1) < vx < MM(self.w - 1) and MM(1) < vy < MM(self.h - 1)):
                        continue
                    if any(o is not p and o.HitTest(pt, MM(0.3 + 0.2)) for o in others):
                        continue
                    path = [pcbnew.VECTOR2I(int(c.x + (vx - c.x) * f), int(c.y + (vy - c.y) * f)) for f in [i / 20 for i in range(1, 19)]]
                    if any(o is not p and o.GetNetname() != "GND" and o.HitTest(q, MM(0.125 + 0.17)) for o in others for q in path):
                        continue
                    if any((vx - q[0]) ** 2 + (vy - q[1]) ** 2 < MM(1.0) ** 2 for q in placed):
                        continue
                    if any(z.Outline().Contains(pt) for z in self.board.Zones() if z.GetIsRuleArea()):
                        continue
                    v = pcbnew.PCB_VIA(self.board)
                    v.SetPosition(pt)
                    v.SetWidth(MM(0.6))
                    v.SetDrill(MM(0.3))
                    v.SetNet(self.net("GND"))
                    v.SetLocked(True)
                    self.board.Add(v)
                    t = pcbnew.PCB_TRACK(self.board)
                    t.SetStart(c)
                    t.SetEnd(pt)
                    t.SetWidth(MM(0.25))
                    t.SetLayer(pcbnew.F_Cu)
                    t.SetNet(self.net("GND"))
                    t.SetLocked(True)
                    self.board.Add(t)
                    placed.append((vx, vy))
                    done = True
                    break
                if done:
                    break
            if not done:
                print("GND fan-out did not fit:", p.GetParentFootprint().GetReference(), p.GetNumber())

    def freeroute(self, jar, minutes=15):
        e = 0.6                         # temporary keepout along the board edge (Freerouting does not know the edge clearance)
        for poly in ([(0, 0), (self.w, 0), (self.w, e), (0, e)], [(0, self.h - e), (self.w, self.h - e), (self.w, self.h), (0, self.h)],
                     [(0, 0), (e, 0), (e, self.h), (0, self.h)], [(self.w - e, 0), (self.w, 0), (self.w, self.h), (self.w - e, self.h)]):
            self.tmp_areas.append(self.area(poly, "EDGE_TMP"))
        dsn, ses = self.out.replace(".kicad_pcb", ".dsn"), self.out.replace(".kicad_pcb", ".ses")
        if not pcbnew.ExportSpecctraDSN(self.board, dsn):
            raise RuntimeError("DSN export failed")
        txt = open(dsn).read()
        if not os.environ.get("ROUTE_GND"):     # by default GND is connected by the fill (ROUTE_GND=1: Freerouting routes it too,
            txt = re.sub(r"\n\s*\(net GND\n.*?\n\s*\)(?=\n)", "", txt, count=1, flags=re.S)   # so no GND pad can end up
            txt = re.sub(r"(\(class \w+[^)]*?) GND\b", r"\1", txt)                                # fenced in by other tracks)
        open(dsn, "w").write(txt)
        subprocess.run([JAVA, "-jar", jar, "--gui.enabled=false", "-de", dsn, "-do", ses, "-mp", "200",
                        "--router.max_passes=200", f"--router.job_timeout=00:{minutes:02d}:00"], check=True)
        if not pcbnew.ImportSpecctraSES(self.board, ses):
            raise RuntimeError("SES import failed")
        self.checkpoint = self.out.replace(".kicad_pcb", "_routed.kicad_pcb")
        for z in self.tmp_areas:
            self.board.Remove(z)
        self.tmp_areas = []
        for f in (dsn, ses):
            if os.path.exists(f):
                os.remove(f)
        self.dedupe()
        pcbnew.SaveBoard(self.checkpoint, self.board)      # checkpoint: RESUME=1 resumes from here

    def resume(self):
        """load the post-Freerouting checkpoint (RESUME=1)"""
        self.board = pcbnew.LoadBoard(self.out.replace(".kicad_pcb", "_routed.kicad_pcb"))
        self.nets = {}
        self.tmp_areas = []

    def dedupe(self):
        vias = []
        for v in [t for t in self.board.GetTracks() if isinstance(t, pcbnew.PCB_VIA)]:   # overlapping / too-close vias
            p = v.GetPosition()
            if any(o.GetNetCode() == v.GetNetCode() and (o.GetPosition().x - p.x) ** 2 + (o.GetPosition().y - p.y) ** 2 < MM(0.45) ** 2 for o in vias):
                self.board.Remove(v)
            else:
                vias.append(v)
        seen = set()
        for t in list(self.board.GetTracks()):
            if isinstance(t, pcbnew.PCB_VIA):
                continue
            a, b = (t.GetStart().x, t.GetStart().y), (t.GetEnd().x, t.GetEnd().y)
            key = (t.GetLayer(), t.GetNetCode(), min(a, b), max(a, b))
            if key in seen:
                self.board.Remove(t)
            seen.add(key)

    def pour_gnd(self, polys):
        for layer in (pcbnew.B_Cu, pcbnew.F_Cu):
            for poly in polys:
                z = pcbnew.ZONE(self.board)
                z.SetLayer(layer)
                z.SetNet(self.net("GND"))
                z.SetLocalClearance(MM(0.3))
                z.SetMinThickness(MM(0.25))
                z.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL)
                ol = z.Outline()
                ol.NewOutline()
                for x, y in poly:
                    ol.Append(MM(x), MM(y))
                self.board.Add(z)
        self.fill()

    def fill(self):
        pcbnew.ZONE_FILLER(self.board).Fill(self.board.Zones())

    def via_grid(self, region, step=5.0, keep_out=()):
        """GND via grid between the fills on both sides; region = (x0, y0, x1, y1)"""
        obst = list(self.board.GetPads()) + [t for t in self.board.GetTracks() if t.GetNetname() != "GND" or isinstance(t, pcbnew.PCB_VIA)]
        rules = [z for z in self.board.Zones() if z.GetIsRuleArea()]
        added = []
        y = region[1]
        while y < region[3]:
            x = region[0]
            while x < region[2]:
                pt = pcbnew.VECTOR2I(MM(x), MM(y))
                if not any(k[0] <= x <= k[2] and k[1] <= y <= k[3] for k in keep_out) and \
                        not any(z.Outline().Contains(pt) for z in rules) and \
                        not any(o.HitTest(pt, MM(0.3 + 0.4)) for o in obst):
                    v = pcbnew.PCB_VIA(self.board)
                    v.SetPosition(pt)
                    v.SetWidth(MM(0.6))
                    v.SetDrill(MM(0.3))
                    v.SetNet(self.net("GND"))
                    self.board.Add(v)
                    added.append(v)
                x += step
            y += step
        self.fill()
        for v in added:      # delete vias not touching any fill (connected to nothing)
            if not any(z.HitTestFilledArea(lay, v.GetPosition()) for z in self.board.Zones() if not z.GetIsRuleArea()
                       for lay in (pcbnew.F_Cu, pcbnew.B_Cu) if z.IsOnLayer(lay)):
                self.board.Remove(v)
        self.fill()

    def cleanup(self):
        """remove dangling stubs in separate processes (cleanup_stubs.py)"""
        self.save()
        subprocess.run([os.sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)), "cleanup_stubs.py"), self.out], check=False)

    def drc(self):
        self.save()
        rep = os.path.join(tempfile.gettempdir(), os.path.basename(self.out) + ".drc.json")
        subprocess.run([CLI, "pcb", "drc", "--format", "json", "-o", rep, self.out], check=True, capture_output=True)
        return json.load(open(rep))

    def finish(self, include_gnd=False, step=0.1, w=0.25, clr=0.15, via=0.6, margin=8.0, max_nodes=150000):
        """route the skipped connections with a grid A* path finder (both layers, with vias)"""
        d = self.drc()
        pairs = []

        def lay_of(desc):
            if desc.startswith(("Via", "PTH")):
                return (0, 1)
            return (1,) if "B.Cu" in desc else (0,)
        for u in d["unconnected_items"]:
            m = u["items"]
            nn = m[0]["description"].split("[")[1].split("]")[0] if "[" in m[0]["description"] else ""
            if not nn or nn == "GND":
                continue
            pts = [((i["pos"]["x"], i["pos"]["y"]), lay_of(i["description"])) for i in m if not i["description"].startswith(("Z\u00f3na", "Zone"))]   # first prefix: Hungarian-locale KiCad DRC output
            if len(pts) == 2:
                pairs.append((nn, pts))
        if include_gnd:
            self.board.BuildConnectivity()
            conn = self.board.GetConnectivity()
            gnd = [p for p in self.board.GetPads() if p.GetNetname() == "GND"] + [t for t in self.board.GetTracks() if t.GetNetname() == "GND"]
            vias = [t for t in gnd if isinstance(t, pcbnew.PCB_VIA)]
            if vias:
                seed = max(vias, key=lambda t: len(conn.GetConnectedItems(t)))
                main = {x.m_Uuid.AsString() for x in conn.GetConnectedItems(seed)} | {seed.m_Uuid.AsString()}
                targets = [x.GetPosition() for x in gnd if x.m_Uuid.AsString() in main and
                           (isinstance(x, pcbnew.PCB_VIA) or (isinstance(x, pcbnew.PAD) and x.GetAttribute() == pcbnew.PAD_ATTRIB_PTH))]
                done = set()
                for x in gnd:
                    uid = x.m_Uuid.AsString()
                    if uid in main or uid in done or not isinstance(x, (pcbnew.PAD, pcbnew.PCB_VIA)):
                        continue
                    grp = list(conn.GetConnectedItems(x)) + [x]
                    done |= {y.m_Uuid.AsString() for y in grp}
                    vv = [y for y in grp if isinstance(y, pcbnew.PCB_VIA)]
                    if vv:
                        x = vv[0]
                    c = x.GetPosition()
                    t = min(targets, key=lambda q: (q.x - c.x) ** 2 + (q.y - c.y) ** 2)
                    smd = isinstance(x, pcbnew.PAD) and x.GetAttribute() == pcbnew.PAD_ATTRIB_SMD
                    pairs.append(("GND", [((c.x / 1e6, c.y / 1e6), (0,) if smd else (0, 1)), ((t.x / 1e6, t.y / 1e6), (0, 1))]))
        rules = [z for z in self.board.Zones() if z.GetIsRuleArea()]
        layers = (pcbnew.F_Cu, pcbnew.B_Cu)
        ok = 0
        for nn, ((a, la), (b, lb)) in pairs:
            nc = self.net(nn).GetNetCode()
            x0, x1 = max(min(a[0], b[0]) - margin, 0.0), min(max(a[0], b[0]) + margin, self.w)
            y0, y1 = max(min(a[1], b[1]) - margin, 0.0), min(max(a[1], b[1]) + margin, self.h)
            box = pcbnew.BOX2I(pcbnew.VECTOR2I(MM(x0 - 2), MM(y0 - 2)), pcbnew.VECTOR2L(MM(x1 - x0 + 4), MM(y1 - y0 + 4)))
            items = [i for i in list(self.board.GetPads()) + list(self.board.GetTracks())
                     if i.GetNetCode() != nc and i.GetBoundingBox().Intersects(box)]
            cache = {}

            def free(ix, iy, li, r):
                key = (ix, iy, li, r)
                if key not in cache:
                    x, y = x0 + ix * step, y0 + iy * step
                    pt = pcbnew.VECTOR2I(MM(x), MM(y))
                    cache[key] = (0.5 < x < self.w - 0.5 and 0.5 < y < self.h - 0.5 and
                                  not any(z.Outline().Contains(pt) for z in rules) and
                                  not any((i.IsOnLayer(layers[li]) or (isinstance(i, pcbnew.PAD) and i.GetAttribute() == pcbnew.PAD_ATTRIB_NPTH))
                                          and i.HitTest(pt, MM(r + clr + 0.02 + (0.25 if isinstance(i, pcbnew.PAD) and
                                                                                 i.GetAttribute() == pcbnew.PAD_ATTRIB_NPTH else 0)))
                                          for i in items))
                return cache[key]
            nx, ny = int((x1 - x0) / step), int((y1 - y0) / step)
            sa = (round((a[0] - x0) / step), round((a[1] - y0) / step))
            sb = (round((b[0] - x0) / step), round((b[1] - y0) / step))
            start = [(sa[0], sa[1], li) for li in la]
            goal = {(sb[0], sb[1], li) for li in lb}
            h = lambda n: math.hypot(n[0] - sb[0], n[1] - sb[1])
            openq = [(h(n), 0.0, n, None) for n in start]
            came, g = {}, {n: 0.0 for n in start}
            found = None
            expanded = 0
            while openq and expanded < max_nodes:   # limit: if there is no path, do not search the whole grid for hours
                expanded += 1
                _, gc, n, par = heapq.heappop(openq)
                if n in came:
                    continue
                came[n] = par
                if n in goal:
                    found = n
                    break
                ix, iy, li = n
                nb = [((ix + dx, iy + dy, li), math.hypot(dx, dy)) for dx in (-1, 0, 1) for dy in (-1, 0, 1) if dx or dy]
                nb.append(((ix, iy, 1 - li), 15.0))
                for m, cst in nb:
                    if not (0 <= m[0] <= nx and 0 <= m[1] <= ny) or m in came:
                        continue
                    if m[2] != li:
                        if not (free(ix, iy, 0, via / 2) and free(ix, iy, 1, via / 2)):
                            continue
                    elif not free(m[0], m[1], m[2], w / 2):
                        continue
                    ng = gc + cst
                    if ng < g.get(m, 1e18):
                        g[m] = ng
                        heapq.heappush(openq, (ng + h(m), ng, m, n))
            if not found:
                print("A*: no path found:", nn, a, b)
                continue
            path, n = [], found
            while n is not None:
                path.append(n)
                n = came[n]
            path.reverse()

            def seg_free(p0, p1):
                k_ = max(abs(p1[0] - p0[0]), abs(p1[1] - p0[1]))
                return all(free(round(p0[0] + (p1[0] - p0[0]) * k / max(k_, 1)), round(p0[1] + (p1[1] - p0[1]) * k / max(k_, 1)),
                                p0[2], w / 2 + 0.05) for k in range(k_ + 1))
            simp, k = [path[0]], 0
            while k < len(path) - 1:
                if path[k + 1][2] != path[k][2]:
                    simp.append(path[k + 1])
                    k += 1
                    continue
                best = k + 1
                for jj in range(len(path) - 1, k + 1, -1):
                    if all(path[q][2] == path[k][2] for q in range(k, jj + 1)) and seg_free(path[k], path[jj]):
                        best = jj
                        break
                simp.append(path[best])
                k = best
            for p0, p1 in zip(simp, simp[1:]):
                P0 = pcbnew.VECTOR2I(MM(x0 + p0[0] * step), MM(y0 + p0[1] * step))
                P1 = pcbnew.VECTOR2I(MM(x0 + p1[0] * step), MM(y0 + p1[1] * step))
                if p0[2] != p1[2]:
                    v = pcbnew.PCB_VIA(self.board)
                    v.SetPosition(P0)
                    v.SetWidth(MM(via))
                    v.SetDrill(MM(0.3))
                    v.SetNet(self.net(nn))
                    self.board.Add(v)
                    continue
                t = pcbnew.PCB_TRACK(self.board)
                t.SetStart(P0)
                t.SetEnd(P1)
                t.SetWidth(MM(w))
                t.SetLayer(layers[p0[2]])
                t.SetNet(self.net(nn))
                self.board.Add(t)
            ok += 1
            print("A*: routed", nn)
        return len(pairs), ok

    # --- JLCPCB ------------------------------------------------------------------------------
    def export_jlc(self, folder):
        os.makedirs(folder, exist_ok=True)
        fps = {fp.GetReference(): fp for fp in self.board.GetFootprints()}
        refs = sorted((r for r in self.assy if r in fps), key=lambda r: (re.sub(r"\d", "", r), int(re.sub(r"\D", "", r) or 0)))
        groups = {}
        for r in refs:
            groups.setdefault(self.assy[r], []).append(r)
        with open(os.path.join(folder, "jlcpcb_bom.csv"), "w", newline="") as f:
            wr = csv.writer(f)
            wr.writerow(["Comment", "Designator", "Footprint", "LCSC Part #"])
            for (val, fpn, lcsc), rs in groups.items():
                wr.writerow([val, ",".join(rs), fpn, lcsc])
        with open(os.path.join(folder, "jlcpcb_cpl.csv"), "w", newline="") as f:
            wr = csv.writer(f)
            wr.writerow(["Designator", "Mid X", "Mid Y", "Layer", "Rotation"])
            for r in refs:
                fp = fps[r]
                name = str(fp.GetFPID().GetLibItemName())
                try:
                    if name.startswith("JUSHUO_AFA07"):
                        # JLCPCB (EasyEDA) centroid: halfway between the signal pad row and the mounting tab row
                        sig = [p_ for p_ in fp.Pads() if p_.GetNumber().isdigit()]
                        mp = [p_ for p_ in fp.Pads() if not p_.GetNumber().isdigit()]
                        cx = lambda ps, a: sum(getattr(p_.GetPosition(), a) for p_ in ps) / len(ps)
                        c = pcbnew.VECTOR2I(int((cx(sig, "x") + cx(mp, "x")) / 2), int((cx(sig, "y") + cx(mp, "y")) / 2))
                        raise StopIteration
                    if os.path.exists(os.path.join(LCSC_LIB, name + ".kicad_mod")):
                        raise ValueError     # LCSC (EasyEDA) footprint: its origin is the JLCPCB part centroid
                    c = fp.GetCourtyard(pcbnew.F_CrtYd).BBox().GetCenter()
                    if fp.GetCourtyard(pcbnew.F_CrtYd).BBox().GetWidth() == 0:
                        raise ValueError
                except StopIteration:
                    pass
                except Exception:
                    c = fp.GetPosition()
                rot = fp.GetOrientationDegrees() + sum(d for pat, d in ROT_FIX if re.match(pat, name))
                wr.writerow([r, f"{c.x / 1e6:.3f}mm", f"{-c.y / 1e6:.3f}mm", "Bottom" if fp.IsFlipped() else "Top", f"{rot % 360:.0f}"])
        print("JLCPCB BOM/CPL:", len(refs), "parts")

    def gerbers(self, folder, zipname):
        g = os.path.join(folder, "gerber")
        os.makedirs(g, exist_ok=True)
        for f in os.listdir(g):
            os.remove(os.path.join(g, f))
        subprocess.run([CLI, "pcb", "export", "gerbers", "-o", g + "/", "-l",
                        "F.Cu,B.Cu,F.Paste,B.Paste,F.Silkscreen,B.Silkscreen,F.Mask,B.Mask,Edge.Cuts", self.out], check=True, capture_output=True)
        subprocess.run([CLI, "pcb", "export", "drill", "--excellon-separate-th", "-o", g + "/", self.out], check=True, capture_output=True)
        z = os.path.join(folder, zipname)
        if os.path.exists(z):
            os.remove(z)
        subprocess.run(["zip", "-q", "-j", z] + [os.path.join(g, f) for f in sorted(os.listdir(g))], check=True)
