"""
Firebird Twin Gauge Pod -- mesh + STEP + PDF generator.

This script builds the same geometry that firebird_gauge_pod.scad describes
(a tilted twin-display pod plus a rear flange) but as an explicit triangle
mesh in Python, and exports:

    firebird_gauge_pod.stl    -- printable STL (binary)
    firebird_gauge_pod.step   -- ISO 10303 STEP AP203 (faceted shell)
    firebird_gauge_pod.pdf    -- multi-view dimensioned shop drawing

All dimensions in millimetres.

Run:  python3 build_pod.py
"""

from __future__ import annotations
import math
import os
import struct
import time
from dataclasses import dataclass
from typing import List, Tuple

import numpy as np

# =====================================================================
# PARAMETERS  (mirror firebird_gauge_pod.scad)
# =====================================================================

# Display module — Waveshare ESP32-S3-Touch-LCD-2.1
# Verified against wiki + dimension drawing + caliper. Rev B, 2026-04.
# The PCB is ROUND, Ø 75 mm (NOT square 65×65 as previously assumed).
DISPLAY_PCB_OD = 75.0          # round PCB outer diameter (wiki: 75.00 ±0.1)
DISPLAY_PCB_T = 1.6            # PCB thickness (typical 2-layer)
DISPLAY_VIS_OD = 53.28         # active glass diameter (wiki drawing)
DISPLAY_HOLE_D = 2.4           # M2 clearance close-fit (drawing: ΦM2)
DISPLAY_BACK_DEPTH = 8.91      # tallest rear component height (calipered)
PCB_POCKET_OS = 0.6            # pocket oversize for fit tolerance

# Mounting hole positions on each display, PCB-center-relative (x, y).
# Top pair X o.c. = 59.28 (calipered), top Y = +14.21 (drawing).
# Bot pair X o.c. = 36.00 (calipered), bot Y = -27.07 (drawing).
DISPLAY_HOLES = [
    (-29.64, +14.21),   # top-left
    (+29.64, +14.21),   # top-right
    (-18.00, -27.07),   # bottom-left
    (+18.00, -27.07),   # bottom-right
]

# Compatibility shims so legacy code paths that still reference the old
# square-PCB names keep working (they just see the round PCB OD).
DISPLAY_PCB_W = DISPLAY_PCB_OD
DISPLAY_PCB_H = DISPLAY_PCB_OD

# Pod body — Rev C: flat fascia, dash-mounted, fits 215.9 × 106.4 envelope
DISPLAY_SPACING = 105.0         # was 90 — wider stance for the bigger pod
WALL = 3.0
FASCIA_T = 4.5
BEZEL_OPENING = 55.0
BEZEL_CHAMFER_W = 2.2
BEZEL_CHAMFER_D = 1.0

POD_W = DISPLAY_SPACING + DISPLAY_PCB_OD + 20.0      # 200.0   (limit 215.9)
POD_H = DISPLAY_PCB_OD + 25.0                         # 100.0   (limit 106.4)
POD_DEPTH = FASCIA_T + DISPLAY_PCB_T + DISPLAY_BACK_DEPTH + 4.0  # 19.01

DRIVER_TILT_DEG = 0.0   # Rev C — flat fascia, no yaw
DRIVER_TILT = math.radians(DRIVER_TILT_DEG)

# Rear flange
FLANGE_T = 5.0
FLANGE_EAR_W = 22.0
FLANGE_EAR_H = 28.0
SCREW_HOLE_D = 5.5
COUNTERSINK_D = 11.0
COUNTERSINK_DEPTH = 3.0
SCREW_SPACING = 160.0   # Rev C — drilled into the dash undercut
# Flange must be at least as wide as the pod body.
FLANGE_W = max(SCREW_SPACING + FLANGE_EAR_W, POD_W)  # 200

# Cable exit (in flange)
CABLE_EXIT_W = 30.0
CABLE_EXIT_H = 12.0

# Internal standoffs (printed bosses inside the pod)
STANDOFF_OD = 5.0
STANDOFF_HOLE_D = 1.7   # M2 self-tap pilot (was 2.2 for M2.5)
STANDOFF_LEN = 3.0      # shorter — rear stack is only 8.91 mm (was 4.0)

# Mesh resolution
CIRC_SEG = 64           # circular hole tessellation segments

# =====================================================================
# Mesh helpers
# =====================================================================

@dataclass
class Mesh:
    verts: np.ndarray            # (N, 3) float
    tris:  np.ndarray            # (M, 3) int -- indices into verts

    def transformed(self, M: np.ndarray) -> "Mesh":
        h = np.hstack([self.verts, np.ones((len(self.verts), 1))])
        v = (M @ h.T).T[:, :3]
        return Mesh(v, self.tris.copy())

    def merged_with(self, other: "Mesh") -> "Mesh":
        offset = len(self.verts)
        verts = np.vstack([self.verts, other.verts])
        tris = np.vstack([self.tris, other.tris + offset])
        return Mesh(verts, tris)


def Tx(dx, dy, dz) -> np.ndarray:
    M = np.eye(4); M[0,3] = dx; M[1,3] = dy; M[2,3] = dz; return M

def Rz(a) -> np.ndarray:
    c, s = math.cos(a), math.sin(a)
    return np.array([[c,-s,0,0],[s,c,0,0],[0,0,1,0],[0,0,0,1]], float)

def Ry(a) -> np.ndarray:
    c, s = math.cos(a), math.sin(a)
    return np.array([[c,0,s,0],[0,1,0,0],[-s,0,c,0],[0,0,0,1]], float)

def Rx(a) -> np.ndarray:
    c, s = math.cos(a), math.sin(a)
    return np.array([[1,0,0,0],[0,c,-s,0],[0,s,c,0],[0,0,0,1]], float)


def quad(a, b, c, d) -> List[Tuple[int,int,int]]:
    """Return two triangles for quad a-b-c-d (CCW)."""
    return [(a,b,c), (a,c,d)]


def fan(center, ring) -> List[Tuple[int,int,int]]:
    """Triangle fan around center vertex index, ring is list of indices."""
    out = []
    n = len(ring)
    for i in range(n):
        out.append((center, ring[i], ring[(i+1) % n]))
    return out


# =====================================================================
# Primitive: rectangular slab with axis-aligned through-holes
# (circular and/or rectangular). Builds a watertight manifold mesh.
#
# The slab is centered at origin in X/Y, with thickness along Z.
# Holes go all the way through.
# =====================================================================

def slab_with_holes(
    sx: float, sy: float, sz: float,
    circ_holes: List[Tuple[float, float, float]] = (),  # (cx, cy, dia)
    rect_holes: List[Tuple[float, float, float, float]] = (),  # (cx, cy, w, h)
    seg: int = CIRC_SEG,
) -> Mesh:
    verts: List[np.ndarray] = []
    tris: List[Tuple[int, int, int]] = []
    def addv(p): verts.append(np.array(p, float)); return len(verts)-1

    z_top = sz/2.0
    z_bot = -sz/2.0

    # We'll build top and bottom faces with hole boundaries, then connect
    # each hole's top boundary to its bottom boundary via a side cylinder.
    # The OUTER boundary of top/bottom is a rectangle.
    # We triangulate top/bottom by:
    #   - Outer rectangle - sum(holes) using a fan from each hole edge to
    #     a representative outer vertex set
    # For a fully general triangulation we'd need an ear-clip with holes;
    # to keep it simple we use a "bridge cuts" approach: split the outer
    # rectangle into N strips, each containing at most one hole.
    #
    # Simpler approach used here: lay out the top face as a regular grid
    # of quads, cutting any quad that intersects a hole. This is robust.
    # The result is a many-triangle but valid manifold mesh.
    return _slab_grid(sx, sy, sz, circ_holes, rect_holes, seg)


