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
FASCIA_T        = 2;         // depth of bezel cut into the fascia
DISPLAY_SPACING = 80;          // bezel center-to-center (was 105 — closer)

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

// ---------- RENDER QUALITY ----------
$fn = 64;
EPS = 0.05;

// =====================================================================
// BUILD
// =====================================================================
//   Coordinate frame:
//     X+ = passenger side, Y+ = up (toward dash), Z+ = forward (cabin)
//     Pod centered at origin in X and Y.
//     Z=0 is the back face (touching dash undercut after install).
//     Z=POD_DEPTH is the front face (fascia, facing driver).

difference() {
    // 1. Outer rectangular pod body
    translate([-POD_W/2, -POD_H/2, 0])
        cube([POD_W, POD_H, POD_DEPTH]);

    // 2. Per-display cuts: bezel + PCB pocket only.
    //    PCB retention is friction-fit + adhesive; no pilot holes in v1.
    for (xs = [-1, 1]) {
        translate([xs * DISPLAY_SPACING/2, 0, 0]) {

            // Bezel: Ø55 cut from front face inward, FASCIA_T deep
            translate([0, 0, POD_DEPTH - FASCIA_T - EPS])
                cylinder(d = BEZEL_OPENING,
                         h = FASCIA_T + 2*EPS);

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
}
