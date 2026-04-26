/*
 * speed_gauge.h -- Firebird GPS ground-speed gauge screen.
 *
 * PHASE 1 (current): placeholder screen, just shows "MPH" and "no
 * GPS fix" so the swipe dispatcher has something to cycle to.
 * set_gauge_speed_mph() exists but only updates a debug label.
 *
 * PHASE 2 (BN-880 wired up): replaced with a full Pontiac-themed
 * speedometer face driven by NMEA $GNRMC sentences over UART.
 * Range 0..120 MPH (or 0..160 if Jeff wants more headroom). No trip
 * setting -- speedo is informational, not safety-critical.
 *
 * Setter is fire-and-forget: callers (GPS task) push every NMEA
 * sentence; if this gauge isn't currently on screen, the call no-ops.
 */
#ifndef SPEED_GAUGE_H
#define SPEED_GAUGE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Build and activate the GPS speed gauge screen. Safe to call
 * repeatedly -- each call builds a fresh screen from scratch and
 * swaps it in. */
void show_gauge_speed(void);

/* Push a fresh ground speed reading (MPH).  Phase 1: updates only the
 * debug value label.  Phase 2: drives a real needle + readout. No-op
 * if the speed gauge is not currently the active screen.  Pass a
 * negative value to indicate "no fix" (the placeholder shows "---"). */
void set_gauge_speed_mph(float mph);

/* Show/hide the GPS-status footer ("NO FIX" / "3D / 2D" / etc.).
 * Phase 2 lets us print HDOP and satellite count here. */
void set_gauge_speed_status(const char *status);

/* Invalidate the speed gauge's widget references. */
void speed_gauge_release(void);

#ifdef __cplusplus
}
#endif

#endif /* SPEED_GAUGE_H */
