/*
 * display_gauge.h -- Firebird water temp gauge screen.
 *
 * Single-face Pontiac-themed ole-school gauge:
 *   - 100..260 F full range, 180 F at 12 o'clock
 *   - Baked red warning band 240..260
 *   - Live needle, pivot hub, digital readout, and optional live red
 *     overheat arc drawn by LVGL on top of the face artwork.
 *
 * The red overheat arc follows Settings_GetOverheatTripF() so the visual
 * warning extends from the user's alarm setpoint down to the baked 240 F
 * factory band whenever the trip is set below 240.
 */
#ifndef DISPLAY_GAUGE_H
#define DISPLAY_GAUGE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Build and activate the water-temp gauge screen. Safe to call
 * repeatedly -- each call builds a fresh screen from scratch and swaps
 * it in.
 *
 * In the multi-gauge dispatcher world (gauge_screen.h), this is one of
 * three sibling show_gauge_xxx() entry points; the dispatcher picks
 * which one to call based on the swipe state.  Direct callers (boot
 * splash, settings BACK button) should usually go through
 * gauge_screen_show_current() instead so the user lands back on
 * whichever gauge they were last viewing.  show_gauge_temp() remains
 * exposed for explicit jumps (e.g. an OVERHEAT alarm could force a
 * jump to the temp gauge regardless of current selection). */
void show_gauge_temp(void);

/* Back-compat alias: old call sites used show_gauge() before the
 * multi-gauge refactor. Kept so any out-of-tree branch we forgot to
 * update still compiles, but new code should call either
 * show_gauge_temp() (explicit) or gauge_screen_show_current() (the
 * dispatcher). */
static inline void show_gauge(void) { show_gauge_temp(); }

/* Move the needle to a temperature (Fahrenheit). Clamped to 100..260 F.
 * Also repaints the live digital readout. No-op if the gauge screen is
 * not currently built (e.g. settings is showing). */
void set_gauge_temp_f(float temp_f);

/* Show/hide a flashing red alarm banner with custom message text.
 *   active=true  -> banner visible, flashing at 2 Hz, showing `message`
 *   active=false -> banner hidden (message ignored)
 * Used for BOTH sensor-fault ("CHECK SENSOR") and overheat alarms
 * ("OVERHEAT"). Caller keeps its own state machine and calls this every
 * sample tick; the setter is idempotent when nothing changes. */
void set_gauge_alarm(bool active, const char *message);

/* Invalidate the gauge's widget references. Call this BEFORE destroying
 * the gauge screen (e.g. from the settings screen) so the Temp_Sender
 * task's continuous set_gauge_temp_f() / set_gauge_alarm() calls become
 * no-ops instead of dereferencing freed LVGL objects. */
void gauge_release(void);

#ifdef __cplusplus
}
#endif

#endif /* DISPLAY_GAUGE_H */