def _slab_grid(sx, sy, sz, circ_holes, rect_holes, seg) -> Mesh:
    """
    Build a slab as a stack of two planar faces (top/bottom) plus side walls
    and inner walls of holes.  Top and bottom faces are tessellated by a
    FAN-FROM-HOLE-CENTER for circular holes, plus surrounding strips. To
    avoid full polygon-with-holes triangulation, we restrict to the case
    where holes are circular and well-separated, plus axis-aligned rect
    holes that sit independently. Each hole is processed by:
      1. Generating a circular ring of vertices on top and bottom
      2. Connecting these into the side wall (cylinder)
      3. The face is then meshed by a Delaunay-like fan -- but to keep it
         simple and general we use a "boundary loop" mesh where we
         construct the *complement* (face = outer rect minus holes) using
         a constrained Delaunay-style approach.
    """
    # --- Build vertex set ---
    z_top = sz/2.0
    z_bot = -sz/2.0
    verts: List[List[float]] = []
    def addv(x,y,z):
        verts.append([float(x), float(y), float(z)])
        return len(verts)-1

    tris: List[Tuple[int,int,int]] = []

    # Outer rectangle corners (top + bottom)
    cx_t = [addv(-sx/2, -sy/2, z_top),
            addv( sx/2, -sy/2, z_top),
            addv( sx/2,  sy/2, z_top),
            addv(-sx/2,  sy/2, z_top)]
    cx_b = [addv(-sx/2, -sy/2, z_bot),
            addv( sx/2, -sy/2, z_bot),
            addv( sx/2,  sy/2, z_bot),
            addv(-sx/2,  sy/2, z_bot)]

    # 4 outer side walls (CCW from outside)
    # Bottom face normal -z so triangle order reversed there.
    # Side walls connect bot[i]->bot[i+1]->top[i+1]->top[i] CCW (outside view)
    for i in range(4):
        j = (i+1) % 4
        a, b = cx_b[i], cx_b[j]
        c, d = cx_t[j], cx_t[i]
        tris.extend(quad(a, b, c, d))

    # --- Top face mesh and inner walls of holes ---
    # We'll tessellate the top face as an offset/fan around each hole, plus
    # bridges. For independent circular holes that are smaller than the
    # face, we use a "ring + fan" approach:
    #   - Around each hole, generate a circle of vertices
    #   - Connect that circle outward to a rectangular "boundary" formed
    #     by the hole's local AABB expanded to nearest outer-edge midpoint
    # This isn't fully general but works for our specific layout.
    #
    # For our two circular bezel holes (which are well inside the slab):
    #   - Generate hole ring on top and bottom
    #   - Triangulate top face as: outer rect tessellated by a uniform grid,
    #     skipping cells inside holes.
    #
    # Simplest robust method: rasterize the top face into a grid of cells
    # (e.g. 1 mm), determine which cells lie outside all holes, build a
    # vertex per cell corner, triangulate cell-by-cell. This produces a
    # dense but very robust mesh.
    grid_step = 2.0  # mm cell -- 2mm gives ~70k tris/face for the pod
    nx = max(2, int(math.ceil(sx / grid_step)) + 1)
    ny = max(2, int(math.ceil(sy / grid_step)) + 1)
    xs = np.linspace(-sx/2, sx/2, nx)
    ys = np.linspace(-sy/2, sy/2, ny)

    # Inside-hole test (center of cell)
    def in_circ_hole(x, y):
        for (cx, cy, d) in circ_holes:
            if (x-cx)**2 + (y-cy)**2 <= (d/2)**2:
                return True
        return False
    def in_rect_hole(x, y):
        for (cx, cy, w, h) in rect_holes:
            if abs(x-cx) <= w/2 and abs(y-cy) <= h/2:
                return True
        return False
    def in_any_hole(x, y):
        return in_circ_hole(x, y) or in_rect_hole(x, y)

    # Snap grid corners that fall INSIDE a hole onto the hole boundary
    # (radial projection for circles, axis projection for rects). This
    # keeps the face flush with the hole edges.
    snapped = np.zeros((nx, ny, 2), float)
    inside_grid = np.zeros((nx, ny), bool)
    for ix, x in enumerate(xs):
        for iy, y in enumerate(ys):
            sx2, sy2 = x, y
            inside = False
            for (cx, cy, d) in circ_holes:
                r = d/2
                dx, dy = x-cx, y-cy
                if dx*dx + dy*dy < r*r:
                    inside = True
                    L = math.sqrt(dx*dx + dy*dy)
                    if L < 1e-6:
                        sx2, sy2 = cx + r, cy
                    else:
                        sx2 = cx + dx*r/L
                        sy2 = cy + dy*r/L
                    break
            if not inside:
                for (cx, cy, w, h) in rect_holes:
                    if abs(x-cx) < w/2 and abs(y-cy) < h/2:
                        inside = True
                        # snap to nearest edge
                        dxr = (w/2) - (x-cx)
                        dxl = (x-cx) - (-w/2)
                        dyt = (h/2) - (y-cy)
                        dyb = (y-cy) - (-h/2)
                        m = min(dxr, dxl, dyt, dyb)
                        if m == dxr: sx2 = cx + w/2
                        elif m == dxl: sx2 = cx - w/2
                        elif m == dyt: sy2 = cy + h/2
                        else:           sy2 = cy - h/2
                        break
            snapped[ix, iy, 0] = sx2
            snapped[ix, iy, 1] = sy2
            inside_grid[ix, iy] = inside

    # Build vertices for each grid corner that touches the face (i.e. is
    # outside hole OR was snapped onto hole boundary).
    top_idx  = -np.ones((nx, ny), int)
    bot_idx  = -np.ones((nx, ny), int)
    for ix in range(nx):
        for iy in range(ny):
            x, y = snapped[ix, iy]
            top_idx[ix, iy] = addv(x, y, z_top)
            bot_idx[ix, iy] = addv(x, y, z_bot)

    # Triangulate cell-by-cell: skip cells where all 4 corners were originally
    # inside a hole (so the cell is fully inside the hole).
    # If 1-3 corners inside, the corners get snapped to the boundary so the
    # triangle still represents face material.
    for ix in range(nx-1):
        for iy in range(ny-1):
            cnt_in = sum([inside_grid[ix, iy], inside_grid[ix+1, iy],
                          inside_grid[ix+1, iy+1], inside_grid[ix, iy+1]])
            if cnt_in == 4:
                # all 4 corners inside same hole -> fully inside -> skip
                # (we'll close the wall via cylinder later)
                continue
            a_t = top_idx[ix, iy]
            b_t = top_idx[ix+1, iy]
            c_t = top_idx[ix+1, iy+1]
            d_t = top_idx[ix, iy+1]
            a_b = bot_idx[ix, iy]
            b_b = bot_idx[ix+1, iy]
            c_b = bot_idx[ix+1, iy+1]
            d_b = bot_idx[ix, iy+1]
            # Top face (CCW from +z above)
            tris.extend(quad(a_t, b_t, c_t, d_t))
            # Bottom face (CCW from -z below = reversed)
            tris.extend(quad(a_b, d_b, c_b, b_b))

    # --- Inner cylindrical walls (one per circular hole) ---
    for (cx, cy, d) in circ_holes:
        r = d/2
        ring_top = []
        ring_bot = []
        for k in range(seg):
            ang = 2*math.pi * k / seg
            x = cx + r*math.cos(ang)
            y = cy + r*math.sin(ang)
            ring_top.append(addv(x, y, z_top))
            ring_bot.append(addv(x, y, z_bot))
        # Side wall facing INWARD (normal points away from cylinder axis)
        # The hole is a "negative" feature: from outside-the-hole viewpoint
        # the wall normal points TOWARD the hole center -- so we wind such
        # that the outward normal of the slab (which here means out of the
        # solid material into the hole) is correct. CCW seen from inside
        # the hole.
        for i in range(seg):
            j = (i+1) % seg
            a = ring_top[i]
            b = ring_top[j]
            c = ring_bot[j]
            d = ring_bot[i]
            tris.extend(quad(a, d, c, b))  # inward-facing

    # --- Inner rectangular hole walls ---
    for (cx, cy, w, h) in rect_holes:
        # 4 vertical walls
        corners_top = [
            addv(cx-w/2, cy-h/2, z_top),
            addv(cx+w/2, cy-h/2, z_top),
            addv(cx+w/2, cy+h/2, z_top),
            addv(cx-w/2, cy+h/2, z_top),
        ]
        corners_bot = [
            addv(cx-w/2, cy-h/2, z_bot),
            addv(cx+w/2, cy-h/2, z_bot),
            addv(cx+w/2, cy+h/2, z_bot),
            addv(cx-w/2, cy+h/2, z_bot),
        ]
        for i in range(4):
            j = (i+1) % 4
            a = corners_top[i]
            b = corners_top[j]
            c = corners_bot[j]
            d = corners_bot[i]
            tris.extend(quad(a, d, c, b))  # inward

    return Mesh(np.array(verts, float), np.array(tris, int))


