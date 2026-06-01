/*
 * speed_gauge.c -- Firebird GPS ground-speed gauge screen.
 *
 * Real Pontiac-themed dial now (was a placeholder).  Mirrors the
 * structure of display_gauge.c so the visual language matches the
 * water-temp gauge: chrome ring, classic numerals 0..120 MPH, baked
 * red band at 100+, "Pontiac" script up top, "MPH" label below the
 * pivot.  LVGL paints the needle, pivot cap, digital readout, and
 * GPS-status footer on top of the static face.
 *
 * Face geometry (must match render_pontiac_speed_face.py):
 *     0 MPH   -> -120 deg (from 12 o'clock, cw positive)
 *    60 MPH  ->    0 deg  (straight up)
 *   120 MPH  -> +120 deg
 *
 * set_gauge_speed_mph(-1) means "no fix" -- needle parks at 0, digital
 * readout shows "---".  Otherwise the value drives both.
 *
 * set_gauge_speed_status() updates the small text below the readout
 * (e.g. "NO FIX", "TRACKING 4", "FIX 7 SATS").  Called from
 * main/GPS/GPS.c.
 */
#include <stdio.h>
#include <string.h>

#include "lvgl.h"
#include "esp_log.h"

#include "speed_gauge.h"
#include "gauge_screen.h"
#include "gauge_face_speed.h"
#include "needle_img.h"           /* same needle as the temp gauge */

static const char *TAG = "SPEED_GAUGE";

/* ------------------------------------------------------------------ */
/* Calibration (must match render_pontiac_speed_face.py)               */
/* ------------------------------------------------------------------ */
#define MPH_START_DEG   -120.0f
#define MPH_SWEEP_DEG    240.0f
#define MPH_MIN            0.0f
#define MPH_MAX          120.0f

/* ------------------------------------------------------------------ */
/* Module state                                                        */
/* ------------------------------------------------------------------ */
static lv_obj_t *s_needle      = NULL;
static lv_obj_t *s_value_lbl   = NULL;  /* big digital MPH readout    */
static lv_obj_t *s_status_lbl  = NULL;  /* small footer: FIX / TRACKING */

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */
static float clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static float mph_to_gauge_deg(float mph)
{
    const float span = MPH_MAX - MPH_MIN;
    return MPH_START_DEG + (mph - MPH_MIN) * MPH_SWEEP_DEG / span;
}

/* ------------------------------------------------------------------ */
/* Public                                                              */
/* ------------------------------------------------------------------ */
void speed_gauge_release(void)
{
    s_needle     = NULL;
    s_value_lbl  = NULL;
    s_status_lbl = NULL;
}

void set_gauge_speed_mph(float mph)
{
    /* Screen not built yet?  GPS task pushes every NMEA tick regardless
     * of which gauge is currently up; we just no-op when not visible. */
    if (s_value_lbl == NULL) return;

    /* Negative MPH = "no fix" sentinel from the GPS module.  Park the
     * needle at 0 and show "---" so the user knows the value isn't
     * stale -- it's just absent. */
    bool no_fix = (mph < 0.0f);
    float painted = no_fix ? 0.0f : clampf(mph, MPH_MIN, MPH_MAX);

    if (s_needle) {
        float angle_deg = mph_to_gauge_deg(painted);
        lv_img_set_angle(s_needle, (int16_t)(angle_deg * 10.0f));
    }

    char buf[8];
    /* Integer-only readout per Jeff: no decimals, and floor anything
     * below 1 MPH to 0 to suppress GPS noise at standstill. */
    if (no_fix) {
        snprintf(buf, sizeof(buf), "---");
    } else if (mph < 1.0f) {
        snprintf(buf, sizeof(buf), "0");
    } else {
        snprintf(buf, sizeof(buf), "%d", (int)(mph + 0.5f));
    }
    lv_label_set_text(s_value_lbl, buf);
}

void set_gauge_speed_status(const char *status)
{
    if (s_status_lbl == NULL || status == NULL) return;
    lv_label_set_text(s_status_lbl, status);
}

