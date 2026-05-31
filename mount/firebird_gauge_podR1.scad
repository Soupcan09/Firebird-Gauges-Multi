// =====================================================================
//  Firebird Twin Gauge Pod — minimum viable v1
//  Clean restart. Get the holes and dimensions right; aesthetics later.
//
//  What's in this version:
//    - Solid rectangular pod body
//    - 2 round bezel openings (Ø55) cut into the front face
//    - 2 round PCB pockets (Ø75.6) directly behind the bezels,
//      going all the way through the back face (PCB drops in from
//      behind, cables exit through the pocket opening)
//    - 4 M2 self-tap pilot holes per display at the trapezoidal
//      standoff positions, hidden 2 mm behind the front face
//    - 2 placeholder pod-to-dash mounting holes on the top edge
//
//  What's NOT in this version (will add later):
//    - Rounded outer edges
//    - Chamfered cosmetic trim ring around each bezel
//    - Hollow internal cavity (saves print weight)
//    - Cable exit slot
//    - Mounting flange with countersunk dash holes
//
//  Print orientation:
//    Back face on the build plate (z=0), fascia points up.
//    The bezel openings on top print clean (no overhangs needed).
//    The PCB pockets print as straight cylindrical walls upward,
//    also clean. The pilot holes print as small blind holes from
//    the back of the fascia toward the front — also fine.
//
//  All dimensions in mm.
// =====================================================================

// ---------- DISPLAY MODULE (Waveshare ESP32-S3-Touch-LCD-2.1) ----------
DISPLAY_PCB_OD     = 75.0;     // round PCB outer diameter (wiki)
DISPLAY_PCB_T      = 1.6;      // PCB thickness
DISPLAY_BACK_DEPTH = 8.91;     // tallest rear component (calipered)
BEZEL_OPENING      = 55.0;     // visible round opening through fascia
PCB_POCKET_OS      = 0.6;      // pocket oversize: Ø75.6 pocket vs Ø75 PCB
                               // = 0.3 mm clearance per side. Snug press-fit
                               // intentionally. PCB held in place by friction
                               // plus a bead of hot glue or VHB tape.

// ---------- POD OVERALL ----------
POD_W           = 175;         // < 215.9 envelope, leaves margin
POD_H           = 100;         // < 106.4 envelope, leaves margin
FASCIA_T        = 4.5;         // depth of bezel cut into the fascia
DISPLAY_SPACING = 80;          // bezel center-to-center

// Pod depth = fascia + PCB + back-of-PCB clearance + a little extra
POD_DEPTH = FASCIA_T + DISPLAY_PCB_T + DISPLAY_BACK_DEPTH + 4;  // ~19

// ---------- POD-TO-DASH MOUNTING ----------
// Two blind holes on the TOP face of the pod. Screws come down from
// the dash side and thread into these holes (use heat-set M5 inserts,
// or self-tap with Ø ~4.2 hole instead of Ø 5.5 if you prefer).
SCREW_HOLE_D     = 5.5;        // for M5 clearance / heat-set insert
SCREW_HOLE_DEPTH = 10;         // depth down from top face
SCREW_SPACING    = 104;        // o.c. (calipered on dash)
SCREW_Z_OFFSET   = POD_DEPTH/2; // depth-wise, midway through the depth

// ---------- COSMETIC ----------
CORNER_R         = 5;          // outer corner radius of pod body
BEZEL_CHAMFER_W  = 2.2;        // width of chamfered trim ring
BEZEL_CHAMFER_D  = 1.0;        // depth of chamfered trim ring

// ---------- BACK COVER (companion part — see firebird_pod_back_cover.scad)
// The cover slides into a recess on the back face so its edge is flush
// with the pod's back face (invisible from the side).
COVER_W              = 160;    // cover plate width
COVER_H              = 80;     // cover plate height
COVER_T              = 3;      // cover plate thickness
COVER_RECESS_CLEAR   = 0.5;    // clearance on each side for slip fit
COVER_INSERT_X       = 65;     // M3 heat-set insert X position (±)
COVER_INSERT_Y       = 35;     // M3 heat-set insert Y position (±)
COVER_INSERT_HOLE_D  = 4.0;    // hole Ø for M3 brass heat-set insert
COVER_INSERT_DEPTH   = 5;      // depth into pod material from recess floor

