/*
 * gauge_screen.h -- multi-gauge dispatcher.
 *
 * The Firebird display now hosts three full-screen gauges:
 *
 *     GAUGE_ID_TEMP    -- water temperature  (display_gauge.c)
 *     GAUGE_ID_OIL_PSI -- engine oil pressure (oil_gauge.c)
 *     GAUGE_ID_SPEED   -- GPS ground speed    (speed_gauge.c)
 *
 * Only one gauge screen is built at a time. Swiping left/right anywhere
 * on the dial cycles to the next/previous gauge with a slide animation.
 * Long-press still drops into the settings screen on every gauge.
 *
 * Sender modules (Temp_Sender, Oil_Sender, GPS) push fresh values through
 * each gauge's set_gauge_*() setter. The setters silently no-op when
 * their gauge isn't the current one, so each task can fire-and-forget
 * without caring which screen is showing.
 *
 * Boot default is GAUGE_ID_TEMP (the safety-critical one). The current
 * selection is held in RAM only -- power cycle returns to temp.
 */
#ifndef GAUGE_SCREEN_H
#define GAUGE_SCREEN_H

#include <stdbool.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    GAUGE_ID_TEMP = 0,
    GAUGE_ID_OIL_PSI,
    GAUGE_ID_SPEED,
    GAUGE_ID_COUNT,
} gauge_id_t;

/* Return the gauge that is currently selected (or about to be built). */
gauge_id_t gauge_screen_get_current(void);

/* Build and activate the screen for the current gauge_id. The first call
 * after boot uses LV_SCR_LOAD_ANIM_NONE; subsequent calls (from
 * gauge_screen_next/prev) use a sliding animation matching the swipe
 * direction. */
void gauge_screen_show_current(void);

/* Cycle forward / backward and rebuild the screen. Called by the swipe
 * gesture handler installed by gauge_screen_install_input(). */
void gauge_screen_next(void);
void gauge_screen_prev(void);

/* Jump directly to a specific gauge (used by the boot path; not wired
 * to any UI yet, but useful for testing and possible future "default
 * gauge" setting). */
void gauge_screen_set(gauge_id_t id);

/* Each gauge's show_gauge_xxx() function calls this on its full-screen
 * touch catcher to wire up the universal input behavior:
 *   - long-press anywhere -> settings screen
 *   - swipe left          -> next gauge
 *   - swipe right         -> previous gauge
 *
 * Doing it once here keeps the gesture/long-press wiring identical
 * across all three gauge implementations and means a future fourth gauge
 * just calls this same helper.
 */
void gauge_screen_install_input(lv_obj_t *touch_catcher);

/* Tiny "1 of 3" dot indicator drawn at the bottom of the active gauge.
 * Three small circles centered horizontally near the bottom of the
 * visible circle; the current gauge's dot is filled and slightly larger.
 * Each gauge's show_gauge_xxx() builds it on its screen so the indicator
 * stays anchored above the round-display chord. */
void gauge_screen_draw_dots(lv_obj_t *parent, gauge_id_t which);

/* Pop the queued screen-load animation (set by gauge_screen_next/prev,
 * cleared back to NONE on read). Each show_gauge_xxx() reads this when
 * it calls lv_scr_load_anim() so the slide direction matches the swipe. */
lv_scr_load_anim_t gauge_screen_pop_anim(void);

#ifdef __cplusplus
}
#endif

#endif /* GAUGE_SCREEN_H */