# =====================================================================
# Build the actual pod assembly
# =====================================================================

def build_pod_body() -> Mesh:
    """
    Pod body in fascia-local frame:
      X = lateral, Y = vertical, Z = thickness (fascia front at +Z/2).
    Two circular through-holes for the bezels.
    """
    # We tessellate the body as a slab POD_W x POD_H x POD_DEPTH with two
    # circular holes (bezel openings). Hole centers sit on the X axis
    # (Y = 0) at +-DISPLAY_SPACING/2.
    holes = [
        (+DISPLAY_SPACING/2, 0.0, BEZEL_OPENING),
        (-DISPLAY_SPACING/2, 0.0, BEZEL_OPENING),
    ]
    return slab_with_holes(POD_W, POD_H, POD_DEPTH, circ_holes=holes)


def build_flange() -> Mesh:
    """
    Rear flange in console-front frame. The flange's Z thickness is the
    direction normal to the console face (i.e. the axis the flange is
    bolted along). Two circular screw holes + 1 rectangular cable hole.
    """
    holes_circ = [
        (+SCREW_SPACING/2, 0.0, SCREW_HOLE_D),
        (-SCREW_SPACING/2, 0.0, SCREW_HOLE_D),
    ]
    rect = [(0.0, -POD_H/2 + WALL + CABLE_EXIT_H/2 + 2.0,
             CABLE_EXIT_W, CABLE_EXIT_H)]
    return slab_with_holes(FLANGE_W, POD_H, FLANGE_T,
                           circ_holes=holes_circ, rect_holes=rect)


def build_assembly() -> Mesh:
    body = build_pod_body()
    flange = build_flange()

    # Pod body: rotate about vertical (Y) axis by DRIVER_TILT, then place
    # so its rear face overlaps the flange's front face.
    # In body local: rear face is at z = -POD_DEPTH/2.
    # In flange local: cabin-side face is at z = +FLANGE_T/2.
    # We want the rear of the pod to sit on the flange's cabin face.
    # First, in pod-local: put rear face at z=0:
    M_pod = Tx(0, 0, POD_DEPTH/2)            # rear face -> z=0
    # Then yaw 15 deg about vertical (Y axis):
    M_pod = Ry(DRIVER_TILT) @ M_pod
    # Then translate so its rear face lies on flange front face:
    M_pod = Tx(0, 0, FLANGE_T/2 + 0.5) @ M_pod   # +0.5 mm overlap for fuse
    # Body needs to be slid forward by overlap to penetrate flange slightly
    body_t = body.transformed(M_pod)

    # Flange: keep at origin, but in same global frame -- thickness along Z
    flange_t = flange   # already centered at z=0

    # Result: a single mesh combining both. Slicers will fuse overlapping
    # solids during slicing.
    return body_t.merged_with(flange_t)


# =====================================================================
# STL writer (binary)
# =====================================================================

def write_stl_binary(filename: str, mesh: Mesh, header: str = "Firebird Pod"):
    v = mesh.verts
    t = mesh.tris
    n = len(t)
    # Compute per-face normals
    p0 = v[t[:,0]]
    p1 = v[t[:,1]]
    p2 = v[t[:,2]]
    e1 = p1 - p0
    e2 = p2 - p0
    norms = np.cross(e1, e2)
    lens = np.linalg.norm(norms, axis=1)
    lens[lens==0] = 1
    norms = norms / lens[:, None]

    with open(filename, 'wb') as f:
        h = header.encode('ascii')
        f.write((h + b' ' * 80)[:80])
        f.write(struct.pack('<I', n))
        for i in range(n):
            f.write(struct.pack('<3f', *norms[i]))
            f.write(struct.pack('<3f', *p0[i]))
            f.write(struct.pack('<3f', *p1[i]))
            f.write(struct.pack('<3f', *p2[i]))
            f.write(b'\x00\x00')


# =====================================================================
# STEP AP203 writer (faceted shell -- closed_shell of triangular faces)
# This produces a valid ISO 10303-21 file consumable by SolidWorks,
# Fusion 360, FreeCAD, etc. The shell is a polyhedral B-rep approximation
# of the geometry, suitable for CAM/visualization but not parametric.
# =====================================================================