// ---------- RENDER QUALITY ----------
$fn = 64;
EPS = 0.05;

// =====================================================================
// HELPER: rounded box (hull of corner spheres)
// =====================================================================
module rounded_cube(size, r=4) {
    sx = size[0]; sy = size[1]; sz = size[2];
    hull() {
        for (x = [r, sx - r], y = [r, sy - r], z = [r, sz - r])
            translate([x, y, z])
                sphere(r = r, $fn = 32);
    }
}

// =====================================================================
// BUILD
// =====================================================================
//   Coordinate frame:
//     X+ = passenger side, Y+ = up (toward dash), Z+ = forward (cabin)
//     Pod centered at origin in X and Y.
//     Z=0 is the back face (touching dash undercut after install).
//     Z=POD_DEPTH is the front face (fascia, facing driver).

difference() {
    // 1. Outer pod body — rounded rectangular box
    translate([-POD_W/2, -POD_H/2, 0])
        rounded_cube([POD_W, POD_H, POD_DEPTH], r = CORNER_R);

    // 2. Per-display cuts: bezel + chamfer trim ring + PCB pocket.
    //    PCB retention is friction-fit + adhesive; no pilot holes in v1.
    for (xs = [-1, 1]) {
        translate([xs * DISPLAY_SPACING/2, 0, 0]) {

            // Bezel: Ø55 cut from front face inward, FASCIA_T deep
            translate([0, 0, POD_DEPTH - FASCIA_T - EPS])
                cylinder(d = BEZEL_OPENING,
                         h = FASCIA_T + 2*EPS);

            // Chamfered trim ring at the front face — widens at the
            // front and tapers down to the bezel diameter, giving the
            // bezel a cosmetic "Stewart-Warner" lip.
            translate([0, 0, POD_DEPTH - BEZEL_CHAMFER_D - EPS])
                cylinder(d1 = BEZEL_OPENING + 2*BEZEL_CHAMFER_W,
                         d2 = BEZEL_OPENING,
                         h = BEZEL_CHAMFER_D + 2*EPS);

            // PCB pocket: Ø75.6 from the back face up to the back of
            // the fascia. PCB inserts from the back; cables route out
            // through the same opening. The PCB rests on the bezel
            // shoulder (annular ring between Ø55 and Ø75.6 at z=POD_DEPTH-FASCIA_T).
            translate([0, 0, -EPS])
                cylinder(d = DISPLAY_PCB_OD + PCB_POCKET_OS,
                         h = POD_DEPTH - FASCIA_T + EPS);
        }
    }

    // 3. Pod-to-dash screw holes — two blind holes on the TOP face,
    //    going down SCREW_HOLE_DEPTH mm. 104 mm o.c. in X, midway
    //    through the depth in Z.
    for (xs = [-1, 1])
        translate([xs * SCREW_SPACING/2,
                   POD_H/2 - SCREW_HOLE_DEPTH,
                   SCREW_Z_OFFSET])
            rotate([-90, 0, 0])
                cylinder(d = SCREW_HOLE_D,
                         h = SCREW_HOLE_DEPTH + EPS);

    // 4. Back-cover recess — a thin rectangular pocket on the back
    //    face that the back cover sits flush into. Slightly larger than
    //    the cover for a slip fit.
    translate([-(COVER_W + 2*COVER_RECESS_CLEAR)/2,
               -(COVER_H + 2*COVER_RECESS_CLEAR)/2,
               -EPS])
        cube([COVER_W + 2*COVER_RECESS_CLEAR,
              COVER_H + 2*COVER_RECESS_CLEAR,
              COVER_T + EPS]);

    // 5. M3 heat-set insert holes — 4 corners of the cover area.
    //    Holes start at the recess floor (z=COVER_T) and extend
    //    INWARD (deeper into the pod body) by COVER_INSERT_DEPTH.
    for (xs = [-1, 1], ys = [-1, 1])
        translate([xs * COVER_INSERT_X,
                   ys * COVER_INSERT_Y,
                   COVER_T - EPS])
            cylinder(d = COVER_INSERT_HOLE_D,
                     h = COVER_INSERT_DEPTH + 2*EPS);
}
