if(CONFIG_SHIELD_DONGLE_SCREEN)
  zephyr_library()
  zephyr_library_sources(${ZEPHYR_BASE}/misc/empty_file.c)
  zephyr_library_include_directories(${ZEPHYR_LVGL_MODULE_DIR})
  zephyr_library_include_directories(${ZEPHYR_BASE}/lib/gui/lvgl/)
  zephyr_library_include_directories(${ZEPHYR_BASE}/drivers)
  zephyr_library_include_directories(${CMAKE_SOURCE_DIR}/include)
  zephyr_library_include_directories(${ZEPHYR_CURRENT_MODULE_DIR}/include)
  zephyr_library_include_directories(${ZEPHYR_CURRENT_CMAKE_DIR}/include)
  zephyr_library_include_directories(include)
  zephyr_library_sources(src/brightness.c)
  zephyr_library_sources(src/custom_status_screen.c)
  zephyr_library_sources(src/widgets/output_status.c)
  zephyr_library_sources(src/widgets/battery_status.c)
  zephyr_library_sources(src/widgets/layer_status.c)
  zephyr_library_sources(src/widgets/wpm_status.c)
  zephyr_library_sources(src/widgets/mod_status.c)
  zephyr_library_sources_ifdef(CONFIG_DONGLE_SCREEN_EYES_ACTIVE src/widgets/eyes_status.c)
  zephyr_library_sources_ifdef(CONFIG_DONGLE_SCREEN_BACKGROUND_ACTIVE src/widgets/background_status.c)
  # Raw-framebuffer engine the eyes widget draws its shapes into (see
  # helpers/display.h) - background_status.c stayed on real LVGL objects
  # throughout the v0.3 framebuffer port and never called into this, so it
  # rides the same Kconfig option as eyes_status.c rather than being built
  # unconditionally.
  zephyr_library_sources_ifdef(CONFIG_DONGLE_SCREEN_EYES_ACTIVE src/widgets/helpers/display.c)
  file(GLOB font_sources src/fonts/*.c)
  zephyr_library_sources(${font_sources})
endif()