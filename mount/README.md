# Firebird Twin Gauge Pod — FB-GP-001

A period-correct (woodgrain-skinnable) twin-display housing for the
1967 Pontiac Firebird, designed to mount **under the dash undercut**
(immediately below the AM radio bezel, in the gap above the center
console). Built around two Waveshare **ESP32-S3-Touch-LCD-2.1** round
display modules, paired with the firmware in
`C:\Firebird-Gauges\ESP32-S3-Multi-Gauge`.

## Package contents

| File                          | Format          | Purpose                                  |
|-------------------------------|-----------------|------------------------------------------|
| `firebird_gauge_pod.scad`     | OpenSCAD        | **Source of truth** — fully parametric   |
| `firebird_gauge_pod.stl`      | Binary STL      | Slicer-ready (≈ 18 k tris)               |
| `firebird_gauge_pod.step`     | ISO 10303 AP203 | Faceted B-rep (SolidWorks / Fusion / FreeCAD) |
| `firebird_gauge_pod.pdf`      | PDF, 6 sheets   | Dimensioned shop drawings                |
| `build_pod.py`                | Python 3        | Regenerates STL/STEP/PDF from parameters |
| `README.md`                   | This file       | Build & install notes                    |

## Geometry summary  (Rev C — 2026-04, dash-mounted, flat fascia)

```
Envelope:   215.9 × 106.4 mm max  (8.5" × 4 3/16" — calipered in car)
Overall:    ≈ 200 W × 100 H × 19 D  mm   (excluding flange)
Flange:     200 × 100 × 5 mm,  2× #10 wood-screw / M5 holes (CSK) at 160 o.c.
Bezels:     2× Ø55 mm, 105 mm center spacing, 0° tilt (FLAT)
Pocket:     Ø75.6 mm round × 1.6 mm deep for PCB; 8.91 mm clearance
            behind PCB for USB-C / pin headers
Standoffs:  4 per display, Ø5.0 mm, 3 mm tall, M2 self-tap (Ø1.7)
            Trapezoid pattern: top pair 59.28 o.c. @ Y +14.21,
                               bot pair 36.00 o.c. @ Y −27.07
Cable exit: 30 × 12 mm slot in rear flange, near bottom
Mount:      Bolts up into the underside of the dash undercut (the lip
            below the AM radio bezel). Use threaded inserts (nutserts)
            in the dash sheet metal for clean fastening.
```

All dimensions are **parametric** — to tune any value, edit the
`PARAMETERS` block at the top of `firebird_gauge_pod.scad` and re-render,
or edit the matching block in `build_pod.py` and run `python3 build_pod.py`.

## Display module specs (verified, Rev B)

The Waveshare ESP32-S3-Touch-LCD-2.1 is a **round** PCB (not square):

```
DISPLAY_PCB_OD     = 75.0    // mm   PCB outer diameter (wiki: 75.00 ±0.1)
DISPLAY_PCB_T      = 1.6     // mm   PCB thickness
DISPLAY_VIS_OD     = 53.28   // mm   visible glass diameter (wiki drawing)
DISPLAY_HOLE_D     = 2.4     // mm   M2 close-fit clearance (drawing: ΦM2)
DISPLAY_BACK_DEPTH = 8.91    // mm   tallest rear component (calipered)

DISPLAY_HOLES = [            // mm   trapezoidal pattern (NOT a bolt circle)
    [-29.64, +14.21],        //      top-left
    [+29.64, +14.21],        //      top-right
    [-18.00, -27.07],        //      bottom-left
    [+18.00, -27.07],        //      bottom-right
];
```

Sources: https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-2.1 (PCB OD,
visible glass) + drawing https://www.waveshare.com/w/upload/6/61/ESP32-S3-Touch-LCD-2.1-introduction-03.png
(hole spacing Y, ΦM2) + caliper readings on physical hardware
(hole spacing X, rear stack height).

## Print recipe

| Setting          | Value                                              |
|------------------|----------------------------------------------------|
| Material         | ABS, ASA, or PETG (PLA will warp on a hot dash)   |
| Layer height     | 0.20 mm                                            |
| Walls            | 4 perimeters (≥ 1.6 mm shell)                      |
| Infill           | 25–35 % gyroid                                     |
| Orientation      | Rear flange flat on the build plate                |
| Supports         | Tree supports under the bezel chamfer ring only    |
| Estimated time   | ≈ 5–7 hr at 0.2 mm                                 |
| Filament         | ≈ 90 g                                             |

## Assembly steps

