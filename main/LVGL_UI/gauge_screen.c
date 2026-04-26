/*
 * gauge_screen.c -- multi-gauge dispatcher implementation.
 *
 * Keeps the "which gauge is showing" state and dispatches into the
 * per-gauge show_gauge_xxx() entry points. Also owns the universal
 * input behavior (long-press -> settings, swipe-left/right -> cycle)
 * and the bottom-of-screen dot indicator.
 *
 * Animation direction follows the swipe: swiping left (which conceptually
 * pushes the current screen off to the left and brings the next one in
 * from the right) uses LV_SCR_LOAD_ANIM_OVER_LEFT. Swipe right is the
 * opposite.
 */
#include "gauge_screen.h"

#include "lvgl.h"
#include "esp_log.h"

#include "display_gauge.h"
#include "oil_gauge.h"
#include "speed_gauge.h"
#include "settings_screen.h"

static const char *TAG = "GAUGE_SCREEN";

/* ------------------------------------------------------------------ */
/* State                                                               */
/* ------------------------------------------------------------------ */
static gauge_id_t        s_current     = GAUGE_ID_TEMP;
static lv_scr_load_anim_t s_next_anim  = LV_SCR_LOAD_ANIM_NONE;

/* ------------------------------------------------------------------ */
/* Public                                                              */
/* ------------------------------------------------------------------ */
gauge_id_t gauge_screen_get_current(void) { return s_current; }

void gauge_screen_set(gauge_id_t id)
{
    if (id < 0 || id >= GAUGE_ID_COUNT) id = GAUGE_ID_TEMP;
    s_current = id;
}

void gauge_screen_show_current(void)
{
    ESP_LOGI(TAG, "show gauge id=%d", (int)s_current);
    switch (s_current) {
        case GAUGE_ID_TEMP:    show_gauge_temp();  break;
        case GAUGE_ID_OIL_PSI: show_gauge_oil();   break;
        case GAUGE_ID_SPEED:   show_gauge_speed(); break;
        default:
            s_current = GAUGE_ID_TEMP;
            show_gauge_temp();
            break;
    }
    /* After the first build, subsequent renders should slide unless
     * caller explicitly resets. The next/prev handlers update this
     * before calling us. */
    s_next_anim = LV_SCR_LOAD_ANIM_NONE;
}

void gauge_screen_next(void)
{
    s_current = (gauge_id_t)((s_current + 1) % GAUGE_ID_COUNT);
    s_next_anim = LV_SCR_LOAD_ANIM_OVER_LEFT;
    gauge_screen_show_current();
}

void gauge_screen_prev(void)
{
    s_current = (gauge_id_t)((s_current + GAUGE_ID_COUNT - 1) % GAUGE_ID_COUNT);
    s_next_anim = LV_SCR_LOAD_ANIM_OVER_RIGHT;
    gauge_screen_show_current();
}

/* Each show_gauge_xxx() reads this to know which animation to use on
 * its lv_scr_load_anim() call. After read, it stays NONE for the next
 * first-paint until next/prev sets it again. Declared in
 * gauge_screen.h so all three gauges can call it. */
lv_scr_load_anim_t gauge_screen_pop_anim(void)
{
    lv_scr_load_anim_t a = s_next_anim;
    s_next_anim = LV_SCR_LOAD_ANIM_NONE;
    return a;
}

/* ------------------------------------------------------------------ */
/* Universal input behavior (long-press + swipe)                       */
/* ------------------------------------------------------------------ */
static void universal_long_press_cb(lv_event_t *e)
{
    (void)e;
    /* Long-press on any gauge drops into settings. Same UX as before. */
    show_settings();
}

/* Manual swipe detection.
 *
 * LVGL 8's built-in LV_EVENT_GESTURE was tested but never fires on this
 * Waveshare CST820 + RGB-panel build, even with the touch driver
 * reporting >150 px of horizontal motion in a single press (verified
 * via the 'LVGL: X=...' driver logs and the LV_EVENT_PRESSED handler
 * below). Rather than chase LVGL gesture-limit / lv_conf differences,
 * we track press-start and release positions ourselves and call it a
 * swipe based on the delta. Simpler, more predictable, and works
 * regardless of how the indev/gesture state machine behaves.
 *
 * Threshold = 60 px on a 480 panel ~ 12% width. That's a deliberate
 * one-inch finger swipe, ignored if the user just taps. Vertical motion
 * has to be smaller than horizontal motion for the swipe to count, so
 * accidental diagonal presses + drags don't trigger a screen flip.
 */
#define SWIPE_THRESHOLD_PX  60

