# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A ZMK Zephyr module providing a dongle status screen for an ST7789P3 172×320 panel on a nice!nano
v2. It is a fork of janpfischer's YADS (`zmk-dongle-screen`), tracking ZMK `v0.3` (Zephyr
`v3.5.0+zmk-fixes`, LVGL 8.3) — see "The ZMK v0.3 framebuffer port" below for why that pin and not
`main`; upstream's own `main` is on LVGL 8 already and unaffected by any of this. The shield, the
brightness handling and the output/layer/mod/WPM/battery widgets are upstream's work. Added here:
an animated eyes widget with dialogue, and a background effects layer.

The panel itself is a later swap: this screen originally drove an ST7789V 240×280 panel (rotated to
280×240 landscape) via a patched copy of Zephyr's in-tree driver. It now drives an ST7789P3
172×320 panel instead, via a driver and devicetree wiring carried over from the author's own
[zmk-pacman-module](https://github.com/rayreside/zmk-pacman-module). The LVGL widget architecture
did not change — only the driver, the devicetree, and every widget's geometry constants did. If you
find a reference to the old panel or to `xiao_ble`, it is stale; only nice!nano v2 is wired up now.

Everything on screen is built from LVGL primitives — rounded bars, `lv_line` polylines and canvases.
There are no image assets.

## Building

There is no CI and no linter. The only real verification is a firmware build through a ZMK west
workspace, and the only runnable script is the expression renderer. Neither `west` nor a Zephyr SDK
is expected to be installed locally — use ZMK's own Docker build image (`zmkfirmware/zmk-build-arm`)
rather than trying to install the toolchain.

```sh
# From inside a ZMK west workspace with the toolchain set up. nice_nano_v2, not
# nice_nano@2.0.0//zmk - ZMK v0.3 predates Zephyr's Hardware Model v2 board@revision
# syntax, so the plain pre-HWMv2 board name is what v0.3 expects.
west build -p -s /path/to/zmk/app -d /path/to/build-output/totem_dongle -b nice_nano_v2 \
  -- -DZMK_CONFIG=/path/to/zmk-config/config \
     -DSHIELD="totem_dongle dongle_screen" \
     -DZMK_EXTRA_MODULES=/path/to/zmk-dongle-eyes/

# Regenerate the expression contact sheet the README uses (stdlib Python only).
# Writes docs/_preview/*.svg (gitignored) and docs/images/expressions.svg.
python docs/render_expressions.py
```

**Standalone build via Docker, with no real keyboard config to point at** — this shield has no
keyboard matrix of its own (it is display-only), so `dongle_screen` alone still needs a keymap and
a zero-key mock kscan to satisfy ZMK core, matched to `boards/shields/settings_reset` in the ZMK
tree for the pattern. `west init -l` needs a git repo to treat as the manifest, so if working from
an uncommitted checkout, commit it into a throwaway git repo first (`west init`'s `.west/` cannot be
created on a read-only mount, and modern git refuses to operate in a repo it doesn't own by UID -
`git config --global --add safe.directory '*'` inside the container fixes that):

```sh
docker pull zmkfirmware/zmk-build-arm:stable

# ZMK_CONFIG needs a matching <shield-name>.keymap/.conf/.overlay - dongle_screen.keymap with
# #include <behaviors.dtsi> / <dt-bindings/zmk/keys.h> and one `&kp` binding, dongle_screen.conf
# with the widget options to exercise, and a dongle_screen.overlay providing a 1-key
# `zmk,kscan-mock` (columns=1, rows=0) as `zmk,kscan` - see settings_reset.overlay for the shape.

docker run --rm -v /path/to/workspace:/module -v /path/to/test-config:/testconfig \
  zmkfirmware/zmk-build-arm:stable bash -c "
    git config --global --add safe.directory '*' &&
    cd /module && west init -l config && west update &&
    west build -p -s zmk/app -d /module/build -b nice_nano_v2 -- \
      -DZephyr_DIR=/module/zephyr/share/zephyr-package/cmake \
      -DZMK_CONFIG=/testconfig -DSHIELD=dongle_screen -DZMK_EXTRA_MODULES=/module"
```

`/path/to/workspace` needs a `config` subdirectory that's this module's own checkout (its
`config/west.yml` declares `self: {path: config}`) - the module doubles as both the Zephyr module
(`-DZMK_EXTRA_MODULES=/module`) and, through `config/`, the west manifest repo, so `west init -l
config` run from `/module` finds it there. Run this against a scratch copy, not the real working
tree: `west update` populates `/module/zmk` and `/module/zephyr` as siblings of `config`, and `west
init` writes a `.west/` into `config` itself - both land wherever `config` actually points, so a
direct bind-mount of the real checkout gets a multi-hundred-MB ZMK+Zephyr tree, and a stray
root-owned `.west/`, written straight into it.

The explicit `-DZephyr_DIR=...` is load-bearing: without it, `find_package(Zephyr)` fails at
configure time even with `west init`/`west update` having run cleanly and `.west/config` correctly
pointing at `zephyr.base`.

`-DSHIELD` must name a shield that already exists in the `-DZMK_CONFIG` directory. Add
`-S zmk-usb-logging` and `-DCONFIG_LOG_PROCESS_THREAD_STARTUP_DELAY_MS=8000` to see logs.

Pin ZMK to a tag or commit rather than a branch when documenting an install — ZMK moving to Zephyr
4.1 renamed every board and broke working configs, which is part of why this module tracks the
`v0.3` tag rather than `main`.

## Architecture

**Entry point.** `boards/shields/dongle_screen/src/custom_status_screen.c` defines
`zmk_display_status_screen()`, which ZMK's display module calls. Each widget is compiled in
conditionally, created, and positioned there with `lv_obj_align`. **LVGL draws children in creation
order, so the order in that function is z-order** — the background is created first on purpose.

**Widget shape.** Every widget is a `.c`/`.h` pair under `src/widgets/` exposing
`zmk_widget_<name>_init()` and `zmk_widget_<name>_obj()`, holding a struct of pre-created `lv_obj_t *`
in its header (objects are created once and shown/hidden, never created on the display thread).

**State flow.** Widgets subscribe with `ZMK_DISPLAY_WIDGET_LISTENER(...)` plus
`ZMK_SUBSCRIPTION(...)` for `zmk_layer_state_changed`, `zmk_wpm_state_changed`,
`zmk_activity_state_changed`. A `*_get_state()` function reads current state from the ZMK APIs, and
a `*_update_cb()` reacts to it. `eyes_status.c` and `background_status.c` each carry layer and WPM in
one state struct so both reach LVGL through one callback.

**Threading rule — do not break this.** `ZMK_DISPLAY_WIDGET_LISTENER` marshals onto the display work
queue; that macro is the only thing making LVGL calls safe. A raw `ZMK_LISTENER` runs synchronously
on whichever thread raised the event (the keymap thread for layer changes) and must never call
`lv_*`. Per-widget `lv_timer`s are fine — they run on the display thread.
`brightness.c` legitimately uses a raw listener because it touches only a message queue and GPIO.

**`eyes_status.c`** (the largest file) holds all expression geometry: point builders
(`rounded_rect`, `clip_below`, `set_*_points`), an `expressions[]` table keyed by `enum expr_id`, and
an `apply_geometry()` that draws the current expression into the small raw-RGB565 framebuffer
`helpers/display.c` owns (see "The ZMK v0.3 framebuffer port" below) rather than into LVGL objects -
the point-builders themselves are unchanged pure geometry, only what consumes their output moved.
`resolve()` decides what to show, in priority order: per-layer mapping → idle → WPM level
(squeeze/confused, with hysteresis) → quirk → neutral. Dialogue lives here too, still on real LVGL
`lv_label` objects: three string lists grouped by prompt (wake / alert / nag), a priority constant so
a lower-ranked line cannot interrupt a higher one, and reveal/hold/fade animations.

**`background_status.c`** draws the dim atmosphere layer: stress lines scaling with WPM, plus layer
effects (sparkles, drifting punctuation, anger marks) that deliberately **outlive the layer that
summoned them** so a tap gets a full burst instead of a flicker. Stayed on real LVGL objects through
the v0.3 framebuffer port below - only `eyes_status.c` moved.

**`brightness.c`** owns the PWM backlight, idle timeout, brightness/toggle keycodes, and the
optional APDS9960 ambient light sensor. `battery_status.c` calls
`brightness_wake_screen_on_reconnect()`; that is the only cross-widget coupling.

**`drivers/display/`** is a driver of its own, not a fork of a Zephyr in-tree one — there is no
in-tree `sitronix,st7789p3` compat to shadow. It implements `display_driver_api` directly, reading
`madctl`/`colmod`/`inversion`/`x-offset`/`y-offset`/`width`/`height` from devicetree; its binding is
`dts/bindings/display/sitronix,st7789p3.yaml`. **Orientation is baked into `madctl`, not runtime**:
`set_orientation()` accepts only `DISPLAY_ORIENTATION_NORMAL` and rejects everything else with
`-ENOTSUP`. Flipping the panel is an overlay edit (see the `madctl` comment in the board overlay),
not a rebuild.

**Device tree.** `dongle_screen.overlay` selects the display as `zephyr,display`.
`boards/nice_nano_v2.overlay` wires SPI3, the PWM backlight, CS/DC/reset pins and the panel's own
tuning (`madctl`, `colmod`, offsets) for that one board — it is currently the only board wired up. A
new board needs a new overlay, named after the ZMK-qualified board name - on this module's ZMK
`v0.3` pin, that's the plain pre-Hardware-Model-v2 name (`nice_nano_v2`, not `nice_nano@2.0.0//zmk`;
see "The ZMK v0.3 framebuffer port" below for why v0.3, and Building above for what breaks if this
file's name and a `-b` flag disagree on which board-naming era they're in).