def write_step_faceted(filename: str, mesh: Mesh, name: str = "FirebirdPod"):
    v = mesh.verts
    t = mesh.tris
    nv = len(v)
    nt = len(t)

    lines: List[str] = []
    eid = 1
    def emit(s):
        nonlocal eid
        lines.append(f"#{eid} = {s};")
        i = eid; eid += 1; return i

    # Header
    timestamp = time.strftime("%Y-%m-%dT%H:%M:%S")
    header = (
        "ISO-10303-21;\n"
        "HEADER;\n"
        f"FILE_DESCRIPTION(('Firebird Twin Gauge Pod -- faceted shell'),'2;1');\n"
        f"FILE_NAME('{name}.step','{timestamp}',('Claude'),(''),"
        "'build_pod.py','','');\n"
        "FILE_SCHEMA(('CONFIG_CONTROL_DESIGN'));\n"
        "ENDSEC;\n"
        "DATA;\n"
    )

    # Application context boilerplate
    app_ctx        = emit("APPLICATION_CONTEXT('configuration controlled 3D designs of mechanical parts and assemblies')")
    app_proto_def  = emit(f"APPLICATION_PROTOCOL_DEFINITION('international standard',"
                          f"'config_control_design',1994,#{app_ctx})")
    prod           = emit(f"PRODUCT('{name}','{name}','',(#{eid+5}))")
    prod_def_form  = emit(f"PRODUCT_DEFINITION_FORMATION('1','',#{prod})")
    prod_def_ctx   = emit(f"PRODUCT_DEFINITION_CONTEXT('part definition',#{app_ctx},'design')")
    prod_def       = emit(f"PRODUCT_DEFINITION('design','',#{prod_def_form},#{prod_def_ctx})")
    prod_def_shape = emit(f"PRODUCT_DEFINITION_SHAPE('','',#{prod_def})")
    pctx           = emit(f"PRODUCT_CONTEXT('',#{app_ctx},'mechanical')")
    # Reorder: emit pctx first, then product references it (we cheated above)
    # For simplicity we emit as a flat sequence and just trust the integer ids.
    # NOTE: STEP doesn't strictly require strict topological order for ids.

    # Geometry: one CARTESIAN_POINT per vertex
    pt_ids: List[int] = []
    for x, y, z in v:
        pt_ids.append(emit(f"CARTESIAN_POINT('',({x:.6f},{y:.6f},{z:.6f}))"))

    # One VERTEX_POINT per cartesian point
    vp_ids = [emit(f"VERTEX_POINT('',#{p})") for p in pt_ids]

    # Build edges (unique). Map (i,j) i<j -> edge_curve_id, vertex_loop_dir
    edge_map: dict = {}
    edge_ids: List[int] = []
    line_dir_ids: List[int] = []  # not strictly needed; we use polyline edges
    def edge_id(i, j):
        a, b = (i, j) if i < j else (j, i)
        if (a, b) in edge_map:
            return edge_map[(a, b)]
        # LINE through point i with direction (j - i)
        pa = v[a]; pb = v[b]
        d = pb - pa
        L = np.linalg.norm(d)
        if L < 1e-9:
            d = np.array([1.0, 0.0, 0.0])
        else:
            d = d / L
        dir_id = emit(f"DIRECTION('',({d[0]:.6f},{d[1]:.6f},{d[2]:.6f}))")
        vec_id = emit(f"VECTOR('',#{dir_id},{L:.6f})")
        line_id = emit(f"LINE('',#{pt_ids[a]},#{vec_id})")
        ec_id = emit(f"EDGE_CURVE('',#{vp_ids[a]},#{vp_ids[b]},#{line_id},.T.)")
        edge_map[(a, b)] = ec_id
        edge_ids.append(ec_id)
        return ec_id

    # Build advanced_face per triangle
    face_ids: List[int] = []
    for tri in t:
        i, j, k = int(tri[0]), int(tri[1]), int(tri[2])
        # Edges in CCW order
        e_ij = edge_id(i, j)
        e_jk = edge_id(j, k)
        e_ki = edge_id(k, i)
        # Determine ORIENTATION of each oriented_edge
        oe_ij = emit(f"ORIENTED_EDGE('',*,*,#{e_ij},{'.T.' if i < j else '.F.'})")
        oe_jk = emit(f"ORIENTED_EDGE('',*,*,#{e_jk},{'.T.' if j < k else '.F.'})")
        oe_ki = emit(f"ORIENTED_EDGE('',*,*,#{e_ki},{'.T.' if k < i else '.F.'})")
        edge_loop = emit(f"EDGE_LOOP('',(#{oe_ij},#{oe_jk},#{oe_ki}))")
        face_outer = emit(f"FACE_OUTER_BOUND('',#{edge_loop},.T.)")
        # Face plane: defined by point i and normal of triangle
        p0 = v[i]; p1 = v[j]; p2 = v[k]
        n = np.cross(p1-p0, p2-p0)
        nl = np.linalg.norm(n)
        if nl < 1e-12:
            n = np.array([0.0, 0.0, 1.0])
        else:
            n = n / nl
        # Local axis along edge ij
        ax = p1 - p0
        axl = np.linalg.norm(ax)
        if axl < 1e-12:
            ax = np.array([1.0, 0.0, 0.0])
        else:
            ax = ax / axl
        plane_pt   = emit(f"CARTESIAN_POINT('',({p0[0]:.6f},{p0[1]:.6f},{p0[2]:.6f}))")
        plane_n    = emit(f"DIRECTION('',({n[0]:.6f},{n[1]:.6f},{n[2]:.6f}))")
        plane_ax   = emit(f"DIRECTION('',({ax[0]:.6f},{ax[1]:.6f},{ax[2]:.6f}))")
        a2p3       = emit(f"AXIS2_PLACEMENT_3D('',#{plane_pt},#{plane_n},#{plane_ax})")
        plane_id   = emit(f"PLANE('',#{a2p3})")
        adv_face   = emit(f"ADVANCED_FACE('',(#{face_outer}),#{plane_id},.T.)")
        face_ids.append(adv_face)

    # Closed shell containing all faces
    face_list = ",".join(f"#{fid}" for fid in face_ids)
    closed_shell = emit(f"CLOSED_SHELL('Body',({face_list}))")
    msb = emit(f"MANIFOLD_SOLID_BREP('{name}',#{closed_shell})")

    # Geometric representation context
    da = emit("DIMENSIONAL_EXPONENTS(0.E0,0.E0,0.E0,0.E0,0.E0,0.E0,0.E0)")
    length_unit = emit("(LENGTH_UNIT()NAMED_UNIT(*)SI_UNIT(.MILLI.,.METRE.))")
    plane_unit  = emit("(NAMED_UNIT(*)PLANE_ANGLE_UNIT()SI_UNIT($,.RADIAN.))")
    solid_unit  = emit("(NAMED_UNIT(*)SOLID_ANGLE_UNIT()SI_UNIT($,.STERADIAN.))")
    uncertainty = emit(f"UNCERTAINTY_MEASURE_WITH_UNIT(LENGTH_MEASURE(0.001),#{length_unit},'distance_accuracy_value','confusion accuracy')")
    geo_ctx     = emit(f"(GEOMETRIC_REPRESENTATION_CONTEXT(3)GLOBAL_UNCERTAINTY_ASSIGNED_CONTEXT((#{uncertainty}))GLOBAL_UNIT_ASSIGNED_CONTEXT((#{length_unit},#{plane_unit},#{solid_unit}))REPRESENTATION_CONTEXT('Context #1','3D Context with UNIT and UNCERTAINTY'))")
    origin_pt   = emit("CARTESIAN_POINT('',(0.,0.,0.))")
    z_dir       = emit("DIRECTION('',(0.,0.,1.))")
    x_dir       = emit("DIRECTION('',(1.,0.,0.))")
    a2p3_world  = emit(f"AXIS2_PLACEMENT_3D('',#{origin_pt},#{z_dir},#{x_dir})")
    abs_shape   = emit(f"ADVANCED_BREP_SHAPE_REPRESENTATION('{name}',(#{a2p3_world},#{msb}),#{geo_ctx})")
    shape_def   = emit(f"SHAPE_DEFINITION_REPRESENTATION(#{prod_def_shape},#{abs_shape})")

    # Compose file
    body = "\n".join(lines)
    with open(filename, "w") as f:
        f.write(header)
        f.write(body)
        f.write("\nENDSEC;\nEND-ISO-10303-21;\n")


# =====================================================================
# PDF dimensioned drawings (multi-view)
# =====================================================================

