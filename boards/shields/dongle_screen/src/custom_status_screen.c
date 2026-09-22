/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "custom_status_screen.h"

#if CONFIG_DONGLE_SCREEN_OUTPUT_ACTIVE
#include "widgets/output_status.h"
static struct zmk_widget_output_status output_status_widget;
#endif

#if CONFIG_DONGLE_SCREEN_LAYER_ACTIVE
#include "widgets/layer_status.h"
static struct zmk_widget_layer_status layer_status_widget;
#endif

#if CONFIG_DONGLE_SCREEN_EYES_ACTIVE
#include "widgets/eyes_status.h"
static struct zmk_widget_eyes_status eyes_status_widget;
#endif

#if CONFIG_DONGLE_SCREEN_BACKGROUND_ACTIVE
#include "widgets/background_status.h"
static struct zmk_widget_background background_widget;
#endif

#if CONFIG_DONGLE_SCREEN_BATTERY_ACTIVE
#include "widgets/battery_status.h"
static struct zmk_widget_dongle_battery_status dongle_battery_status_widget;
#endif

#if CONFIG_DONGLE_SCREEN_WPM_ACTIVE
#include "widgets/wpm_status.h"
static struct zmk_widget_wpm_status wpm_status_widget;
#endif

#if CONFIG_DONGLE_SCREEN_MODIFIER_ACTIVE
#include "widgets/mod_status.h"
static struct zmk_widget_mod_status mod_widget;
#endif

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

lv_style_t global_style;

lv_obj_t *zmk_display_status_screen()
{
    lv_obj_t *screen;

    screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, 255, LV_PART_MAIN);

    lv_style_init(&global_style);
    // lv_style_set_text_font(&global_style, &lv_font_unscii_8); // ToDo: Font is not recognized
    lv_style_set_text_color(&global_style, lv_color_white());
    lv_style_set_text_letter_space(&global_style, 1);
    lv_style_set_text_line_space(&global_style, 1);
    lv_obj_add_style(screen, &global_style, LV_PART_MAIN);

#if CONFIG_DONGLE_SCREEN_BACKGROUND_ACTIVE
    // First, and deliberately so: LVGL draws children in creation order, which
    // is what puts this behind everything else. Nothing in front raises itself
    // above a sibling except the dialogue and the eyes' own cut-out, both of
    // which want to be in front of this anyway.
    zmk_widget_background_init(&background_widget, screen);
    lv_obj_align(zmk_widget_background_obj(&background_widget), LV_ALIGN_CENTER, 0, 0);
    zmk_widget_background_sparkle_burst(&background_widget);
#endif

#if CONFIG_DONGLE_SCREEN_OUTPUT_ACTIVE
    zmk_widget_output_status_init(&output_status_widget, screen);
    // Its own bottom corner rather than sharing a row with the batteries: the
    // panel gained 40px of width, which is enough to give the two their own
    // corners instead of splitting one row between them. Pulled in from the
    // edge so the case lip doesn't clip the profile number.
    lv_obj_align(zmk_widget_output_status_obj(&output_status_widget), LV_ALIGN_BOTTOM_RIGHT, -8, -4);
#endif

#if CONFIG_DONGLE_SCREEN_BATTERY_ACTIVE
    zmk_widget_dongle_battery_status_init(&dongle_battery_status_widget, screen);
    // The opposite corner from the output widget now that the two no longer
    // share a row. The cells only need about 162px of the 240 the widget's own
    // box reserves, so this has slack to spare before it reaches the output
    // widget's corner.
    lv_obj_align(zmk_widget_dongle_battery_status_obj(&dongle_battery_status_widget), LV_ALIGN_BOTTOM_LEFT, 16, 0);
#endif

#if CONFIG_DONGLE_SCREEN_WPM_ACTIVE
    zmk_widget_wpm_status_init(&wpm_status_widget, screen);
    // Top-left corner. Dialogue now lives in the right-hand column rather than
    // a band across the top of the panel (see DIALOGUE_BOTTOM in
    // eyes_status.c), so this no longer sits under a remark the way it did on
    // the 240px panel.
    lv_obj_align(zmk_widget_wpm_status_obj(&wpm_status_widget), LV_ALIGN_TOP_LEFT, 16, 12);
#endif

#if CONFIG_DONGLE_SCREEN_LAYER_ACTIVE
    zmk_widget_layer_status_init(&layer_status_widget, screen);
    lv_obj_align(zmk_widget_layer_status_obj(&layer_status_widget), LV_ALIGN_CENTER, 0, 0);
#endif

#if CONFIG_DONGLE_SCREEN_EYES_ACTIVE
    zmk_widget_eyes_status_init(&eyes_status_widget, screen);
    // The eyes' box is the panel exactly - 320x172 - so it is placed flush and
    // unshifted, and the widget's own constants decide where everything inside
    // it lands. Children are clipped to the box, so any offset here would push
    // the dialogue column or the bottom of the eyes off the panel.
    //
    // The 8px nudge this used to carry centred the face in the case window on
    // the 240px panel, which was mounted off-centre behind a lip. Nothing about
    // the new panel is known yet, so there is no equivalent offset to apply
    // until one is measured against the actual case.
    lv_obj_align(zmk_widget_eyes_status_obj(&eyes_status_widget), LV_ALIGN_CENTER, 0, 0);
#endif

#if CONFIG_DONGLE_SCREEN_MODIFIER_ACTIVE
    zmk_widget_mod_status_init(&mod_widget, screen);
    // There is no offset that clears both neighbours: the eyes reach down to
    // about y=116 and the battery row starts at y=152, a 36px gap this
    // widget's own 40px height cannot fit inside. +46 centres it in that gap
    // anyway, which grazes the eyes' bottom edge by about 4px and just clears
    // the battery row - this widget is placed for a screen with a face-shaped
    // hole in the middle of it, and the README says to turn it off or move it.
    lv_obj_align(zmk_widget_mod_status_obj(&mod_widget), LV_ALIGN_CENTER, 0, 46);
#endif

    return screen;
}