1. **Print the pod** in ABS/ASA/PETG per recipe above.
2. **Tap the standoffs** with an M2.5 tap (or just thread M2.5 × 6 mm
   self-tappers on first install — the Ø2.2 hole sizes for self-tap).
3. **Drop displays in** from the back. Glass faces forward through the
   Ø55 bezel openings; PCB drops into the Ø75.6 round pocket.
4. **Secure with 4× M2 × 4 mm** machine screws per display
   (8 screws total). Don't over-torque into printed plastic — snug only.
   Holes are on a trapezoidal pattern (NOT a square BCD) — the SCAD
   defines all four positions explicitly.
5. **Route the wiring loom** out the 30 × 12 cable slot in the rear
   flange. (Power, I²C/SPI, ground bus, and any CAN/sensor leads.)
6. **Skin the fascia** for period correctness. Two good options:
   - **3M Di-Noc MWG-series woodgrain vinyl** — heat-form around the
     gentle bezel curvature, trim with a fresh blade.
   - **Real wood veneer** (~0.6 mm walnut, rosewood, or burl) — spray
     contact cement, lay it down, finish with 2K clear.
   Both pair well with the existing dash + console woodgrain.
7. **Add a chrome accent ring** around each bezel (1/4-inch chrome
   trim strip from any auto-parts store) for the classic
   Stewart-Warner / Sun Super Tach trim-ring look.
8. **Mount to dash** (Rev C). Pilot-drill the underside of the dash
   undercut (the lip below the AM radio bezel) at the two screw
   locations (160 mm CTC, centered laterally). For sheet-metal dash
   structure, install threaded inserts (M5 nutserts) into the pilot
   holes for clean reusable fastening. Drive 2× #10 × 1" oval-head
   screws (or M5 machine screws into the nutserts) through the
   countersunk flange holes. Don't overtighten.

## Re-rendering after changes

Edit `firebird_gauge_pod.scad`, open in OpenSCAD, press **F6** to
render, then **File → Export → Export as STL**.

If you'd rather edit Python:

```bash
cd firebird_pod
python3 build_pod.py
# regenerates firebird_gauge_pod.stl, .step, and .pdf
```

## Regenerating STEP from a clean parametric source

The included `.step` file is a faceted B-rep (every triangle is its
own ADVANCED_FACE). It's accepted by SolidWorks, Fusion 360, and
FreeCAD, but it's not parametrically editable. For a true parametric
STEP:

1. Open `firebird_gauge_pod.scad` in **FreeCAD** (the OpenSCAD
   importer ships with FreeCAD).
2. Tessellate / import → **File → Export → STEP (\*.step \*.stp)**.
3. The result is a B-rep with planar/cylindrical surfaces instead of
   a faceted shell.

## Notes & caveats

- The pod body in the STL is a simplified rectangular wedge (no
  rounded outer edges). The OpenSCAD master uses a `hull()` of two
  tapered rounded boxes for a softer aesthetic — render the SCAD file
  if you want that look on the actual print.
- Driver tilt is **0° (flat fascia)** in Rev C. The `DRIVER_TILT`
  parameter is retained in both source files in case you ever want
  to re-introduce a yaw — set it to a positive value (degrees) and
  re-render.
- Display center spacing is 105 mm, leaving a 50 mm gap between
  bezel cutouts (29 mm gap between the round PCB pocket walls).
  Increase `DISPLAY_SPACING` if you want a wider stance — but stay
  within the 215.9 mm width envelope.
- Internal volume after the displays is ≈ 90 cm³ — plenty of room
  for an ESP32 daughter board, a CAN transceiver, voltage regulators,
  or a dedicated harness header. Add a bulkhead with a Molex
  Mini-Fit Jr. for clean serviceability if you want.

## Rev history

- **Rev A** (initial): assumed square 65 × 65 PCB, 58 mm square BCD,
  M2.5 hardware, 14 mm rear depth. Pod was 149 × 73 × 28 mm with
  15° driver tilt, mounted to console front face.
- **Rev B** (2026-04): corrected to round Ø75 PCB, trapezoidal M2
  hole pattern, 1.6 mm PCB, 8.91 mm rear depth (verified against
  Waveshare wiki + dimension drawing + caliper). Pod grew to
  171 × 83 × 24 mm and bezel center spacing widened to 90 mm.
- **Rev C** (2026-04): in-car fitment confirmed envelope of
  215.9 × 106.4 mm under the dash undercut. Pod grown to
  200 × 100 × 24 mm to fill the available area, bezel spacing
  widened to 105 mm, screw spacing widened to 160 mm. Driver tilt
  removed (flat fascia, 0°). Mounting target moved from console
  front face to the dash underside.