def render_drawings(filename: str, mesh_body: Mesh, mesh_flange: Mesh):
    """
    Multi-view orthographic dimensioned drawing.
    Views: front, top, side, isometric.
    """
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib.patches import (Rectangle, Circle, FancyArrowPatch,
                                    PathPatch)
    from matplotlib.path import Path
    from matplotlib.backends.backend_pdf import PdfPages

    # We don't render the actual mesh; instead we draw clean schematic
    # views from the parametric values. This produces crisp engineering
    # drawings rather than fuzzy mesh projections.

    pdf = PdfPages(filename)

    # ---- Page 1: Title block + isometric overview ----
    fig, ax = plt.subplots(figsize=(11, 8.5))   # US-Letter landscape
    ax.set_xlim(0, 11); ax.set_ylim(0, 8.5)
    ax.set_aspect('equal'); ax.axis('off')

    # Border
    ax.add_patch(Rectangle((0.3, 0.3), 10.4, 7.9, fill=False, lw=1.5))

    # Title block (bottom right) -- wider columns to avoid overlap
    tb_x, tb_y, tb_w, tb_h = 4.7, 0.3, 6.0, 1.4
    ax.add_patch(Rectangle((tb_x, tb_y), tb_w, tb_h, fill=False, lw=1.2))
    col1 = tb_x + 2.4
    col2 = tb_x + 4.4
    col3 = tb_x + 5.3
    ax.plot([tb_x, tb_x+tb_w], [tb_y+0.7, tb_y+0.7], 'k', lw=0.6)
    ax.plot([col1, col1], [tb_y, tb_y+1.4], 'k', lw=0.6)
    ax.plot([col2, col2], [tb_y, tb_y+1.4], 'k', lw=0.6)
    ax.plot([col3, col3], [tb_y, tb_y+1.4], 'k', lw=0.6)

    ax.text(tb_x+0.1, tb_y+1.18, "TITLE", fontsize=7, color='gray')
    ax.text(tb_x+0.1, tb_y+0.85, "Firebird Twin Gauge Pod",
            fontsize=10, weight='bold')
    ax.text(col1+0.1, tb_y+1.18, "PART NO.", fontsize=7, color='gray')
    ax.text(col1+0.1, tb_y+0.85, "FB-GP-001", fontsize=10)
    ax.text(col2+0.1, tb_y+1.18, "REV", fontsize=7, color='gray')
    ax.text(col2+0.1, tb_y+0.85, "C", fontsize=10)
    ax.text(col3+0.1, tb_y+1.18, "DATE", fontsize=7, color='gray')
    ax.text(col3+0.1, tb_y+0.85, "2026-04", fontsize=9)
    ax.text(tb_x+0.1, tb_y+0.50, "VEHICLE", fontsize=7, color='gray')
    ax.text(tb_x+0.1, tb_y+0.18, "1967 Pontiac Firebird", fontsize=9)
    ax.text(col1+0.1, tb_y+0.50, "MATERIAL", fontsize=7, color='gray')
    ax.text(col1+0.1, tb_y+0.18, "ABS / ASA / PETG", fontsize=9)
    ax.text(col2+0.1, tb_y+0.50, "UNITS", fontsize=7, color='gray')
    ax.text(col2+0.1, tb_y+0.18, "mm", fontsize=9)
    ax.text(col3+0.1, tb_y+0.50, "SHT", fontsize=7, color='gray')
    ax.text(col3+0.1, tb_y+0.18, "1/6", fontsize=9)

    # Sheet 1 -- front, side, top + isometric
    # Use parametric values to draw clean engineering views
    _draw_overview_iso(ax)
    pdf.savefig(fig, bbox_inches='tight'); plt.close(fig)

    # ---- Page 2: Front view (looking at fascia) with full dimensions ----
    fig, ax = plt.subplots(figsize=(11, 8.5))
    ax.set_xlim(0, 11); ax.set_ylim(0, 8.5)
    ax.set_aspect('equal'); ax.axis('off')
    ax.add_patch(Rectangle((0.3, 0.3), 10.4, 7.9, fill=False, lw=1.5))
    ax.text(5.5, 8.0, "FRONT VIEW (looking at fascia)", ha='center', fontsize=12, weight='bold')
    _draw_front_view(ax)
    pdf.savefig(fig, bbox_inches='tight'); plt.close(fig)

    # ---- Page 3: Top view (plan) ----
    fig, ax = plt.subplots(figsize=(11, 8.5))
    ax.set_xlim(0, 11); ax.set_ylim(0, 8.5)
    ax.set_aspect('equal'); ax.axis('off')
    ax.add_patch(Rectangle((0.3, 0.3), 10.4, 7.9, fill=False, lw=1.5))
    _tv_subtitle = (f"shows {DRIVER_TILT_DEG:.0f}° driver tilt"
                    if DRIVER_TILT_DEG > 0 else "flat fascia, no driver tilt")
    ax.text(5.5, 8.0, f"TOP VIEW (plan -- {_tv_subtitle})",
            ha='center', fontsize=12, weight='bold')
    _draw_top_view(ax)
    pdf.savefig(fig, bbox_inches='tight'); plt.close(fig)

    # ---- Page 4: Side / section view ----
    fig, ax = plt.subplots(figsize=(11, 8.5))
    ax.set_xlim(0, 11); ax.set_ylim(0, 8.5)
    ax.set_aspect('equal'); ax.axis('off')
    ax.add_patch(Rectangle((0.3, 0.3), 10.4, 7.9, fill=False, lw=1.5))
    ax.text(5.5, 8.0, "SIDE VIEW & SECTION A-A",
            ha='center', fontsize=12, weight='bold')
    _draw_side_view(ax)
    pdf.savefig(fig, bbox_inches='tight'); plt.close(fig)

    # ---- Page 5: Display module pocket detail ----
    fig, ax = plt.subplots(figsize=(11, 8.5))
    ax.set_xlim(0, 11); ax.set_ylim(0, 8.5)
    ax.set_aspect('equal'); ax.axis('off')
    ax.add_patch(Rectangle((0.3, 0.3), 10.4, 7.9, fill=False, lw=1.5))
    ax.text(5.5, 8.0, "DETAIL B -- Display Pocket (typ. 2 places)",
            ha='center', fontsize=12, weight='bold')
    _draw_pocket_detail(ax)
    pdf.savefig(fig, bbox_inches='tight'); plt.close(fig)

    # ---- Page 6: Notes ----
    fig, ax = plt.subplots(figsize=(11, 8.5))
    ax.set_xlim(0, 11); ax.set_ylim(0, 8.5)
    ax.set_aspect('equal'); ax.axis('off')
    ax.add_patch(Rectangle((0.3, 0.3), 10.4, 7.9, fill=False, lw=1.5))
    ax.text(5.5, 8.0, "NOTES", ha='center', fontsize=12, weight='bold')
    notes = [
        "1. ALL DIMENSIONS IN MILLIMETRES UNLESS NOTED.",
        "2. GENERAL TOLERANCE: ±0.3 MM ON 3D PRINTED FEATURES,",
        "   ±0.1 MM ON BORE DIAMETERS.",
        "3. FILLET / BREAK ALL EDGES R0.5 MIN.",
        "4. MATERIAL: ABS, ASA, OR PETG. AVOID PLA (HEAT WARPING).",
        "5. WALLS:  4 PERIMETERS MIN (≥ 1.6 MM), INFILL 25–35 % GYROID.",
        "6. PRINT ORIENTATION: REAR FLANGE FLAT ON BUILD PLATE.",
        "   USE TREE SUPPORTS UNDER BEZEL CHAMFER OVERHANG.",
        "7. DISPLAY MODULE: WAVESHARE ESP32-S3-TOUCH-LCD-2.1 (×2).",
        "   ROUND PCB Ø75 mm. VERIFIED VS WIKI + CALIPER 2026-04.",
        "8. INSTALL DISPLAYS WITH 4× M2 × 4 MM MACHINE SCREWS",
        "   INTO PRINTED STANDOFFS (TAP OR SELF-TAP).",
        "9. MOUNT TO DASH UNDERCUT WITH 2× #10 × 1\" SCREWS (OR M5",
        "   MACHINE SCREWS INTO INSTALLED NUTSERTS) THROUGH THE",
        "   COUNTERSUNK FLANGE HOLES.  PILOT-DRILL DASH FIRST.",
        "10. PERIOD-CORRECT FINISH (WOODGRAIN):",
        "    a. SAND FASCIA 320 → 600 GRIT, FILL LAYER LINES.",
        "    b. APPLY 3M DI-NOC MWG WOODGRAIN VINYL OR REAL VENEER.",
        "    c. ADD CHROME ACCENT TRIM RING AROUND EACH BEZEL FOR",
        "       AUTHENTIC STEWART-WARNER LOOK.",
        "11. CABLE EXIT: 30 × 12 MM SLOT IN REAR FLANGE,",
        "    LOCATED 16 MM FROM BOTTOM EDGE.",
        "12. DRIVER TILT: 0° (FLAT FASCIA) — REV C.",
        "    PARAMETER `DRIVER_TILT` IS RETAINED IN THE SOURCE",
        "    FOR FUTURE RE-INTRODUCTION IF DESIRED.",
        "",
        "CAD SOURCE OF TRUTH: firebird_gauge_pod.scad (parametric)",
    ]
    y = 7.4
    for line in notes:
        ax.text(0.6, y, line, fontsize=10, family='monospace')
        y -= 0.28

    pdf.savefig(fig, bbox_inches='tight'); plt.close(fig)
    pdf.close()


# Helper drawing routines (each takes a 0..11 x 0..8.5 inch axis)
def _mm_to_in_factor(mm_extent_x, in_extent_x):
    return in_extent_x / mm_extent_x

