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
    // Top-right corner, 4px down from the top edge - the same inset the wpm
    // widget uses on its own corner (see wpm_status.c), which is what lets
    // the two labels share a line. Originally moved here from the
    // bottom-right to stay clear of dialogue, back when a remark sat flush
    // against the panel's bottom edge; dialogue has since moved to dead
    // centre and hides the eyes for as long as it's showing (see
    // DIALOGUE_CENTER_Y in eyes_status.c), so nothing forces this corner any
    // more, but there is no reason to move it back either. Pulled in from the
    // edge so the case lip doesn't clip the profile number.
    lv_obj_align(zmk_widget_output_status_obj(&output_status_widget), LV_ALIGN_TOP_RIGHT, -8, 4);
#endif

#if CONFIG_DONGLE_SCREEN_BATTERY_ACTIVE
    zmk_widget_dongle_battery_status_init(&dongle_battery_status_widget, screen);
    // Back along the bottom edge, flush like before, but the widget's own box
    // now spans the full panel width and anchors each half's cell to its own
    // corner within it (see BAT_MARGIN in battery_status.c) rather than
    // sitting side by side - the left half in the bottom-left corner, the
    // right half in the bottom-right. Flush rather than inset, so the
    // modifier widget's own +46 offset below still clears it exactly as it
    // did before the row was ever moved off this edge.
    lv_obj_align(zmk_widget_dongle_battery_status_obj(&dongle_battery_status_widget), LV_ALIGN_BOTTOM_MID, 0, 0);
#endif

#if CONFIG_DONGLE_SCREEN_WPM_ACTIVE
    zmk_widget_wpm_status_init(&wpm_status_widget, screen);
    // Top-left corner, at the same 4px top inset as the output widget's own
    // top-right placement above - both labels are Fredoka_SemiBold_20 now
    // (see wpm_status.c), so matching insets is enough to put the wpm number
    // and the connection text on the same line without a per-widget fudge.
    lv_obj_align(zmk_widget_wpm_status_obj(&wpm_status_widget), LV_ALIGN_TOP_LEFT, 16, 4);
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