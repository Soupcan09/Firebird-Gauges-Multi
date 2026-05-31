# GPS Hardware Options for the Firebird Multi-Gauge

A running record of the GPS-module-and-antenna decisions for the
Firebird gauge, captured here so we don't have to re-derive it next
time we're picking parts. Updated as new options come up.

## Current state on the bench

Working: **Beitian BN-880** GPS module, powered externally at **5 V**
(NOT off the Waveshare's JST 3 V3 — the BN-880 needs 3.6 V minimum,
which the JST can't supply). UART1 on GPIO 43/44 reads NMEA at 9600
baud. Reaches `TRACKING 10` satellites in the garage; gets a real
`FIX` outdoors or by a window.

The firmware is solid; the limitation is purely physical — the
internal patch antenna on the BN-880 needs a clear sky view, which
isn't going to happen behind a metal-roof car dashboard.

## Constraints we're shopping against

1. **Module body must be hideable.** Going behind the center dash
   speaker grille if reception there allows it; otherwise tucked
   inside the gauge enclosure.
2. **Antenna must be a small black hockey-puck on a magnetic / adhesive
   base** with ~3 ft of cable, mountable on the dashboard with a clear
   view through the windshield.
3. **Output: 9600 baud NMEA over UART, 3.3 V or 5 V TTL.** That's what
   the firmware in `main/GPS/GPS.c` expects.
4. **Power: ideally runs off the Waveshare's JST 3 V3 pin** so we don't
   have to add a buck converter. If the module needs 5 V, it has to
   tap into the upstream 12 V → 5 V regulator that powers the
   Waveshare itself.
5. **Antenna connector: SMA jack on the module** (or u.FL with adapter)
   so any standard puck antenna screws on.

## Options we've considered

### Option A — Keep the BN-880, add external 5 V supply (current setup)

| Item | Cost | Notes |
|---|---|---|
| Existing BN-880 | $0 | already on hand |
| 12 V → 5 V buck converter | ~$3–5 | needed for car install |
| External puck antenna | n/a | Jeff confirmed this BN-880 has no u.FL |

**Verdict:** Works on the bench but doesn't solve the hide-the-module
problem because BN-880's antenna is internal. **Not the long-term
answer.** Useful as the proof-of-concept while waiting for a better
module to ship.

### Option B — NEO-7M module with built-in SMA jack + separate puck antenna **(RECOMMENDED)**

| Item | Cost | Notes |
|---|---|---|
| [NEO-7M GPS Module with SMA Antenna Interface, 3.3 V/5 V TTL](https://www.amazon.com/Compatible-Antenna-Interface-Included-Replaces/dp/B0G7C59W58) | ~$10 | replaces NEO-6M, well-known chip |
| [Geekstory Active GPS+GLONASS Antenna, 33 dB, 3 m cable, IP67](https://www.amazon.com/Geekstory-Navigation-Connector-External-Tracking/dp/B0D9JQGQ97) | ~$15 | black puck, magnetic, SMA male |
| **Total** | **~$25** | |

**Pros:**
- **Runs on 3.3 V** off the Waveshare JST — no buck converter needed.
- **Default 9600 baud NMEA** — drop-in firmware compatibility, no
  changes to `main/GPS/GPS.c`.
- SMA jack is industry-standard, antenna swappable forever.
- 33 dB gain on the antenna is strong enough to punch through the dash
  speaker grille.

**Cons:**
- NEO-7M is GPS-only (no GLONASS/BeiDou). Doesn't matter for North
  American driving; plenty of GPS satellites overhead at all times.
- Older chip (~2013) than the M8N. Slightly slower cold-start TTFF
  (~30 s vs ~25 s). Not noticeable in practice.

**This is the lowest-risk, lowest-cost path.** If you want to spend
money once and forget it, buy these two parts.

### Option C — ABUQ NEO-M8N module with SMA jack + separate puck antenna

| Item | Cost | Notes |
|---|---|---|
| ABUQ NEW GPS Mini Module Neo-8n NEO-M8N-0-01 with SMA Head | ~$15 | does NOT come with antenna |
| Geekstory 33 dB puck antenna (same as Option B) | ~$15 | black puck, magnetic, SMA male |
| **Total** | **~$30** | |

**Pros:**
- NEO-M8N is multi-constellation (GPS + GLONASS + BeiDou), 72 channels.
- Slightly faster TTFF and better multipath rejection than the 7M.
- SMA jack on the module.

**Cons:**
- **Default baud rate not confirmed.** Could ship at 9600, 38400, 57600,
  or 115200. Most "Arduino-targeted" listings are 9600 NMEA but ABUQ's
  listing didn't specify.
- **Output protocol not confirmed.** Most ship as NMEA, but some
  Pixhawk-targeted M8N modules default to UBX binary.

**Firmware mitigation if it ships at the wrong baud / protocol:**
- I'll add an **auto-probe** to `GPS_Init()` that tries 9600 → 38400
  → 57600 → 115200 in sequence, locking onto whichever yields valid
  NMEA. ~30 lines of new code, runs once on boot, +5 s startup time
  worst case.
- If the module ships in **UBX binary mode**, I'll add a one-time
  configuration sequence (`CFG-PRT`, `CFG-MSG`, `CFG-CFG`) that flips
  it to NMEA at 9600 and saves the config to its flash so it sticks.
  ~80 lines of code.

**Verdict:** Not zero-risk, but recoverable if it ships funky. Pick
this if you specifically want the M8N chip / multi-constellation.

### Option D — Beitian BN-880Q (alternative cheap drop-in)

| Item | Cost | Notes |
|---|---|---|
| [Beitian BN-880Q GLONASS GPS Module](https://www.amazon.com/Compatible-Airplanes-Helicopters-Multirotors-Freestyle/dp/B0DR8J3J79) | ~$15 | NEO-M8N chip, 3.0 V–5.5 V range |

**Pros:**
- **Wider voltage range (3.0–5.5 V)** so it'll run off the JST 3 V3 pin
  unlike our current BN-880. No external 5 V supply needed.
- 9600 baud NMEA default.
- NEO-M8N chip.

**Cons:**
- **Internal patch antenna only** (or u.FL — varies by batch). Doesn't
  solve the hide-the-module problem unless we add an external antenna
  separately, in which case we're back at Option C.

**Verdict:** Useful if we just want to fix the 5 V power issue and keep
the same form factor, but doesn't get us to a hidden install.

### Option E — SparkFun NEO-M9N SMA breakout (premium)

| Item | Cost | Notes |
|---|---|---|
| [SparkFun NEO-M9N (Qwiic) with SMA jack](https://www.amazon.com/SparkFun-Breakout-Configuration-Time-First-Fix/dp/B09YX6QZFQ) | ~$50 | newest u-blox chip |
| Geekstory 33 dB puck antenna | ~$15 | |
| **Total** | **~$65** | |

**Pros:**
- Newest u-blox chip (M9N, post-M8). Best TTFF and multipath in the
  family.
- SparkFun documentation, libraries, and warranty.

**Cons:**
- Overkill for displaying MPH on a Firebird gauge.
- 2x the cost of Option B with no real benefit for this use case.

**Verdict:** Unnecessary. Skip.

### Option F — Original BN-880 + u.FL pigtail + puck antenna (DEAD END)

I kept suggesting this because most BN-880 modules do have a u.FL
connector for an external antenna. Jeff confirmed his specific module
does NOT have u.FL accessible. Removed from consideration.

## Recommendation

**Buy Option B (NEO-7M + Geekstory puck antenna, ~$25).** Lowest cost,
lowest risk, runs on the JST 3 V3 with no buck converter, and the
firmware needs no changes whatsoever. The "older chip" caveat is
academic — you'll never notice the TTFF difference on a Firebird
gauge.

If for some reason Option B doesn't pan out (out of stock, looks
weird, etc.), Option C (ABUQ NEO-M8N + same puck antenna) is the next
choice with the auto-probe firmware mitigation.

## Antenna positioning on the dashboard

Once the puck antenna arrives, install plan:
1. **Module body**: tucked behind the center dash speaker grille, or
   inside the gauge enclosure if grille reception is too weak.
2. **Antenna puck**: stuck (magnetic + adhesive) on the **top of the
   dashboard near the windshield**, label / sky-side facing UP.
   Short SMA cable run from antenna to module body, hidden under the
   dashpad.
3. **Power**: 3 V3 from the JST UART connector on the Waveshare board.
   GND tied to the same JST GND.
4. **Data**: GPS-TX (green) → JST RXD; GPS-RX (white) → JST TXD.
5. **Firmware**: nothing to change. Flash, reset, watch the speed
   gauge's footer go through `NO FIX` → `TRACKING N` → `FIX N SATS`
   as the antenna locks onto satellites.

## What's still unknown

- Whether reception through the center speaker grille is good enough.
  The grille has a metal speaker frame behind it that may attenuate
  signal. **Tomorrow's outdoor test with the BN-880 in that location
  will tell us.** If yes, the puck antenna can sit on the top of the
  dashpad inside the gauge enclosure. If no, the puck has to mount on
  a clear sky-view spot (windshield base, dashpad top, etc.).
- Whether the NEO-7M ships with the SMA antenna pre-attached (the
  small ceramic patch most NEO-7M kits include). Doesn't matter — we'd
  swap it for the puck regardless, just affects what's in the box.