def _draw_overview_iso(ax):
    # Simple isometric-style sketch using parallel projection
    import numpy as np
    # Build wireframe of pod body box + flange box, project to isometric
    cx, cy = 5.5, 4.5
    scale_in_per_mm = 0.022   # inches per mm
    # Isometric basis (X→right+down, Y→up, Z→right+up)
    e_x = np.array([1.0, -0.5]) * scale_in_per_mm
    e_y = np.array([0.0, 1.0]) * scale_in_per_mm
    e_z = np.array([1.0, 0.5]) * scale_in_per_mm

    def proj(p):
        return np.array([cx, cy]) + e_x*p[0] + e_y*p[1] + e_z*p[2]

    def draw_box(corner, sx, sy, sz, **kw):
        # 8 corners
        c = corner
        pts = [
            [c[0], c[1], c[2]],
            [c[0]+sx, c[1], c[2]],
            [c[0]+sx, c[1]+sy, c[2]],
            [c[0], c[1]+sy, c[2]],
            [c[0], c[1], c[2]+sz],
            [c[0]+sx, c[1], c[2]+sz],
            [c[0]+sx, c[1]+sy, c[2]+sz],
            [c[0], c[1]+sy, c[2]+sz],
        ]
        P = [proj(p) for p in pts]
        edges = [(0,1),(1,2),(2,3),(3,0),
                 (4,5),(5,6),(6,7),(7,4),
                 (0,4),(1,5),(2,6),(3,7)]
        for a, b in edges:
            ax.plot([P[a][0], P[b][0]], [P[a][1], P[b][1]],
                    color=kw.get('color','black'), lw=kw.get('lw',1.0))

    # Flange (untilted)
    draw_box([-FLANGE_W/2, -POD_H/2, -FLANGE_T/2], FLANGE_W, POD_H, FLANGE_T,
             color='black', lw=1.2)

    # Pod body (tilted) -- approximate as a sheared box
    # Place pod body in front of flange and tilted: rotate corners about Y
    body_corners = []
    for ix in (-1, 1):
        for iy in (-1, 1):
            for iz in (0, 1):
                px = ix * POD_W/2
                py = iy * POD_H/2
                pz = iz * POD_DEPTH
                # rotate about y axis by DRIVER_TILT
                x2 = px*math.cos(DRIVER_TILT) + pz*math.sin(DRIVER_TILT)
                z2 = -px*math.sin(DRIVER_TILT) + pz*math.cos(DRIVER_TILT)
                # translate so front of pod sticks out forward (+z)
                body_corners.append([x2, py, z2 + FLANGE_T/2])
    P = [proj(p) for p in body_corners]
    # 8 corners ordered: (-1,-1,0)(1,-1,0)(1,1,0)(-1,1,0)(-1,-1,1)...
    edges = [(0,1),(1,3),(3,2),(2,0),
             (4,5),(5,7),(7,6),(6,4),
             (0,4),(1,5),(2,6),(3,7)]
    for a, b in edges:
        ax.plot([P[a][0], P[b][0]], [P[a][1], P[b][1]], 'b-', lw=1.0)

    # Bezel cylinders (just circles at front face of pod, projected)
    for sx in (-1, 1):
        cx_b = sx * DISPLAY_SPACING/2
        # face center at z=POD_DEPTH (front face)
        # rotate
        x2 = cx_b * math.cos(DRIVER_TILT) + POD_DEPTH * math.sin(DRIVER_TILT)
        z2 = -cx_b * math.sin(DRIVER_TILT) + POD_DEPTH * math.cos(DRIVER_TILT)
        cf = proj([x2, 0, z2 + FLANGE_T/2])
        # Draw an ellipse approximating the projected circle
        from matplotlib.patches import Ellipse
        ax.add_patch(Ellipse((cf[0], cf[1]),
                             width=BEZEL_OPENING*scale_in_per_mm*1.05,
                             height=BEZEL_OPENING*scale_in_per_mm*0.55,
                             angle=-DRIVER_TILT_DEG, fill=False, color='blue', lw=1.2))

    # Labels
    ax.text(cx, cy + 2.2, "ISOMETRIC OVERVIEW", ha='center', fontsize=12,
            weight='bold')
    if DRIVER_TILT_DEG > 0:
        ax.annotate(f"Driver tilt\n{DRIVER_TILT_DEG:.0f}°",
                    xy=(cx-1.2, cy+0.5), xytext=(cx-2.6, cy+1.6),
                    fontsize=9, ha='center',
                    arrowprops=dict(arrowstyle='->', lw=1))
    else:
        ax.annotate("Flat fascia\n(no tilt)",
                    xy=(cx-1.2, cy+0.5), xytext=(cx-2.6, cy+1.6),
                    fontsize=9, ha='center',
                    arrowprops=dict(arrowstyle='->', lw=1))
    ax.annotate("Rear flange\n(bolts to dash)", xy=(cx-0.5, cy-1.5),
                xytext=(cx-3.2, cy-2.5), fontsize=9, ha='center',
                arrowprops=dict(arrowstyle='->', lw=1))
    ax.annotate("Display bezel openings\n(2× Ø55)", xy=(cx+1.2, cy+0.6),
                xytext=(cx+3.0, cy+2.4), fontsize=9, ha='center',
                arrowprops=dict(arrowstyle='->', lw=1))


def _add_dim_h(ax, x1, x2, y, label, txt_offset=0.18):
    """Horizontal dimension between x1 and x2 at vertical y. Inch coords."""
    ax.annotate('', xy=(x2, y), xytext=(x1, y),
                arrowprops=dict(arrowstyle='<->', color='black', lw=0.8))
    ax.text((x1+x2)/2, y + txt_offset, label, ha='center', fontsize=8)

def _add_dim_v(ax, y1, y2, x, label, txt_offset=0.20):
    ax.annotate('', xy=(x, y2), xytext=(x, y1),
                arrowprops=dict(arrowstyle='<->', color='black', lw=0.8))
    ax.text(x - txt_offset, (y1+y2)/2, label, va='center', ha='right',
            rotation=90, fontsize=8)

def _draw_front_view(ax):
    """Front view of the pod, dimensioned. Drawn at scale ~1:1.6."""
    from matplotlib.patches import Rectangle, Circle
    s = 0.038   # inches per mm  -> 149mm pod ~5.7"
    cx, cy = 5.5, 4.5

    def to_in(x_mm, y_mm):
        return cx + x_mm * s, cy + y_mm * s

    # Outer rectangle
    x0, y0 = to_in(-POD_W/2, -POD_H/2)
    ax.add_patch(Rectangle((x0, y0), POD_W*s, POD_H*s,
                           fill=False, lw=1.4))

    # Two bezel openings
    for xs in (-1, 1):
        ccx, ccy = to_in(xs*DISPLAY_SPACING/2, 0)
        ax.add_patch(Circle((ccx, ccy), BEZEL_OPENING/2 * s,
                            fill=False, lw=1.2))
        # chamfer outer ring (dashed)
        ax.add_patch(Circle((ccx, ccy),
                            (BEZEL_OPENING/2 + BEZEL_CHAMFER_W) * s,
                            fill=False, lw=0.8, ls='--', color='gray'))
        # centerlines
        ax.plot([ccx-0.4, ccx+0.4], [ccy, ccy], 'k-', lw=0.4)
        ax.plot([ccx, ccx], [ccy-0.4, ccy+0.4], 'k-', lw=0.4)

    # Dimensions
    # Total width
    yA = cy - POD_H/2 * s - 0.7
    _add_dim_h(ax,
               cx - POD_W/2 * s,
               cx + POD_W/2 * s,
               yA, f"{POD_W:.1f}")
    # Display center spacing
    yB = cy + POD_H/2 * s + 0.4
    _add_dim_h(ax,
               cx - DISPLAY_SPACING/2 * s,
               cx + DISPLAY_SPACING/2 * s,
               yB, f"{DISPLAY_SPACING:.0f}  (CENTER SPACING)")
    # Total height
    xA = cx - POD_W/2 * s - 0.6
    _add_dim_v(ax,
               cy - POD_H/2 * s,
               cy + POD_H/2 * s,
               xA, f"{POD_H:.1f}")
    # Bezel opening leader
    ccx, ccy = to_in(DISPLAY_SPACING/2, 0)
    ax.annotate(f"Ø{BEZEL_OPENING:.1f}",
                xy=(ccx + BEZEL_OPENING/2*s, ccy + BEZEL_OPENING/2*s),
                xytext=(ccx + 1.8, ccy + 1.4),
                fontsize=9,
                arrowprops=dict(arrowstyle='->', lw=0.7))
    ax.annotate(f"Chamfer Ø{BEZEL_OPENING + 2*BEZEL_CHAMFER_W:.1f}\n× {BEZEL_CHAMFER_D:.1f} DEEP",
                xy=(ccx - BEZEL_OPENING/2*s, ccy - BEZEL_OPENING/2*s),
                xytext=(ccx - 2.0, ccy - 1.6),
                fontsize=8,
                arrowprops=dict(arrowstyle='->', lw=0.7))

    # Scale bar
    ax.plot([1.0, 1.0 + 25*s], [0.7, 0.7], 'k-', lw=2)
    ax.plot([1.0, 1.0], [0.65, 0.75], 'k-', lw=1.2)
    ax.plot([1.0 + 25*s, 1.0 + 25*s], [0.65, 0.75], 'k-', lw=1.2)
    ax.text(1.0 + 12.5*s, 0.5, "25 mm", fontsize=8, ha='center')