/* ------------------------------------------------------------------ */
/* Build the screen                                                    */
/* ------------------------------------------------------------------ */
void show_gauge_speed(void)
{
    ESP_LOGI(TAG, "show_gauge_speed: Pontiac MPH face, 0..120");

    /* FRESH SCREEN PATTERN -- new screen, build, swap with auto_del. */
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_set_size(scr, 480, 480);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(scr, LV_DIR_NONE);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLL_CHAIN);

    /* Touch catcher (swipe + long-press wired by the dispatcher). */
    lv_obj_t *touch = lv_obj_create(scr);
    lv_obj_remove_style_all(touch);
    lv_obj_set_size(touch, 480, 480);
    lv_obj_set_pos(touch, 0, 0);
    lv_obj_set_style_bg_opa(touch, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(touch, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(touch, LV_OBJ_FLAG_SCROLLABLE);
    gauge_screen_install_input(touch);

    /* Static Pontiac MPH face artwork. */
    lv_obj_t *face = lv_img_create(scr);
    lv_img_set_src(face, &gauge_face_speed);
    lv_obj_set_pos(face, 0, 0);

    /* Digital MPH readout -- positioned the same as the temp gauge's
     * digital readout so both gauges feel consistent.  CY+135 puts the
     * baseline below the "MPH" label baked into the artwork. */
    s_value_lbl = lv_label_create(scr);
    lv_label_set_text(s_value_lbl, "---");
    lv_obj_set_style_text_color(s_value_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(s_value_lbl, &lv_font_montserrat_48, 0);
    lv_obj_set_width(s_value_lbl, 120);
    lv_obj_set_style_text_align(s_value_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_value_lbl, LV_ALIGN_CENTER, 0, 135);

    /* Unit suffix "mph" -- lowercase to fit better at this size. */
    lv_obj_t *unit = lv_label_create(scr);
    lv_label_set_text(unit, "mph");
    lv_obj_set_style_text_color(unit, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(unit, &lv_font_montserrat_28, 0);
    lv_obj_align_to(unit, s_value_lbl, LV_ALIGN_OUT_RIGHT_BOTTOM, 4, -6);

    /* Needle: 80x480 image with pivot at (40, 240) -- same asset as the
     * temp gauge.  Pinned so the pivot lands at screen (240, 240). */
    s_needle = lv_img_create(scr);
    lv_img_set_src(s_needle, &needle_img);
    lv_obj_set_pos(s_needle, 200, 0);
    lv_img_set_pivot(s_needle, 40, 240);
    /* Park at 0 until the GPS task pushes a real value. */
    set_gauge_speed_mph(-1.0f);

    /* Pivot cap overlay -- identical to the temp gauge. */
    lv_obj_t *cap = lv_obj_create(scr);
    lv_obj_remove_style_all(cap);
    lv_obj_set_size(cap, 28, 28);
    lv_obj_align(cap, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_radius(cap, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(cap, lv_color_hex(0xD2D2D4), 0);
    lv_obj_set_style_bg_grad_color(cap, lv_color_hex(0x78787C), 0);
    lv_obj_set_style_bg_grad_dir(cap, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(cap, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(cap, 0, 0);
    lv_obj_clear_flag(cap, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(cap, LV_OBJ_FLAG_CLICKABLE);

    /* GPS status footer -- sits below the digital readout in the same
     * spot the temp gauge uses for its CHECK SENSOR banner. Plain
     * text, no flashing -- "FIX 7 SATS" / "TRACKING 4" / "NO FIX". */
    s_status_lbl = lv_label_create(scr);
    lv_label_set_text(s_status_lbl, "NO FIX");
    lv_obj_set_style_text_color(s_status_lbl, lv_color_hex(0xB8B8B8), 0);
    lv_obj_set_style_text_font(s_status_lbl, &lv_font_montserrat_18, 0);
    lv_obj_align(s_status_lbl, LV_ALIGN_CENTER, 0, 185);

    /* Dot indicator -- "you are on screen 3 of 3" */
    gauge_screen_draw_dots(scr, GAUGE_ID_SPEED);

    /* Activate the screen with whatever slide animation the dispatcher
     * queued (LEFT on swipe-next, RIGHT on swipe-prev, NONE on first
     * paint). */
    lv_scr_load_anim(scr, gauge_screen_pop_anim(), 250, 0, true);
}
