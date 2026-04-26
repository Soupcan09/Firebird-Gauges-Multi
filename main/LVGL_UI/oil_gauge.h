/*
 * oil_gauge.h -- Firebird engine oil pressure gauge screen.
 *
 * PHASE 1 (current): placeholder screen. Just shows "OIL PSI" and a
 * "no sensor wired" message so the swipe dispatcher has something to
 * cycle to. set_gauge_oil_psi() exists but only updates a debug label.
 *
 * PHASE 3 (transducer arrives): replaced with a full Pontiac-themed
 * dial driven by the OTUAYAUTO 100 PSI 5 V transducer through an
 * ADS1115 channel. Range 0..100 PSI, low-pressure warning band on the
 * left of the dial (red below ~15 PSI at idle).
 *
 * Setter is fire-and-forget: callers (Oil_Sender) push every sample
 * tick; if this gauge isn't currently on screen, the call no-ops.
 */
#ifndef OIL_GAUGE_H
#define OIL_GAUGE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Build and activate the oil PSI gauge screen. Safe to call repeatedly
 * -- each call builds a fresh screen from scratch and swaps it in. */
void show_gauge_oil(void);

/* Push a fresh oil pressure reading. Phase 1: updates only the debug
 * value label.  Phase 3: drives a real needle + readout. No-op if the
 * oil gauge is not currently the active screen. */
void set_gauge_oil_psi(float psi);

/* Invalidate the oil gauge's widget references. Call this BEFORE
 * destroying the oil gauge screen (e.g. on swipe-away or on settings
 * entry) so any background sender task's continuous setter calls
 * become no-ops instead of dereferencing freed LVGL objects. */
void oil_gauge_release(void);

#ifdef __cplusplus
}
#endif

#endif /* OIL_GAUGE_H */
