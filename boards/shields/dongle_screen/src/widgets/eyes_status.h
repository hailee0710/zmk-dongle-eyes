/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <lvgl.h>
#include <zephyr/kernel.h>

// The spiral needs 28. A rounded rectangle traced for the derived shapes needs
// 20, and clipping it can add two more.
#define EYE_MAX_PTS 56

// Lines a single piece of dialogue may occupy. Breaks are written into the
// strings themselves rather than wrapped automatically, so anything past this
// is an authoring mistake and is simply dropped.
//
// Two is what the panel has room for. A plate is 26px and the band above the
// eyes is about 62, which two lines fill exactly once the drift on the way out
// is accounted for. A third would start off the top of the screen.
#define DIALOGUE_MAX_LINES 2

// The eyes themselves are drawn into the shared raw framebuffer (see
// helpers/display.h), not built from lv_obj/lv_line/lv_canvas objects - only
// `fb_img`, the one image object that displays that buffer, lives here.
// Dialogue and the sleep z's stay real LVGL labels, since that API doesn't
// need porting and drawing text into the framebuffer would mean giving up
// this shield's own fonts for pacman's bitmap one (see CLAUDE.md's "The ZMK
// v0.3 framebuffer port").
struct zmk_widget_eyes_status {
    sys_snode_t node;
    lv_obj_t *obj;
    lv_obj_t *fb_img;
    lv_point_t pts[2][EYE_MAX_PTS];
    lv_obj_t *zzz[3];  // drift up and fade once idle has gone on a while
    // One label per line rather than one label with newlines in it, so each
    // line's background hugs its own text like a highlight instead of every
    // line sharing the bounding box of the longest.
    lv_obj_t *dialogue[DIALOGUE_MAX_LINES];

    uint8_t expr;
    uint8_t pending_expr; // expression to adopt at the bottom of a transition

    int16_t openness; // 0-256, scales vertical extent; drives blinks and morphs
    int16_t strain;   // 0-256, how hard a squeeze is currently pushing
    int16_t spin;     // 0-359, rotation of the confused spiral
    int16_t shake;    // horizontal shudder, applied to both eyes together
    int16_t wob;      // 0-359, slow drift phase for the confused spirals
    int16_t gaze_x;
    int16_t gaze_y;
    bool idle;

    // Set by the per-tick animation callbacks (openness/strain/spin/wob/
    // shake/gaze), cleared by eyes_status.c's own redraw timer once it has
    // actually redrawn - see EYES_REDRAW_MS. LVGL's animation system steps
    // every ~10ms, far faster than the panel is ever sampled (BLINK_CLOSE_MS
    // etc put that at 80ms), so calling apply_geometry() straight from each
    // callback redrew the same frame eight times over for one that was ever
    // shown, and doing it from as many as two independent animations at once
    // (confused's spin+wob, squeezed's strain+shake) doubled that again. The
    // redraw timer coalesces however many of those landed since its last
    // tick into the one redraw that actually mattered.
    bool geom_dirty;
};

int zmk_widget_eyes_status_init(struct zmk_widget_eyes_status *widget, lv_obj_t *parent);
lv_obj_t *zmk_widget_eyes_status_obj(struct zmk_widget_eyes_status *widget);
