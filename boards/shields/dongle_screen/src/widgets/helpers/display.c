/*
 * See display.h for why this exists.
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include <math.h>

#include "display.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

// [colour-lo, colour-hi, alpha] per pixel - the layout LVGL 8.3's own sw
// image draw path expects for LV_IMG_CF_TRUE_COLOR_ALPHA at
// LV_COLOR_DEPTH=16 (src/draw/sw/lv_draw_sw_img.c's convert_cb reads
// `*src_tmp8 + (*(src_tmp8+1)<<8)` for the colour and the byte after it for
// alpha), not a separate colour-plane-plus-alpha-plane split - the two have
// to be physically interleaved because LVGL reads this struct's `data`
// pointer as one flat byte stream, not two.
//
// 200 x 90 x 3 bytes = 54,000 bytes - see display.h for why the box is sized
// the way it is, and why this needs a third byte per pixel that the
// plain-TRUE_COLOR version this replaced didn't.
struct __packed fb_px_t {
    uint8_t lo, hi, a;
};
static struct fb_px_t fb[DISPLAY_FB_W * DISPLAY_FB_H];
static lv_obj_t *fb_img;
static lv_img_dsc_t fb_dsc;

// CONFIG_LV_COLOR_16_SWAP=y (Kconfig.defconfig) exists so the ST7789P3
// driver can write LVGL's own render buffer straight to SPI with no
// per-pixel swap - LVGL's colour pipeline applies that swap itself, and
// every ordinary lv_color_t in this build (e.g. LV_COLOR_CHROMA_KEY, or
// anything built through lv_color_hex()/lv_color_make()) already carries
// its bytes in that swapped order from the point it's constructed. This
// buffer bypasses LVGL's own colour pipeline (it reaches LVGL as a
// pre-baked TRUE_COLOR_ALPHA image, its bytes copied through verbatim), so
// every plain RGB565 value from the DISPLAY_COLOR_* constants has to be
// swapped here, at the point it's actually stored, to land in that same
// order - otherwise every colour reaches the panel byte-reversed. Carried
// over from zmk-pacman-module's display.c, which hits the same driver the
// same way.
static inline uint16_t wire_color(uint16_t rgb565) {
    return (uint16_t)((rgb565 >> 8) | (rgb565 << 8));
}

// Writes `w` opaque pixels of `color` starting at (x,y), clipped to the
// buffer. display_fb_fill_polygon()'s only caller, for its scanline spans -
// the pixel/line/circle helpers this buffer inherited from
// zmk-pacman-module's own display.c had no caller left here and were
// dropped rather than updated for the new per-pixel alpha byte.
static inline void fb_hline(int16_t x, int16_t y, int16_t w, uint16_t color) {
    if (y < 0 || y >= DISPLAY_FB_H || w <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (x >= DISPLAY_FB_W || w <= 0) return;
    if (x + w > DISPLAY_FB_W) w = DISPLAY_FB_W - x;
    uint16_t wc = wire_color(color);
    struct fb_px_t *dst = &fb[y * DISPLAY_FB_W + x];
    for (int16_t i = 0; i < w; i++) {
        dst[i].lo = (uint8_t)wc;
        dst[i].hi = (uint8_t)(wc >> 8);
        dst[i].a = 0xFF;
    }
}

void display_fb_clear(void) {
    // All-zero bytes is fully transparent (alpha 0) regardless of what the
    // colour bytes happen to be, so there is no wire_color() to compute
    // here - unlike the plain-TRUE_COLOR version this replaced, which had
    // to fill every pixel with an actual opaque background colour.
    memset(fb, 0, sizeof(fb));
}

void display_fb_blend_px(int16_t x, int16_t y, uint16_t color, uint8_t coverage) {
    if (x < 0 || x >= DISPLAY_FB_W || y < 0 || y >= DISPLAY_FB_H || coverage == 0) {
        return;
    }

    struct fb_px_t *px = &fb[y * DISPLAY_FB_W + x];
    uint16_t wc = wire_color(color);

    if (coverage == 255 || px->a == 0) {
        // Either a full hit, or the first thing to ever touch this pixel
        // since the last clear - either way there is no real "current
        // colour" worth blending toward (a coverage=255 write should just
        // be exact, and an alpha=0 pixel's colour bytes are stale from
        // whatever the buffer last held, not a colour that means anything).
        // Alpha becomes `coverage` outright rather than composited against
        // 0, which is the same result 0 + (255-0)*coverage/255 would give,
        // just without the arithmetic.
        px->lo = (uint8_t)wc;
        px->hi = (uint8_t)(wc >> 8);
        px->a = (coverage == 255) ? 0xFF : coverage;
        return;
    }

    // Standard Porter-Duff "over": composite `color`@coverage onto whatever
    // is already here. In this widget every call into this function within
    // one apply_geometry() pass uses the same `color` for a given pixel (a
    // fill and its own smoothing stroke are always the same colour; the two
    // strokes that ever disagree - the twinkle's white outer edge and black
    // hole edge - never reach the same pixel), so this is never asked to mix
    // two different hues; its job is purely to combine two coverage values
    // of the same colour into their union, which is what stops the two
    // round joins at a shared vertex (see display_fb_stroke_aa) from
    // painting that vertex's fringe twice.
    uint16_t cur = (uint16_t)(px->lo | (px->hi << 8));
    uint16_t cur_rgb = wire_color(cur); // back to plain (unswapped) RGB565 to do arithmetic on
    int32_t cr = (cur_rgb >> 11) & 0x1F, cg = (cur_rgb >> 5) & 0x3F, cb = cur_rgb & 0x1F;
    int32_t tr = (color >> 11) & 0x1F, tg = (color >> 5) & 0x3F, tb = color & 0x1F;
    int32_t r = cr + ((tr - cr) * coverage) / 255;
    int32_t g = cg + ((tg - cg) * coverage) / 255;
    int32_t b = cb + ((tb - cb) * coverage) / 255;
    uint16_t blended = wire_color((uint16_t)((r << 11) | (g << 5) | b));

    px->lo = (uint8_t)blended;
    px->hi = (uint8_t)(blended >> 8);
    // 255 - (255-old)*(255-coverage)/255, i.e. 1-(1-a1)(1-a2) scaled to a
    // byte - the union of the old coverage and the new, not their sum.
    px->a = (uint8_t)(255 - ((255 - px->a) * (255 - coverage)) / 255);
}

void display_fb_fill_polygon(const lv_point_t *p, int n, int32_t ox, int32_t oy,
                             uint16_t color) {
    if (n < 3) {
        return;
    }

    int32_t y_top = DISPLAY_FB_H - 1, y_bot = 0;
    for (int i = 0; i < n; i++) {
        int32_t y = (int32_t)p[i].y + oy;
        if (y < y_top) y_top = y;
        if (y > y_bot) y_bot = y;
    }
    y_top = MAX(y_top, 0);
    y_bot = MIN(y_bot, DISPLAY_FB_H - 1);

    // Scanline crossings per row. A rounded rectangle's 20 points cross at
    // most a handful of edges per row; the twinkle's 54-point two-contour
    // shape is the widest user of this and still stays well under it.
#define DISPLAY_FB_FILL_MAX_X 16
    for (int32_t y = y_top; y <= y_bot; y++) {
        int32_t xs[DISPLAY_FB_FILL_MAX_X];
        int cnt = 0;

        for (int i = 0; i < n && cnt < DISPLAY_FB_FILL_MAX_X; i++) {
            int j = (i + 1) % n;
            int32_t y0 = (int32_t)p[i].y + oy, y1 = (int32_t)p[j].y + oy;
            if ((y0 <= y && y1 > y) || (y1 <= y && y0 > y)) {
                int32_t x0 = (int32_t)p[i].x + ox, x1 = (int32_t)p[j].x + ox;
                xs[cnt++] = x0 + ((y - y0) * (x1 - x0)) / (y1 - y0);
            }
        }

        for (int a = 1; a < cnt; a++) {
            int32_t v = xs[a];
            int b = a - 1;
            while (b >= 0 && xs[b] > v) { xs[b + 1] = xs[b]; b--; }
            xs[b + 1] = v;
        }

        for (int k = 0; k + 1 < cnt; k += 2) {
            fb_hline((int16_t)MAX(xs[k], 0), (int16_t)y,
                     (int16_t)(MIN(xs[k + 1], DISPLAY_FB_W - 1) - MAX(xs[k], 0) + 1), color);
        }
    }
#undef DISPLAY_FB_FILL_MAX_X
}

// Squared distance from (px,py) to the nearest point on the segment
// (x0,y0)-(x1,y1). Split out from the old point_segment_dist() so
// stroke_capsule can compare against squared thresholds and only pay for
// the real sqrtf on the ~1px-wide band of pixels where the answer actually
// depends on it - see stroke_capsule.
static float point_segment_dist2(float px, float py, float x0, float y0, float x1, float y1) {
    float dx = x1 - x0, dy = y1 - y0;
    float len2 = dx * dx + dy * dy;
    float t = len2 > 0.0f ? ((px - x0) * dx + (py - y0) * dy) / len2 : 0.0f;
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    float ex = px - (x0 + t * dx), ey = py - (y0 + t * dy);
    return ex * ex + ey * ey;
}

// One round-capped segment's worth of coverage, blended straight into the
// buffer. display_fb_stroke_aa calls this once per segment; consecutive
// capsules sharing an endpoint is what gives the whole polyline rounded
// joins without a separate join case (see display.h).
static void stroke_capsule(float x0, float y0, float x1, float y1, float half_w, uint16_t color) {
    int32_t minx = (int32_t)floorf(fminf(x0, x1) - half_w - 1);
    int32_t maxx = (int32_t)ceilf(fmaxf(x0, x1) + half_w + 1);
    int32_t miny = (int32_t)floorf(fminf(y0, y1) - half_w - 1);
    int32_t maxy = (int32_t)ceilf(fmaxf(y0, y1) + half_w + 1);
    minx = MAX(minx, 0);
    miny = MAX(miny, 0);
    maxx = MIN(maxx, DISPLAY_FB_W - 1);
    maxy = MIN(maxy, DISPLAY_FB_H - 1);

    // Coverage only actually varies with distance in a 1px-wide band
    // straddling half_w (full at half_w-0.5, zero at half_w+0.5, same as
    // before); everywhere else the answer is a flat 0 or 1 and the real
    // (square-rooted) distance was never needed to know that. Comparing
    // squared distance against these two squared thresholds first, and only
    // calling sqrtf for the pixels that actually fall in the band, is the
    // same output for a fraction of the sqrtf calls - most of a capsule's
    // bounding box is solidly inside or solidly outside it, especially for
    // the wider fills (lidded/angry/twinkle at line_w=7) this runs on every
    // animation tick.
    const float inner = half_w - 0.5f;
    const float inner2 = inner > 0.0f ? inner * inner : 0.0f;
    const float outer2 = (half_w + 0.5f) * (half_w + 0.5f);

    for (int32_t y = miny; y <= maxy; y++) {
        for (int32_t x = minx; x <= maxx; x++) {
            // Sampled at the pixel centre, matching how the fill's own
            // integer coordinates already treat a pixel as occupying
            // [x,x+1)x[y,y+1) - a whole-integer distance test here would
            // sample the pixel's corner instead and skew every edge by half
            // a pixel against the fill it's meant to sit flush with.
            float dist2 = point_segment_dist2(x + 0.5f, y + 0.5f, x0, y0, x1, y1);
            if (dist2 >= outer2) {
                continue; // solidly outside the stroke - no coverage at all
            }
            if (dist2 <= inner2) {
                display_fb_blend_px((int16_t)x, (int16_t)y, color, 255); // solidly inside
                continue;
            }
            float coverage = half_w + 0.5f - sqrtf(dist2);
            coverage = coverage < 0.0f ? 0.0f : (coverage > 1.0f ? 1.0f : coverage);
            if (coverage > 0.0f) {
                display_fb_blend_px((int16_t)x, (int16_t)y, color, (uint8_t)(coverage * 255.0f));
            }
        }
    }
}

void display_fb_stroke_aa(const lv_point_t *p, int n, int32_t ox, int32_t oy,
                          int32_t width, uint16_t color) {
    if (n < 2 || width < 1) {
        return;
    }
    float half_w = width / 2.0f;
    for (int i = 0; i + 1 < n; i++) {
        stroke_capsule((float)(p[i].x + ox), (float)(p[i].y + oy), (float)(p[i + 1].x + ox),
                       (float)(p[i + 1].y + oy), half_w, color);
    }
}

void display_fb_rotate_points(lv_point_t *p, int n, int32_t pivot_x, int32_t pivot_y,
                              int32_t angle_decidegrees, int32_t place_x, int32_t place_y) {
    if (angle_decidegrees == 0) {
        // The common case - every expression currently ships with rot=0 - is
        // a pure translation, so it skips the trig entirely rather than
        // doing a rotate-by-zero that would round-trip every point through
        // sinf/cosf for no change.
        for (int i = 0; i < n; i++) {
            p[i].x = (lv_coord_t)((int32_t)p[i].x - pivot_x + place_x);
            p[i].y = (lv_coord_t)((int32_t)p[i].y - pivot_y + place_y);
        }
        return;
    }

    // LVGL's transform_rotation is in tenths of a degree, positive clockwise
    // in screen space (y down) - which is a positive mathematical angle in
    // screen coordinates already, so no sign flip is needed against sinf/cosf.
    float rad = (float)angle_decidegrees * (3.14159265f / 1800.0f);
    float s = sinf(rad), c = cosf(rad);

    for (int i = 0; i < n; i++) {
        float x = (float)((int32_t)p[i].x - pivot_x);
        float y = (float)((int32_t)p[i].y - pivot_y);
        float rx = x * c - y * s;
        float ry = x * s + y * c;
        p[i].x = (lv_coord_t)lroundf(rx + (float)place_x);
        p[i].y = (lv_coord_t)lroundf(ry + (float)place_y);
    }
}

void display_fb_flush(void) {
    lv_obj_invalidate(fb_img);
}

lv_obj_t *display_fb_init(lv_obj_t *parent) {
    // All-zero is fully transparent - see display_fb_clear().
    memset(fb, 0, sizeof(fb));

    fb_dsc.header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA;
    fb_dsc.header.w = DISPLAY_FB_W;
    fb_dsc.header.h = DISPLAY_FB_H;
    fb_dsc.data_size = sizeof(fb);
    fb_dsc.data = (const uint8_t *)fb;

    // A plain child of `parent`, not lv_layer_top(): zmk-pacman-module puts
    // its zone images there because it owns the whole screen and nothing
    // else is drawn with LVGL. Here the peripheral widgets (output/battery/
    // wpm/layer/mod) and the dialogue/zzz labels stay real lv_obj_t
    // siblings, so this has to sit in the ordinary child list where
    // eyes_status's own object does today, or their creation-order z-order
    // (documented in custom_status_screen.c) breaks.
    fb_img = lv_img_create(parent);
    lv_img_set_src(fb_img, &fb_dsc);

    LOG_INF("Eyes framebuffer ready (%dx%d, %u bytes)", DISPLAY_FB_W, DISPLAY_FB_H,
            (unsigned)sizeof(fb));

    return fb_img;
}
