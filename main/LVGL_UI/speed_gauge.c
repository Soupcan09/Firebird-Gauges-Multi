/*
 * speed_gauge.c -- placeholder GPS speed screen for Phase 1.
 *
 * Same construction pattern as oil_gauge.c.  Replaced with a real
 * Pontiac-style speedo in Phase 2 once the BN-880 UART driver is
 * online and producing NMEA-derived ground speeds.
 *
 * Layout (480x480 round panel):
 *
 *       +---------------------+
 *       |                     |
 *       |        MPH          |   <- 48pt title
 *       |                     |
 *       |        ---          |   <- live MPH readout (placeholder)
 *       |                     |
 *       |       NO FIX        |   <- 18pt GPS status text
 *       |                     |
 *       |        o o O        |   <- gauge_screen_draw_dots()
 *       +---------------------+
 */
#include <stdio.h>
#include <string.h>

#include "lvgl.h"
#include "esp_log.h"

#include "speed_gauge.h"
#include "gauge_screen.h"

static const char *TAG = "SPEED_GAUGE";

/* ------------------------------------------------------------------ */
/* Module state                                                        */
/* ------------------------------------------------------------------ */
static lv_obj_t *s_value_lbl  = NULL;
static lv_obj_t *s_status_lbl = NULL;

/* ------------------------------------------------------------------ */
/* Public                                                              */
/* ------------------------------------------------------------------ */
void speed_gauge_release(void)
{
    s_value_lbl  = NULL;
    s_status_lbl = NULL;
}

void set_gauge_speed_mph(float mph)
{
    if (s_value_lbl == NULL) return;

    char buf[16];
    /* Negative MPH = "no fix" sentinel (the GPS task uses this when no
     * RMC/GGA sentence has produced a usable position yet). */
    if (mph < 0.0f) {
        lv_label_set_text(s_value_lbl, "---");
        return;
    }
    if (mph > 199.0f) mph = 199.0f;
    snprintf(buf, sizeof(buf), "%.0f", mph);
    lv_label_set_text(s_value_lbl, buf);
}

void set_gauge_speed_status(const char *status)
{
    if (s_status_lbl == NULL || status == NULL) return;
    lv_label_set_text(s_status_lbl, status);
}

void show_gauge_speed(void)
{
    ESP_LOGI(TAG, "show_gauge_speed (placeholder)");

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

    /* Touch catcher -- swipe + long-press wired by gauge_screen. */
    lv_obj_t *touch = lv_obj_create(scr);
    lv_obj_remove_style_all(touch);
    lv_obj_set_size(touch, 480, 480);
    lv_obj_set_pos(touch, 0, 0);
    lv_obj_set_style_bg_opa(touch, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(touch, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(touch, LV_OBJ_FLAG_SCROLLABLE);
    gauge_screen_install_input(touch);

    /* Title */
    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "MPH");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_48, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 100);

    /* Live MPH readout */
    s_value_lbl = lv_label_create(scr);
    lv_label_set_text(s_value_lbl, "---");
    lv_obj_set_style_text_color(s_value_lbl, lv_color_hex(0xFFB84A), 0);
    lv_obj_set_style_text_font(s_value_lbl, &lv_font_montserrat_48, 0);
    lv_obj_set_width(s_value_lbl, 200);
    lv_obj_set_style_text_align(s_value_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_value_lbl, LV_ALIGN_CENTER, 0, 0);

    /* GPS status footer -- placeholder shows "NO FIX" until the
     * UART driver lands in Phase 2. */
    s_status_lbl = lv_label_create(scr);
    lv_label_set_text(s_status_lbl, "NO FIX");
    lv_obj_set_style_text_color(s_status_lbl, lv_color_hex(0xB8B8B8), 0);
    lv_obj_set_style_text_font(s_status_lbl, &lv_font_montserrat_18, 0);
    lv_obj_align(s_status_lbl, LV_ALIGN_CENTER, 0, 80);

    /* Dot indicator -- screen 3 of 3 */
    gauge_screen_draw_dots(scr, GAUGE_ID_SPEED);

    lv_scr_load_anim(scr, gauge_screen_pop_anim(), 250, 0, true);
}
