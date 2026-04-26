/*
 * oil_gauge.c -- placeholder oil PSI screen for Phase 1.
 *
 * Same fresh-screen / kill-scroll / btn-flush patterns as the temp
 * gauge so the swipe-cycle UX is identical across all three screens.
 * The only thing missing here is the actual Pontiac face artwork and
 * needle -- those land in Phase 3 once the transducer arrives.
 *
 * Layout (480x480 round panel):
 *
 *       +---------------------+
 *       |                     |
 *       |       OIL PSI       |   <- 48pt title
 *       |                     |
 *       |        ---          |   <- live PSI readout (placeholder)
 *       |                     |
 *       |  Sensor not wired   |   <- 18pt status text
 *       |                     |
 *       |        o O o        |   <- gauge_screen_draw_dots()
 *       +---------------------+
 *
 * The "swipe to cycle, long-press for settings" UX is wired by
 * gauge_screen_install_input() the same way the temp gauge does it.
 */
#include <stdio.h>

#include "lvgl.h"
#include "esp_log.h"

#include "oil_gauge.h"
#include "gauge_screen.h"

static const char *TAG = "OIL_GAUGE";

/* ------------------------------------------------------------------ */
/* Module state                                                        */
/* ------------------------------------------------------------------ */
static lv_obj_t *s_value_lbl = NULL;
static lv_obj_t *s_status    = NULL;

/* ------------------------------------------------------------------ */
/* Public                                                              */
/* ------------------------------------------------------------------ */
void oil_gauge_release(void)
{
    s_value_lbl = NULL;
    s_status    = NULL;
}

void set_gauge_oil_psi(float psi)
{
    if (s_value_lbl == NULL) return;

    char buf[16];
    /* Phase 1: just print whatever the caller pushes; clamp out of
     * range fault values so the layout doesn't blow up if Oil_Sender
     * publishes 1e9 on an open ADS1115 channel. */
    if (psi < -10.0f)  psi = -10.0f;
    if (psi > 200.0f)  psi = 200.0f;
    snprintf(buf, sizeof(buf), "%.0f", psi);
    lv_label_set_text(s_value_lbl, buf);
}

void show_gauge_oil(void)
{
    ESP_LOGI(TAG, "show_gauge_oil (placeholder)");

    /* FRESH SCREEN PATTERN -- new screen, build, swap with auto_del so
     * the previous screen and all its widgets get freed. Same approach
     * as display_gauge.c and settings_screen.c. */
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

    /* Full-screen invisible touch catcher for swipe + long-press. */
    lv_obj_t *touch = lv_obj_create(scr);
    lv_obj_remove_style_all(touch);
    lv_obj_set_size(touch, 480, 480);
    lv_obj_set_pos(touch, 0, 0);
    lv_obj_set_style_bg_opa(touch, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(touch, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(touch, LV_OBJ_FLAG_SCROLLABLE);
    gauge_screen_install_input(touch);

    /* Title -- "OIL PSI" in white Montserrat 48 at top-center. */
    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "OIL PSI");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_48, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 100);

    /* Big live value -- placeholder "---" until Oil_Sender starts
     * pushing real numbers in Phase 3. Same orange as the temp readout
     * so the visual language is consistent across gauges. */
    s_value_lbl = lv_label_create(scr);
    lv_label_set_text(s_value_lbl, "---");
    lv_obj_set_style_text_color(s_value_lbl, lv_color_hex(0xFFB84A), 0);
    lv_obj_set_style_text_font(s_value_lbl, &lv_font_montserrat_48, 0);
    lv_obj_set_width(s_value_lbl, 200);
    lv_obj_set_style_text_align(s_value_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_value_lbl, LV_ALIGN_CENTER, 0, 0);

    /* Status line below -- explicit "placeholder" callout so testers
     * don't think the gauge is broken. Phase 3 swaps this out. */
    s_status = lv_label_create(scr);
    lv_label_set_text(s_status, "Sensor not wired");
    lv_obj_set_style_text_color(s_status, lv_color_hex(0xB8B8B8), 0);
    lv_obj_set_style_text_font(s_status, &lv_font_montserrat_18, 0);
    lv_obj_align(s_status, LV_ALIGN_CENTER, 0, 80);

    /* Dot indicator -- "you are on screen 2 of 3". */
    gauge_screen_draw_dots(scr, GAUGE_ID_OIL_PSI);

    /* Activate, with the slide animation (if any) queued by the
     * dispatcher's last next/prev call. */
    lv_scr_load_anim(scr, gauge_screen_pop_anim(), 250, 0, true);
}
