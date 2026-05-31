# Caliper measurements — VERIFIED 2026-04

Original purpose of this file was to track unknowns before patching the
SCAD. All five values are now confirmed and folded into Rev B of
`firebird_gauge_pod.scad` and `build_pod.py`.

## Verified values

| # | Measurement | Verified value | Source |
|---|---|---|---|
| 1 | PCB OD (round) | **75.0 mm** | Waveshare wiki spec ("75.00 ±0.1") |
| 2 | PCB thickness  | **1.6 mm**  | typical Waveshare 2-layer (user-confirmed) |
| 3 | Hole pattern (X o.c.) — top pair | **59.28 mm** | user caliper |
| 3 | Hole pattern (X o.c.) — bottom pair | **36.00 mm** | user caliper |
| 3 | Hole pattern (Y from PCB center) — top row | **+14.21 mm** | wiki dimension drawing |
| 3 | Hole pattern (Y from PCB center) — bottom row | **−27.07 mm** | wiki dimension drawing |
| 4 | Hole Ø (M2)   | **2.4 mm clearance** | drawing "ΦM2" callout (close-fit) |
| 4 | Number of holes | **4** (trapezoidal) | drawing |
| 5 | Rear stack height | **8.91 mm** | user caliper |

Geometric sanity: with PCB radius 37.5 mm, the four hole positions
(±29.64, +14.21) and (±18.00, −27.07) sit 4.6 mm and 5.0 mm in from the
PCB edge respectively — both healthy edge clearances.

## Net effect on the pod

| Param | Rev A | Rev B |
|---|---|---|
| PCB pocket | 65 × 65 mm square | Ø 75.6 mm round |
| Display center spacing | 78 mm | **90 mm** (so Ø75 PCBs don't fight) |
| POD_W | 149 mm | **171 mm** |
| POD_H | 73 mm | **83 mm** |
| POD_DEPTH | 24.5 mm | **19.0 mm** (rear stack is only 8.91, so pod can be shallower) |
| Hardware | M2.5 × 6 | **M2 × 4** |
| Standoff Ø / pilot | 5.5 / 2.2 | **5.0 / 1.7** |

## What's left

Re-render the deliverables. Two ways:

```bash
cd C:\Firebird-Gauges\ESP32-S3-Multi-Gauge\mount
python3 build_pod.py
```

…regenerates `firebird_gauge_pod.stl`, `.step`, and `.pdf`.

Or open `firebird_gauge_pod.scad` in OpenSCAD, F6 to render, and export
STL from the File menu.

## Reference shortcuts (kept for posterity)

- `WAVESHARE_DIMENSION_DRAWING.url` — official PNG with hole positions
- `WAVESHARE_WIKI.url` — wiki page with PCB OD spec
- `MEASURE_THIS.svg` — original measurement diagram