The same overlay disables `i2c0` (and with it the ambient-light sensor,
`CONFIG_DONGLE_SCREEN_AMBIENT_LIGHT`'s hardware) — not a hardware change, a RAM one: see "The ZMK
v0.3 framebuffer port" below.

**Adding a widget** takes four edits: the `.c`/`.h` pair, a `zephyr_library_sources_ifdef` line in
`boards/shields/dongle_screen/CMakeLists.txt`, a `CONFIG_DONGLE_SCREEN_<NAME>_ACTIVE` bool in
`Kconfig.defconfig`, and creation in `custom_status_screen.c`.

## The ZMK v0.3 framebuffer port

This module tracks ZMK `v0.3` (Zephyr `v3.5.0+zmk-fixes`, LVGL 8.3), pinned for stability rather than
riding `main` — `main` moving to Zephyr 4.1 has already broken working configs once (see Building
above). LVGL 8.3 doesn't have the `lv_obj`/`lv_line`/`lv_canvas` object/style API `eyes_status.c` was
originally written against on `main`/LVGL 9, so rather than translate every call site, the eyes
widget's shapes draw straight into a small raw RGB565 framebuffer instead - the same approach
`hailee0710/zmk-pacman-module` uses on the same panel and the same ZMK v0.3 pin, whose driver and
devicetree binding turned out to be near-line-identical to this module's own (no porting needed
there, only the rendering layer). `background_status.c` and every other widget stayed on real LVGL
objects - only `eyes_status.c` moved, and even then only its shapes: dialogue and the sleep z's are
still `lv_label` objects, since that API is stable across LVGL 8/9 and porting it would trade this
module's own fonts and lowercase-safe dialogue for pacman's bitmap font, which has neither.

