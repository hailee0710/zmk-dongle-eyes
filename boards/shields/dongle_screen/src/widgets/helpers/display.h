/*
 * Raw RGB565+alpha framebuffer backing the eyes widget only.
 *
 * ZMK v0.3 pins Zephyr v3.5.0, which carries LVGL 8.3 - a different object/
 * style API to the LVGL 9 this shield's widgets were written against (see
 * CLAUDE.md's "The ZMK v0.3 framebuffer port"). Rather than translate every
 * lv_obj/lv_line/lv_canvas call site one for one, the eyes widget draws its
 * shapes into this buffer directly and hands LVGL one pre-baked image to
 * display, the way hailee0710/zmk-pacman-module's own display.c does on the
 * same panel and the same ZMK v0.3 pin. Every other widget (layer/mod/output/
 * wpm/battery, background, dialogue and zzz labels) stays on real lv_label/
 * lv_line/lv_canvas objects - that API is stable across LVGL 8 and 9 (call
 * sites still need individual verification - some function signatures
 * changed, see battery_status.c's LVGL 8 fixes), and porting it would trade a
 * working font/dialogue system for pacman's own bitmap font, which has no
 * lowercase and drops most punctuation.
 *
 * This buffer is sized to the eyes' own bounding box, not the full panel:
 * a first spike with a full 320x172 buffer (110,080 bytes) overflowed RAM
 * by 24,504 bytes on ZMK v0.3 with nothing else drawn yet, which ruled out
 * a full-panel buffer (and, with it, an earlier plan to have the background
 * widget share this buffer - background now stays on LVGL entirely, see
 * background_status.c).
 *
 * Carries a real per-pixel alpha channel (LV_IMG_CF_TRUE_COLOR_ALPHA), not
 * plain LV_IMG_CF_TRUE_COLOR: an opaque image is a solid rectangle, and this
 * box sits over the background effects layer and (on some layers) the layer
 * label, both created earlier in custom_status_screen.c and so meant to show
 * through everywhere the eyes don't actually draw ink. A first pass shipped
 * as plain TRUE_COLOR, which is what an opaque black box the size of this
 * whole buffer looks like against anything behind it - caught by re-reading
 * the LVGL 8.3 draw path (draw/sw/lv_draw_sw_img.c's convert_cb), not by a
 * build. LV_IMG_CF_TRUE_COLOR_ALPHA costs a third byte per pixel
 * (LV_IMG_PX_SIZE_ALPHA_BYTE=3 at LV_COLOR_DEPTH=16) laid out
 * [colour-lo, colour-hi, alpha] - see display.c's fb_px_t.
 */

#pragma once

#include <lvgl.h>
#include <stdint.h>
#include <stdbool.h>

// The eyes' own box: two EYE_W(46)xEYE_H(60) eyes centred EYE_DX(52) either
// side of the midline (eyes_status.c), so the pair spans 2*52+46=150px wide
// by 60px tall at rest. 200x90 rounds that up with margin for gaze offset,
// the wink/blink morphs, and - the tightest of the lot - the confused
// spiral: EYE_DX+its own spread(6) puts each spiral's centre 58px off the
// midline, its own radius (r_max, scaled by openness) reaches another ~31px
// out from there, the wobble sway adds WOBBLE_PX(4) more, and the stroke
// itself is half its own width (SPIRAL uses line_w=6, so 3px) past whatever
// point display_fb_stroke_aa is centred on - 58+31+4+3=96px against this
// buffer's 100px half-width, ~4px to spare. A first pass at 160px (80px
// half-width, 89px worth of the same reach without the wobble/stroke terms
// folded in) clipped the spiral's outer coil, caught by checking the
// expression table's actual numbers rather than guessing from EYE_W/EYE_H
// alone - see CLAUDE.md's "Any new expression that reaches further from
// centre...". At 3 bytes/px that's 54,000 bytes.
#define DISPLAY_FB_W 200
#define DISPLAY_FB_H 90

// Plain white or black - every shape in this widget is drawn white on
// nothing (there is no sclera, so an eye is just its pupil), except the
// twinkle's hole edge, which is smoothed with a thin black stroke - see
// eyes_status.c's SPARK_EDGE_W.
#define DISPLAY_COLOR_WHITE 0xFFFF
#define DISPLAY_COLOR_BLACK 0x0000

// Creates the framebuffer-backed image object as a child of `parent`.
// Returns the image object so the caller can lv_obj_align it the same way
// every other widget in custom_status_screen.c is aligned.
lv_obj_t *display_fb_init(lv_obj_t *parent);

