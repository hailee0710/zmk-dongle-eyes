# ST7789P3 display driver.
#
# target_sources(app ...) rather than zephyr_library_amend(): amend extends an
# already-existing Zephyr library scope, which is how the old driver worked -
# it was shadowing Zephyr's own in-tree sitronix,st7789v library. There is no
# in-tree sitronix,st7789p3 equivalent to shadow here, so there is no library
# to amend into; this matches the pattern proven working by the author's own
# zmk-pacman-module, which drives the same panel and controller revision.
target_sources(app PRIVATE display_st7789p3.c)