**`boards/shields/dongle_screen/src/widgets/helpers/display.c`/`.h`** own the framebuffer: a static
array of `struct fb_px_t` (`{lo, hi, a}` - RGB565 plus a real per-pixel alpha byte, not a plain
`uint16_t`, see below) bound to one `lv_img_dsc_t`/`lv_img_create()` object, created as a plain child
of `eyes_status`'s own `lv_obj_t` (not `lv_layer_top()`, unlike pacman's version - this module still
has other real LVGL objects around it that need ordinary z-order to work). Five primitives write to
it: `display_fb_clear()` (resets every pixel to fully transparent), `display_fb_fill_polygon()`
(even-odd scanline fill, deliberately hard-edged - ported near-verbatim from `eyes_status.c`'s own
pre-port `fill_polygon()`, which drew into an `lv_canvas` the same way), `display_fb_stroke_aa()`
(antialiased stroke, coverage from distance-to-segment on capsule-shaped segments, which is what
makes the joins and caps come out rounded without a separate case for them), `display_fb_blend_px()`
(the real alpha compositing `display_fb_stroke_aa()`'s coverage rides on - Porter-Duff "over", not a
flat pixel set), and `display_fb_rotate_points()` (rotates a point array about a pivot then places
it, the fb equivalent of LVGL's transform-rotation-plus-align). `apply_geometry()` in
`eyes_status.c` calls these instead of `lv_line_set_points()`/`lv_obj_set_style_*`; the
point-builders that feed it are otherwise unchanged.

The image is `LV_IMG_CF_TRUE_COLOR_ALPHA`, not the plain `LV_IMG_CF_TRUE_COLOR` a first pass shipped
with - a plain-TRUE_COLOR image is opaque by construction, which made the whole 200×90 box a solid
rectangle sitting over whatever `custom_status_screen.c` drew earlier at that spot (the background
effects layer everywhere, the layer label on layers with no expression mapped to them), regardless
of whether the current expression actually put ink there. Caught by reading LVGL 8.3's own sw image
draw path (`draw/sw/lv_draw_sw_img.c`'s `convert_cb`), not by a build - an opaque box the exact size
of the widget's own bounding box isn't the kind of thing a FLASH/RAM report notices. The real alpha
channel costs a third byte per pixel (54,000 bytes total, up from 36,000) but means a pixel
`display_fb_clear()` left untouched stays genuinely see-through, and `display_fb_blend_px()`
composites onto whatever's really behind it rather than onto a fixed background colour.

**The framebuffer is sized to the eyes' own box (`DISPLAY_FB_W`/`DISPLAY_FB_H` in `display.h`), not
the full panel.** A full 320×172 buffer overflowed RAM by 24,504 bytes on a real build with *nothing
else drawn yet* - the number that ruled out an earlier plan to have `background_status.c` share it
too. The eyes' pair spans 150×60px at rest (two `EYE_W`(46)×`EYE_H`(60) eyes centred `EYE_DX`(52)
either side of the midline), but the confused expression's spiral reaches furthest from centre of
anything in the table: its centre sits `EYE_DX+spread`=58px off the midline, its own radius reaches
another ~31px out from there, the wobble sway adds `WOBBLE_PX`(4) more, and the stroke drawn along it
reaches another ~3px past that (half of its 6px `line_w`) - 58+31+4+3=96px against this buffer's
100px half-width, ~4px to spare. Caught by checking the expression table's actual numbers, not by
looking at the picture, since there's no hardware yet to look at it on. `DISPLAY_FB_W` is 200 to give
that margin. **Any new expression that reaches further from centre than the existing table needs
this checked by hand again** — there is no clipping indicator, an expression that overflows the
buffer just silently loses whatever part of it fell outside.

**`apply_geometry()` redraws are coalesced, not called straight from every animation tick.** LVGL's
own animation system steps every ~10ms - far more often than the panel is ever actually sampled
(`BLINK_CLOSE_MS`'s own comment puts that at 80ms) - and up to two of the six per-tick callbacks
(openness/strain/spin/wob/shake/gaze) run at once for some expressions (confused's spin+wob,
squeezed's strain+shake), which used to mean two full redraws every 10ms for one that was ever shown.
Each callback now only updates its own field and sets `widget->geom_dirty`; a dedicated
`EYES_REDRAW_MS`-period `lv_timer` (`eyes_status.c`'s `redraw_timer_cb`) does the actual
`apply_geometry()` call, and only when something is actually dirty. This changes nothing about what
ends up on screen - LVGL already coalesces repeated `lv_obj_invalidate()` calls into one flush - only
how often the framebuffer's own geometry gets recomputed to produce it.

**Ambient light sensing is disabled** (`&i2c0 { status = "disabled"; }` in
`boards/nice_nano_v2.overlay`) for the same RAM budget, not for a hardware reason: a real build
showed the `i2c0` peripheral driver's own RAM cost is tied to the devicetree node being `"okay"`, not
to whether `CONFIG_DONGLE_SCREEN_AMBIENT_LIGHT` is on - so the node had a real RAM cost sitting there
disabled by Kconfig alone. `CONFIG_DONGLE_SCREEN_AMBIENT_LIGHT` itself now depends on that node's
devicetree status (`Kconfig.defconfig`), so turning it on without also re-enabling the node is a
normal "config not available" rather than a link error against a `struct device` that was never
instantiated. Re-enable the node once there's enough headroom to re-measure against and the feature
is actually wanted - see `Kconfig.defconfig`'s own `LV_Z_VDB_SIZE` comment for the current numbers.

A handful of LVGL 9 → 8 API renames surfaced only once each affected file actually compiled or linked
against v0.3, not from inspection - none of them were anticipated going in: `lv_point_precise_t`
doesn't exist in LVGL 8 (use `lv_point_t`, same `{x,y}` shape); `lv_anim_delete()` is `lv_anim_del()`;
`lv_obj_remove_flag()` is `lv_obj_clear_flag()`; `lv_anim_completed_cb_t`/`lv_anim_set_completed_cb()`
are `lv_anim_ready_cb_t`/`lv_anim_set_ready_cb()`; `lv_timer_get_user_data()` doesn't exist -
`lv_timer_t::user_data` is a plain struct field; `lv_display_get_horizontal/vertical_resolution()`
are `lv_disp_get_hor_res()`/`lv_disp_get_ver_res()` (`lv_display_t` itself is `lv_disp_t`);
`lv_canvas_set_px()` takes no `lv_opa_t` argument; `lv_canvas_set_buffer()` takes the `lv_img_cf_t`
enum (`LV_IMG_CF_TRUE_COLOR`), not LVGL 9's `LV_COLOR_FORMAT_RGB565`. A ZMK-core rename showed up the
same way, unrelated to LVGL: `zmk_endpoint_get_selected()` on `main` is `zmk_endpoints_selected()` on
v0.3, same returned struct. Expect more of these if any other file starts getting real changes rather
than just a rebuild - the pattern is reliable enough to check for first, before assuming a build
failure is something else.

## Constraints that fail silently

- **The bundled 20px font has no uppercase, and no punctuation beyond `!`, `%`, `.` and `?`.** An
  uppercase character renders as *nothing at all* — no error, no warning. Every label must be
  lowercase. Widen the range and regenerate rather than wondering where the text went; the
  `lv_font_conv` commands are in the README's Licensing section.
- Dialogue is centred on the eyes' box and clipped past its full width — about 320px on the
  current 320px-wide panel, a little over thirty lowercase characters. Line breaks are written
  into the strings by hand, not wrapped; a third line is dropped (`DIALOGUE_MAX_LINES`).
- **Layer mapping lives in Kconfig, not source, and defaults to none** — layer indices mean whatever
  your keymap says. Layer 0 is never read (base-layer behaviour is activity-driven) and layers past
  7 are unmapped.
- **Adding an expression or effect is a coordinated edit.** For an expression: the `enum expr_id`,
  the `expressions[]` table, `BUILD_ASSERT(EXPR_COUNT == 13, ...)` in `eyes_status.c`, the
  `range 0 12` on the `DONGLE_SCREEN_LAYER_<n>_EXPRESSION` options in `Kconfig.defconfig`, and the id
  table in the README. Reordering the enum silently changes the meaning of every existing config.
  Background effects have the same guard (`BUILD_ASSERT(BG_EFFECT_ANGER == 3, ...)`).
- `docs/render_expressions.py` **duplicates the geometry constants** from the top of
  `eyes_status.c` and ports the point builders by hand. Change one, change the other, then rerun it
  to refresh the contact sheet.
- **A new or resized expression can silently overflow `helpers/display.h`'s `DISPLAY_FB_W`/
  `DISPLAY_FB_H`.** The eyes framebuffer is sized to the expression table's actual worst-case reach
  (see "The ZMK v0.3 framebuffer port" above for the arithmetic and the margin that's already spent),
  not to the panel or to `EYE_W`/`EYE_H` alone. There is no clipping warning - a shape that reaches
  past the buffer's edge just loses whatever fell outside it, silently, the same way an uppercase
  character in a label does.
- **The eyes' box (`EYES_W`/`EYES_H`) is the panel, exactly** — the widget is placed flush and
  unshifted in `custom_status_screen.c`. Everything the widget draws, including the dialogue and the
  sleep z's, has to fit inside that box; there is no slack above or below it the way the old 240px
  panel's taller-than-the-screen box had. `DIALOGUE_BOTTOM` in `eyes_status.c` positions the dialogue
  column and `ZZZ_TOP` positions the sleep z's independently of it - both are the constants to retune
  if the box size changes again.

## Style

The house style is dense explanatory comment blocks that say *why* a value is what it is — what was
tried, what it looked like, what it broke. Match that where you touch code; a bare constant is a
regression here. Comments are in prose (British spelling), not caveman.

Commit messages are a single sentence describing the outcome, no `type:` prefix.

The screen layout is tuned by eye against the actual case, so pixel offsets in
`custom_status_screen.c` are not fully derivable — but where they are, the derivation and the
overlap it leaves are written in a comment beside the offset, not left implicit. The layer, mod and
WPM widgets still sit where an older, taller-panel dashboard wanted them and cut into the face's own
space — the README tells users to turn them off or move them.

The panel swap above (ST7789V 240×280 → ST7789P3 172×320) was checked with a real firmware build
while this module still tracked ZMK `main` (`FLASH: 54%`, `RAM: 80%` on a minimal test keymap with
eyes and background both active) — that pin is gone now (see "The ZMK v0.3 framebuffer port" above),
but the panel geometry itself carried over unchanged. The v0.3 framebuffer port's first pass had its
own real build check on the same minimal test keymap: `FLASH: 47.57%`, `RAM: 81.70%`. A follow-up
review pass moved the eyes framebuffer to a real per-pixel alpha channel (it was hiding the
background layer behind an opaque box - see "The ZMK v0.3 framebuffer port") and turned
`LV_Z_VDB_SIZE` down from 50 to 25 (`Kconfig.defconfig`'s own comment on it has the arithmetic) to
pay for that and then some; the same build check on the same keymap now reads `FLASH: 47.60%`,
`RAM: 67.58%` - the alpha channel's own +18,000 bytes included, `LV_Z_VDB_SIZE` still ends up a net
win by a wide margin. Neither has been checked against real hardware yet. Every resized constant
still carries a comment explaining its derivation, not a measurement off a physical case — expect
another pass once a unit exists to look at, and expect the eyes' own antialiasing/blend quality
specifically to need a look once there's a picture to judge it by, not just a linker report.

Two bugs only a standalone build surfaces, both fixed:
- **`spi0` and `i2c0` are the same physical block on the nRF52840** (`0x40003000` in
  `nrf52840.dtsi`). zmk-pacman-module's own overlay puts the display on `spi0` because it has no
  `i2c0` node of its own; this module's ambient-light sensor does, so the display has to be on a
  peripheral instance that doesn't collide - `spi3`, which is also what the ST7789V wiring this
  replaced used. Enabling both `&spi0` and `&i2c0` at once does not fail loudly at configure time;
  it fails at link with an undefined devicetree-ordinal symbol.
- **Nothing in this module ever explicitly set `CONFIG_SPI=y`.** GPIO, PWM and LED all get a
  `default y` in `Kconfig.defconfig`; SPI never did, for no evident reason. Real builds never
  noticed because their own keyboard shield usually turns SPI on already; a from-scratch standalone
  build of just `dongle_screen` does not have that luck. Fixed by setting it explicitly in
  `dongle_screen.conf`, alongside the other values that shield already forces there.