// ---- Drawing primitives (write to the shared framebuffer) ----
// Coordinates are signed: shapes are frequently positioned via
// centre-minus-radius arithmetic (e.g. cx - r) that legitimately goes
// negative for anything near the left/top edge, and clipping that correctly
// needs a real sign rather than wrapping around through an unsigned type.
// Sizes (w/h/radius) stay unsigned - they are never negative.

// Resets every pixel to fully transparent (alpha 0). Widgets call this once
// per redraw before drawing anything - matches apply_geometry()'s own "there
// is no persistent object tree, redraw from scratch every time" approach.
// Takes no colour: a transparent pixel's colour bytes are never read by
// LVGL's draw path, so there is nothing to choose between here, unlike the
// plain-TRUE_COLOR version this replaced, which had to pick an opaque
// background colour to clear to.
void display_fb_clear(void);

// Alpha-composites `color` over whatever is already at (x,y), using
// `coverage` (0-255) as source alpha - the standard Porter-Duff "over"
// operator, not a flat pixel set. This is what makes antialiasing possible:
// everywhere a shape's edge is soft (display_fb_stroke_aa's coverage
// falloff), the pixel there gets blended rather than overwritten.
//
// A pixel that starts fully transparent (alpha 0, e.g. anywhere
// display_fb_clear() left untouched) has no real "current colour" to blend
// from, so the first hit there sets colour and alpha directly rather than
// lerping toward an undefined background. A pixel that is already partially
// covered (this can only happen where two stroked segments meet at a shared
// vertex - see display_fb_stroke_aa) composites the new coverage on top of
// the old the same way, which is what makes two overlapping round joins of
// the same colour combine into the correct union coverage
// (1-(1-a1)(1-a2)) instead of double-painting the same colour twice.
void display_fb_blend_px(int16_t x, int16_t y, uint16_t color, uint8_t coverage);

// Even-odd scanline fill of the polygon `p` traces, `n` points, treated as a
// closed loop (edge i to i+1, wrapping i=n-1 back to 0) regardless of
// whether the caller already repeated the first point as the last - ported
// from eyes_status.c's own fill_polygon(), which this replaces as that
// function's sink changes from an lv_canvas draw buffer to this shared
// buffer. Deliberately hard-edged, not antialiased, exactly like the
// original: the caller strokes the same points on top with
// display_fb_stroke_aa(), and that stroke's own antialiasing covers the
// stepped edges this leaves - two passes doing one job between them, same
// division of labour the LVGL version used between its canvas and its line
// object. `ox`/`oy` offset every point before it's written, so callers don't
// have to bake a placement into the point list themselves. Every filled
// pixel is written fully opaque - see display.c.
void display_fb_fill_polygon(const lv_point_t *p, int n, int32_t ox, int32_t oy,
                             uint16_t color);

// Antialiased stroke along the open polyline `p` (n points, n-1 segments,
// no wraparound - matching lv_line's own semantics, which is why every
// point-builder in eyes_status.c that wants a closed-looking outline already
// repeats its first point as its last rather than relying on this to close
// it). `width` is the full stroke width, centred on the path. Coverage is
// computed per pixel from distance to the nearest point on the nearest
// segment, so caps and joins come out rounded for free - a polyline made of
// round-capped segments is a rounded polyline wherever two segments meet,
// with no separate join case to get right. Matches lv_line's own
// line_rounded=true, which every stroke in the LVGL version turned on.
void display_fb_stroke_aa(const lv_point_t *p, int n, int32_t ox, int32_t oy,
                          int32_t width, uint16_t color);

// Rotates `p` (n points) by `angle_decidegrees` (tenths of a degree, positive
// clockwise, matching lv_obj_set_style_transform_rotation's own units - the
// eyes' expression table carries rotation in the same units for exactly this
// reason) about the pivot `(pivot_x, pivot_y)`, then translates the result so
// that pivot lands at `(place_x, place_y)`. Point-builders work in a box's
// own local coordinates with the box's centre as the natural pivot, same as
// the LVGL version's `lv_obj_set_style_transform_pivot_x/y` calls did; this
// is the fb equivalent of that pivot plus the lv_obj_align() that used to
// follow it, folded into one step since there's no separate "rotate the
// object" and "place the object" here, only points.
void display_fb_rotate_points(lv_point_t *p, int n, int32_t pivot_x, int32_t pivot_y,
                              int32_t angle_decidegrees, int32_t place_x, int32_t place_y);

// Marks the whole buffer dirty and pushes it to the panel. Widgets call this
// once per redraw after any number of the primitives above, not once per
// primitive - matches how eyes_status.c's own fill_polygon already does a
// single lv_obj_invalidate at the end of a shape rather than one per span.
void display_fb_flush(void);