static lv_coord_t s_press_x = 0;
static lv_coord_t s_press_y = 0;
static bool       s_press_valid = false;

static void universal_press_cb(lv_event_t *e)
{
    (void)e;
    lv_indev_t *indev = lv_indev_get_act();
    if (indev == NULL) return;
    lv_point_t pt;
    lv_indev_get_point(indev, &pt);
    s_press_x = pt.x;
    s_press_y = pt.y;
    s_press_valid = true;
    ESP_LOGD(TAG, "press at (%d, %d)", (int)pt.x, (int)pt.y);
}

static void universal_release_cb(lv_event_t *e)
{
    (void)e;
    if (!s_press_valid) return;
    s_press_valid = false;

    lv_indev_t *indev = lv_indev_get_act();
    if (indev == NULL) return;
    lv_point_t pt;
    lv_indev_get_point(indev, &pt);

    lv_coord_t dx = pt.x - s_press_x;
    lv_coord_t dy = pt.y - s_press_y;
    lv_coord_t adx = dx < 0 ? -dx : dx;
    lv_coord_t ady = dy < 0 ? -dy : dy;

    ESP_LOGI(TAG, "release: dx=%d dy=%d", (int)dx, (int)dy);

    /* Reject if vertical motion dominates -- user probably just dragged
     * a finger up/down by accident. Reject if the press never moved far
     * enough to count as a swipe (a tap or a long-press). */
    if (adx < SWIPE_THRESHOLD_PX) return;
    if (ady > adx)               return;

    if (dx < 0) {
        ESP_LOGI(TAG, "swipe LEFT -> next gauge");
        gauge_screen_next();
    } else {
        ESP_LOGI(TAG, "swipe RIGHT -> prev gauge");
        gauge_screen_prev();
    }
}

void gauge_screen_install_input(lv_obj_t *touch_catcher)
{
    if (!touch_catcher) return;
    /* Long-press anywhere -> settings (unchanged from the temp-only era). */
    lv_obj_add_event_cb(touch_catcher, universal_long_press_cb,
                        LV_EVENT_LONG_PRESSED, NULL);
    /* Manual swipe detection: stash press start, evaluate delta on
     * release.  See universal_release_cb() for the threshold logic and
     * the comment block above explaining why we don't use LVGL's
     * built-in LV_EVENT_GESTURE. */
    lv_obj_add_event_cb(touch_catcher, universal_press_cb,
                        LV_EVENT_PRESSED,  NULL);
    lv_obj_add_event_cb(touch_catcher, universal_release_cb,
                        LV_EVENT_RELEASED, NULL);
}

/* ------------------------------------------------------------------ */
/* Dot indicator at bottom                                             */
/* ------------------------------------------------------------------ */
/* Draws three small circles centered horizontally near the bottom of
 * the BLACK face (NOT on the chrome ring -- that was the bug at y=460
 * and y=448, where the bottom edge of each dot bled onto the ring).
 *
 * Geometry on the temp face (R_RING_IN = 210 from render_pontiac_face_final.py):
 *   center column x=240: ring inner edge at y = 240 + 210 = 450
 *   x=262 (outer dot):   ring inner edge at y = 240 + sqrt(210^2 - 22^2)
 *                                            ~= 448.8
 * So dot BOTTOM has to clear y=448.  With max dot size 10 (active dot,
 * radius 5), dot center must be at y <= 443.  Y=440 gives a clean 5 px
 * margin below the ring everywhere across the 3-dot row.
 *
 * Y=440 lands just inside the OVERHEAT / CHECK SENSOR fault banner
 * (y=405..445).  When the banner is up the alarm is what matters and
 * obscuring the gauge indicator is fine; when the banner is hidden
 * (~99% of the time) the dots sit cleanly on the black face. */
void gauge_screen_draw_dots(lv_obj_t *parent, gauge_id_t which)
{
    const int Y       = 420;     /* dot row center -- generous margin   */
    const int SPACING = 22;      /* horizontal pixel pitch between dots  */
    const int N       = (int)GAUGE_ID_COUNT;

    /* Center the row of N dots about CX=240. */
    int row_w = (N - 1) * SPACING;
    int x0    = 240 - row_w / 2;

    for (int i = 0; i < N; ++i) {
        bool active = (i == (int)which);
        int  size   = active ? 10 : 8;

        lv_obj_t *dot = lv_obj_create(parent);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, size, size);
        lv_obj_set_pos(dot, x0 + i * SPACING - size / 2, Y - size / 2);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(dot,
            lv_color_hex(active ? 0xFFB84A : 0x404040), 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
    }
}