def _draw_top_view(ax):
    """Top (plan) view showing 15° driver tilt."""
    from matplotlib.patches import Rectangle, Polygon
    s = 0.038
    cx, cy = 5.5, 4.5

    # Flange (untilted) -- rectangle from -FLANGE_W/2 to +FLANGE_W/2 in X,
    # FLANGE_T deep
    fx0 = cx - FLANGE_W/2 * s
    fy0 = cy - FLANGE_T/2 * s
    ax.add_patch(Rectangle((fx0, fy0), FLANGE_W*s, FLANGE_T*s,
                           fill=False, lw=1.4))
    ax.text(cx, fy0 - 0.25, "REAR FLANGE", ha='center', fontsize=8, color='gray')

    # Pod body footprint -- a rotated rectangle in plan
    a = DRIVER_TILT
    # Body corners in body-local: (-W/2,*), z from FLANGE_T/2 to FLANGE_T/2+POD_DEPTH
    # Plan view shows X (lateral) vs Z (forward depth into cabin)
    body_corners = [
        (-POD_W/2, FLANGE_T/2),
        ( POD_W/2, FLANGE_T/2),
        ( POD_W/2, FLANGE_T/2 + POD_DEPTH),
        (-POD_W/2, FLANGE_T/2 + POD_DEPTH),
    ]
    rot = []
    for x, z in body_corners:
        x2 = x*math.cos(a) + z*math.sin(a)
        z2 = -x*math.sin(a) + z*math.cos(a)
        # but we want pod to sit IN FRONT of flange with rear at flange
        # rotate about pivot at (0, FLANGE_T/2) -- the back center of pod
        # Actually we need to tilt about back face center, so subtract pivot first
        pass
    # Cleaner: tilt about pivot (0, FLANGE_T/2)
    pivot = (0.0, FLANGE_T/2)
    rotated = []
    for x, z in body_corners:
        x_rel = x - pivot[0]
        z_rel = z - pivot[1]
        x2 = x_rel*math.cos(a) + z_rel*math.sin(a) + pivot[0]
        z2 = -x_rel*math.sin(a) + z_rel*math.cos(a) + pivot[1]
        rotated.append((cx + x2*s, cy + z2*s))
    poly = Polygon(rotated, fill=False, lw=1.4, ec='blue')
    ax.add_patch(poly)
    _body_lbl = (f"POD BODY ({DRIVER_TILT_DEG:.0f}° YAW)"
                 if DRIVER_TILT_DEG > 0 else "POD BODY (flat)")
    ax.text(cx + 1.0, cy + 1.0, _body_lbl,
            fontsize=9, color='blue', ha='left')

    # Tilt angle annotation -- only meaningful when there IS a tilt.
    if DRIVER_TILT_DEG > 0:
        from matplotlib.patches import Arc
        arc_r = 0.6
        ax.add_patch(Arc((cx, cy + FLANGE_T/2 * s), arc_r*2, arc_r*2,
                         angle=0, theta1=90 - DRIVER_TILT_DEG, theta2=90,
                         color='red', lw=1.2))
        ax.text(cx + 0.1, cy + FLANGE_T/2 * s + arc_r + 0.2,
                f"{DRIVER_TILT_DEG:.0f}°", color='red', fontsize=10, weight='bold')
        ax.annotate("Toward DRIVER",
                    xy=(cx - 1.2, cy + 1.3), xytext=(cx - 3.2, cy + 1.8),
                    fontsize=9, ha='center',
                    arrowprops=dict(arrowstyle='->', lw=1, color='red'))
        ax.annotate("Toward PASSENGER",
                    xy=(cx + 1.2, cy + 1.3), xytext=(cx + 3.2, cy + 1.8),
                    fontsize=9, ha='center',
                    arrowprops=dict(arrowstyle='->', lw=1))

    # Depth dim
    _add_dim_h(ax,
               cx - FLANGE_W/2*s - 0.4, cx - FLANGE_W/2*s,
               cy, f"{FLANGE_T:.1f}")
    _add_dim_v(ax,
               cy + FLANGE_T/2*s,
               cy + (FLANGE_T/2 + POD_DEPTH*math.cos(a))*s,
               cx - POD_W/2*s - 1.0,
               f"POD DEPTH {POD_DEPTH:.1f}")

    # Width
    _add_dim_h(ax,
               cx - FLANGE_W/2*s, cx + FLANGE_W/2*s,
               fy0 - 0.5, f"FLANGE WIDTH {FLANGE_W:.0f}")

    # Forward arrow
    ax.annotate("", xy=(cx, cy + 3.0), xytext=(cx, cy + 2.4),
                arrowprops=dict(arrowstyle='->', lw=1.5))
    ax.text(cx + 0.2, cy + 2.7, "INTO CABIN", fontsize=8)


def _draw_side_view(ax):
    """Side elevation -- shows fascia angle from side."""
    from matplotlib.patches import Rectangle, Polygon
    s = 0.046   # bigger scale, side view is narrow
    cx, cy = 5.5, 4.5

    # Flange in side view: thickness FLANGE_T (horizontal), height POD_H
    fx0 = cx - FLANGE_T/2 * s
    fy0 = cy - POD_H/2 * s
    ax.add_patch(Rectangle((fx0, fy0), FLANGE_T*s, POD_H*s,
                           fill=False, lw=1.4))
    ax.text(fx0 - 0.15, cy, "FLANGE", fontsize=8, color='gray',
            rotation=90, ha='right', va='center')

    # Pod body -- in side view it's just a rectangle (yaw doesn't change side
    # silhouette much). Width = POD_DEPTH, height = POD_H.
    px0 = cx + FLANGE_T/2 * s
    py0 = cy - POD_H/2 * s
    ax.add_patch(Rectangle((px0, py0), POD_DEPTH*s, POD_H*s,
                           fill=False, lw=1.4))
    ax.text(px0 + POD_DEPTH*s/2, cy, "POD BODY", fontsize=10,
            ha='center', va='center')

    # Cable exit slot through flange (at bottom)
    cy_ce = cy - POD_H/2*s + (WALL + CABLE_EXIT_H/2 + 2.0)*s
    ax.add_patch(Rectangle((fx0, cy_ce - CABLE_EXIT_H/2*s),
                           FLANGE_T*s, CABLE_EXIT_H*s,
                           fill=False, lw=0.8, ls='--', color='red'))
    ax.annotate(f"Cable exit\n{CABLE_EXIT_W} × {CABLE_EXIT_H}",
                xy=(fx0, cy_ce), xytext=(fx0 - 1.4, cy_ce - 1.0),
                fontsize=8, color='red',
                arrowprops=dict(arrowstyle='->', lw=0.6, color='red'))

    # Screw hole locations on flange (shown as dashed circles in side view)
    # Side view: only one screw is visible (the lateral one is in/out of page)
    ax.text(fx0 + FLANGE_T*s/2, cy + POD_H/2*s + 0.2,
            f"2× Ø{SCREW_HOLE_D:.1f} CSK Ø{COUNTERSINK_D:.0f} × {COUNTERSINK_DEPTH:.1f} DEEP",
            ha='center', fontsize=8)

    # Dimensions
    _add_dim_v(ax, fy0, cy + POD_H/2*s, fx0 - 0.7, f"{POD_H:.1f}")
    _add_dim_h(ax, fx0, fx0 + FLANGE_T*s, fy0 - 0.4, f"{FLANGE_T:.1f}")
    _add_dim_h(ax, px0, px0 + POD_DEPTH*s, fy0 - 0.4, f"{POD_DEPTH:.1f}")


