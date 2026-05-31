// =====================================================================
//  Firebird Twin Gauge Pod — back cover plate
//
//  Companion to firebird_gauge_podR1.scad. Holds the two PCBs in their
//  pockets by pressing them against the bezel shoulder. Sits flush in
//  the recessed back face of the pod (no visible edge from the side).
//
//  Print orientation: lay flat on the build plate, any side up. No
//  supports needed.
//
//  Hardware required:
//    - 4× M3 brass heat-set inserts (Ø ~5 mm × 4 mm typical), pressed
//      into the pod's back face after the pod is printed
//    - 4× M3 × 8 mm socket-head cap screws
//
//  All dimensions in mm.
// =====================================================================

// ---------- DIMENSIONS — must match the matching block in
//            firebird_gauge_podR1.scad ---------------------------------
COVER_W              = 160;    // cover plate width
COVER_H              = 80;     // cover plate height
COVER_T              = 3;      // cover plate thickness
COVER_INSERT_X       = 65;     // screw hole X position (±)
COVER_INSERT_Y       = 35;     // screw hole Y position (±)

// ---------- DISPLAY GEOMETRY (matches pod) ----------------------------
DISPLAY_SPACING      = 80;     // bezel center-to-center
DISPLAY_PCB_OD       = 75.0;   // PCB outer diameter

// ---------- COVER-SPECIFIC --------------------------------------------
CABLE_CUTOUT_D       = 60;     // round cable / connector access opening
                               //   (must be < DISPLAY_PCB_OD so the cover
                               //    rim presses on the back of the PCB)
SCREW_CLEAR_D        = 3.5;    // M3 clearance hole
SCREW_HEAD_D         = 6.0;    // M3 socket-head counterbore Ø
SCREW_HEAD_DEPTH     = 2.0;    // counterbore depth (cover is 3 mm thick)

// ---------- COSMETIC --------------------------------------------------
COVER_CORNER_R       = 3;      // soften the cover's outer corners

// ---------- RENDER QUALITY --------------------------------------------
$fn = 64;
EPS = 0.05;

// =====================================================================
// HELPER: rounded plate (2D rounded rectangle, extruded)
// =====================================================================
module rounded_plate(sx, sy, sz, r) {
    linear_extrude(height = sz)
        offset(r = r)
            offset(r = -r)
                square([sx, sy], center = true);
}

// =====================================================================
// BUILD
// =====================================================================
//   Coordinate frame matches the pod:
//     X+ = passenger side, Y+ = up
//     Cover centered at origin in X and Y, sitting on z=0
//     Z=0 is the back face (flush with pod back face after install)
//     Z=COVER_T is the inside (pressing against PCB backs)

difference() {
    // 1. Cover plate — rounded rectangle
    rounded_plate(COVER_W, COVER_H, COVER_T, r = COVER_CORNER_R);

    // 2. Two cable / connector cutouts, centered on each PCB pocket
    for (xs = [-1, 1])
        translate([xs * DISPLAY_SPACING/2, 0, -EPS])
            cylinder(d = CABLE_CUTOUT_D, h = COVER_T + 2*EPS);

    // 3. Four M3 clearance holes with counterbores for socket-head
    //    screws. Counterbore is on the BACK side (z=0) so the screw
    //    head sits flush with the cover back (which is flush with the
    //    pod back face after install).
    for (xs = [-1, 1], ys = [-1, 1]) {
        // Through-clearance hole
        translate([xs * COVER_INSERT_X, ys * COVER_INSERT_Y, -EPS])
            cylinder(d = SCREW_CLEAR_D, h = COVER_T + 2*EPS);
        // Counterbore for the screw head
        translate([xs * COVER_INSERT_X, ys * COVER_INSERT_Y, -EPS])
            cylinder(d = SCREW_HEAD_D, h = SCREW_HEAD_DEPTH + EPS);
    }
}
