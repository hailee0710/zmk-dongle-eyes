# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A ZMK Zephyr module providing a dongle status screen for an ST7789P3 172×320 panel on a nice!nano
v2. It is a fork of janpfischer's YADS (`zmk-dongle-screen`), tracking ZMK `main` (Zephyr 4.1,
LVGL 9); upstream's `main` is still on LVGL 8 and will not build against it. The shield, the
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
# From inside a ZMK west workspace with the toolchain set up.
west build -p -s /path/to/zmk/app -d /path/to/build-output/totem_dongle -b "nice_nano@2.0.0//zmk" \
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
    west build -p -s zmk/app -d /module/build -b nice_nano@2.0.0//zmk -- \
      -DZephyr_DIR=/module/zephyr/share/zephyr-package/cmake \
      -DZMK_CONFIG=/testconfig -DSHIELD=dongle_screen -DZMK_EXTRA_MODULES=/module"
```

The explicit `-DZephyr_DIR=...` is load-bearing: without it, `find_package(Zephyr)` fails at
configure time even with `west init`/`west update` having run cleanly and `.west/config` correctly
pointing at `zephyr.base`.

`-DSHIELD` must name a shield that already exists in the `-DZMK_CONFIG` directory. Add
`-S zmk-usb-logging` and `-DCONFIG_LOG_PROCESS_THREAD_STARTUP_DELAY_MS=8000` to see logs.

Pin ZMK to a commit rather than a branch when documenting an install — ZMK moving to Zephyr 4.1
renamed every board and broke working configs.

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
(`rounded_rect`, `clip_below`, `set_*_points`, `fill_polygon`), an `expressions[]` table keyed by
`enum expr_id`, and an `apply_geometry()` that lays the current expression into the pre-created
objects. `resolve()` decides what to show, in priority order: per-layer mapping → idle → WPM level
(squeeze/confused, with hysteresis) → quirk → neutral. Dialogue lives here too: three string lists
grouped by prompt (wake / alert / nag), a priority constant so a lower-ranked line cannot interrupt a
higher one, and reveal/hold/fade animations.

**`background_status.c`** draws the dim atmosphere layer: stress lines scaling with WPM, plus layer
effects (sparkles, drifting punctuation, anger marks) that deliberately **outlive the layer that
summoned them** so a tap gets a full burst instead of a flicker.

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
`boards/nice_nano_nrf52840_zmk_2_0_0.overlay` wires SPI0, the PWM backlight, CS/DC/reset pins and
the panel's own tuning (`madctl`, `colmod`, offsets) for that one board — it is currently the only
board wired up. A new board needs a new overlay, named after the ZMK-qualified board name
(`nice_nano@2.0.0//zmk` → `nice_nano_nrf52840_zmk_2_0_0.overlay`).

**Adding a widget** takes four edits: the `.c`/`.h` pair, a `zephyr_library_sources_ifdef` line in
`boards/shields/dongle_screen/CMakeLists.txt`, a `CONFIG_DONGLE_SCREEN_<NAME>_ACTIVE` bool in
`Kconfig.defconfig`, and creation in `custom_status_screen.c`.

## Constraints that fail silently

- **The bundled 20px font has no uppercase, and no punctuation beyond `!`, `%`, `.` and `?`.** An
  uppercase character renders as *nothing at all* — no error, no warning. Every label must be
  lowercase. Widen the range and regenerate rather than wondering where the text went; the
  `lv_font_conv` commands are in the README's Licensing section.
- Dialogue is clipped past the eyes' box width less its right margin — about 300px on the current
  320px-wide panel, a little under thirty lowercase characters. Line breaks are written into the
  strings by hand, not wrapped; a third line is dropped (`DIALOGUE_MAX_LINES`).
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
- **The eyes' box (`EYES_W`/`EYES_H`) is the panel, exactly** — the widget is placed flush and
  unshifted in `custom_status_screen.c`. Everything the widget draws, including the dialogue and the
  sleep z's, has to fit inside that box; there is no slack above or below it the way the old 240px
  panel's taller-than-the-screen box had. `DIALOGUE_BOTTOM` in `eyes_status.c` is what positions the
  dialogue column and is the constant to retune if the box size changes again.

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

The panel swap above (ST7789V 240×280 → ST7789P3 172×320) has been checked with a real firmware
build — `west build` against ZMK `main` on `nice_nano@2.0.0//zmk` with `dongle_screen` alone
completes cleanly (`FLASH: 54%`, `RAM: 80%` on a minimal test keymap with eyes and background both
active) — but not yet against real hardware. Every resized constant still carries a comment
explaining its derivation, not a measurement off a physical case — expect another pass once a unit
exists to look at.

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