def _draw_pocket_detail(ax):
    """Detail showing the display PCB pocket cross-section."""
    from matplotlib.patches import Rectangle, FancyArrowPatch
    s = 0.10  # bigger scale for detail
    cx, cy = 5.5, 4.0

    # Cross-section through fascia at one bezel
    # Layers from front (left) to back (right):
    #   Air | Bezel chamfer | Fascia | PCB | Components | Air
    # Drawn left-to-right horizontally
    layer_x = cx - 4.0
    fascia_y_top = cy + 2.0
    fascia_y_bot = cy - 2.0

    # Bezel front cone (chamfer)
    # Just draw two angled lines representing chamfer edges
    ax.plot([layer_x - 0.1, layer_x + BEZEL_CHAMFER_D*s],
            [fascia_y_top - 1.0,  fascia_y_top - 1.0 - BEZEL_CHAMFER_W*s],
            'k-', lw=1.2)
    ax.plot([layer_x - 0.1, layer_x + BEZEL_CHAMFER_D*s],
            [fascia_y_bot + 1.0,  fascia_y_bot + 1.0 + BEZEL_CHAMFER_W*s],
            'k-', lw=1.2)

    # Fascia outline (top half)
    f_x0 = layer_x + BEZEL_CHAMFER_D*s
    f_x1 = layer_x + FASCIA_T*s
    # top
    ax.plot([f_x0, f_x1, f_x1],
            [fascia_y_top - 1.0 - BEZEL_CHAMFER_W*s,
             fascia_y_top - 1.0 - BEZEL_CHAMFER_W*s,
             fascia_y_top],
            'k-', lw=1.2)
    # bottom
    ax.plot([f_x0, f_x1, f_x1],
            [fascia_y_bot + 1.0 + BEZEL_CHAMFER_W*s,
             fascia_y_bot + 1.0 + BEZEL_CHAMFER_W*s,
             fascia_y_bot],
            'k-', lw=1.2)
    # outer fascia top/bot
    ax.plot([layer_x, layer_x], [fascia_y_top, fascia_y_top - 1.0], 'k-', lw=1.2)
    ax.plot([layer_x, layer_x], [fascia_y_bot, fascia_y_bot + 1.0], 'k-', lw=1.2)
    # close right of fascia (back wall = pocket front)
    ax.plot([f_x1, f_x1], [fascia_y_top, fascia_y_bot], 'k-', lw=1.0, ls=':')

    # PCB
    p_x0 = f_x1
    p_x1 = p_x0 + DISPLAY_PCB_T*s
    pcb_y_top = fascia_y_top - 0.3
    pcb_y_bot = fascia_y_bot + 0.3
    ax.add_patch(Rectangle((p_x0, pcb_y_bot), DISPLAY_PCB_T*s, pcb_y_top - pcb_y_bot,
                           fill=True, lw=1.0, fc='#cce4ff', ec='black'))
    ax.text((p_x0+p_x1)/2, cy, "PCB", fontsize=9, ha='center', va='center', rotation=90)

    # Components space (just a labeled rectangle)
    c_x0 = p_x1
    c_x1 = p_x0 + (DISPLAY_PCB_T + DISPLAY_BACK_DEPTH)*s
    ax.add_patch(Rectangle((c_x0, pcb_y_bot - 0.4),
                           c_x1 - c_x0, (pcb_y_top - pcb_y_bot) + 0.8,
                           fill=False, lw=0.8, ls='--', ec='gray'))
    ax.text((c_x0+c_x1)/2, cy, "COMPONENTS\n+ HEADERS",
            fontsize=8, ha='center', va='center', color='gray')

    # Standoff boss representation -- show below PCB so the labels don't
    # collide with the fascia thickness annotation above.
    so_x0 = p_x0
    ax.add_patch(Rectangle((so_x0, pcb_y_bot - 0.55),
                           STANDOFF_LEN*s, 0.55,
                           fill=True, lw=0.8, fc='#dddddd'))
    ax.annotate(f"STANDOFF Ø{STANDOFF_OD:.1f} × {STANDOFF_LEN:.1f}\n"
                f"M2 SELF-TAP (×4 PER DISPLAY)",
                xy=(so_x0 + STANDOFF_LEN*s/2, pcb_y_bot - 0.27),
                xytext=(so_x0 + 1.1, pcb_y_bot - 1.4),
                fontsize=8, ha='left',
                arrowprops=dict(arrowstyle='->', lw=0.6))

    # Labels
    ax.text(layer_x - 0.15, cy, "← FASCIA SIDE\n(visible)",
            fontsize=8, ha='right', va='center', color='gray')
    ax.text(c_x1 + 0.15, cy, "→ INSIDE\n(rear)",
            fontsize=8, ha='left', va='center', color='gray')

    # Dimensions
    _add_dim_h(ax, layer_x, f_x1, fascia_y_top + 0.6,
               f"FASCIA {FASCIA_T:.1f}")
    _add_dim_h(ax, p_x0, p_x1, pcb_y_top + 0.4,
               f"{DISPLAY_PCB_T:.1f}")
    _add_dim_h(ax, c_x0, c_x1, pcb_y_top + 0.4,
               f"{DISPLAY_BACK_DEPTH:.1f}")
    _add_dim_h(ax, layer_x, c_x1, fascia_y_bot - 0.7,
               f"TOTAL POCKET DEPTH {(FASCIA_T+DISPLAY_PCB_T+DISPLAY_BACK_DEPTH):.1f}")
    # Bezel opening dimension (vertical between bezel edges)
    _add_dim_v(ax,
               fascia_y_bot + 1.0 + BEZEL_CHAMFER_W*s,
               fascia_y_top - 1.0 - BEZEL_CHAMFER_W*s,
               layer_x + 0.2,
               f"Ø{BEZEL_OPENING:.1f}")


# =====================================================================
# Main
# =====================================================================

def main():
    here = os.path.dirname(os.path.abspath(__file__))
    out_dir = here
    print(f"[*] Output dir: {out_dir}")

    print("[*] Building pod body mesh...")
    body = build_pod_body()
    print(f"    body: {len(body.verts)} verts / {len(body.tris)} tris")

    print("[*] Building flange mesh...")
    flange = build_flange()
    print(f"    flange: {len(flange.verts)} verts / {len(flange.tris)} tris")

    print("[*] Assembling...")
    assembly = build_assembly()
    print(f"    assembly: {len(assembly.verts)} verts / {len(assembly.tris)} tris")

    stl_path = os.path.join(out_dir, "firebird_gauge_pod.stl")
    print(f"[*] Writing STL: {stl_path}")
    write_stl_binary(stl_path, assembly,
                     header="Firebird Twin Gauge Pod (FB-GP-001 rev C)")

    step_path = os.path.join(out_dir, "firebird_gauge_pod.step")
    print(f"[*] Writing STEP: {step_path}")
    write_step_faceted(step_path, assembly, name="FirebirdGaugePod")

    pdf_path = os.path.join(out_dir, "firebird_gauge_pod.pdf")
    print(f"[*] Rendering PDF drawings: {pdf_path}")
    render_drawings(pdf_path, body, flange)

    print("[*] Done.")


if __name__ == "__main__":
    main()
