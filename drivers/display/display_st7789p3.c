/*
 * Copyright (c) 2024
 * SPDX-License-Identifier: MIT
 *
 * ST7789P3 display driver, 320x172 landscape.
 *
 * Implements Zephyr's display_driver_api directly, so Zephyr's LVGL glue can
 * drive the panel through display_get_capabilities()/display_write() on the
 * chosen zephyr,display device. That is why this is a driver of its own rather
 * than a fork of Zephyr's in-tree sitronix,st7789v: the P3 revision's init
 * sequence, gamma tables and MADCTL handling differ, and there is no in-tree
 * equivalent to shadow.
 *
 * Panel-specific tuning is all devicetree: madctl, colmod, inversion, x/y
 * offsets and dimensions. Orientation is one of them - it is swept into MADCTL,
 * so there is no runtime set_orientation() and flipping the screen is a
 * devicetree edit rather than a rebuild.
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/display.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/logging/log.h>

#include "display_st7789p3.h"

#define DT_DRV_COMPAT sitronix_st7789p3

LOG_MODULE_REGISTER(display_st7789p3, CONFIG_DISPLAY_LOG_LEVEL);

/* ST7789 commands */
#define ST7789_NOP      0x00
#define ST7789_SWRESET  0x01
#define ST7789_SLPIN    0x10
#define ST7789_SLPOUT   0x11
#define ST7789_NORON    0x13
#define ST7789_INVOFF   0x20
#define ST7789_INVON    0x21
#define ST7789_DISPOFF  0x28
#define ST7789_DISPON   0x29
#define ST7789_CASET    0x2A
#define ST7789_RASET    0x2B
#define ST7789_RAMWR    0x2C
#define ST7789_MADCTL   0x36
#define ST7789_COLMOD   0x3A
#define ST7789_PORCTRL  0xB2
#define ST7789_GCTRL    0xB7
#define ST7789_VCOMS    0xBB
#define ST7789_LCMCTRL  0xC0
#define ST7789_VDVVRHEN 0xC2
#define ST7789_VRHS     0xC3
#define ST7789_VDVS     0xC4
#define ST7789_FRCTRL2  0xC6
#define ST7789_PVGAMCTRL 0xE0
#define ST7789_NVGAMCTRL 0xE1

/* MADCTL bits */
#define MADCTL_MY  0x80
#define MADCTL_MX  0x40
#define MADCTL_MV  0x20
#define MADCTL_ML  0x10
#define MADCTL_BGR 0x08
#define MADCTL_MH  0x04

struct st7789p3_config {
    struct spi_dt_spec spi;
    struct gpio_dt_spec reset_gpio;
    struct gpio_dt_spec dc_gpio;
    struct gpio_dt_spec bl_gpio;
    uint16_t width;
    uint16_t height;
    uint8_t madctl;
    uint8_t colmod;
    bool inversion;
    uint8_t x_offset;
    uint8_t y_offset;
};

/* Send command byte */
static inline void st7789p3_write_cmd(const struct device *dev, uint8_t cmd) {
    const struct st7789p3_config *config = dev->config;
    struct spi_buf buf = { .buf = &cmd, .len = 1 };
    struct spi_buf_set buf_set = { .buffers = &buf, .count = 1 };
    gpio_pin_set_dt(&config->dc_gpio, 0);
    spi_write_dt(&config->spi, &buf_set);
}

/* Send data bytes */
static inline void st7789p3_write_data(const struct device *dev, const uint8_t *data, size_t len) {
    const struct st7789p3_config *config = dev->config;
    struct spi_buf buf = { .buf = (void *)data, .len = len };
    struct spi_buf_set buf_set = { .buffers = &buf, .count = 1 };
    gpio_pin_set_dt(&config->dc_gpio, 1);
    spi_write_dt(&config->spi, &buf_set);
}

static inline void st7789p3_write_data8(const struct device *dev, uint8_t d) {
    st7789p3_write_data(dev, &d, 1);
}

/* Set the address window, shifted by the panel's own offsets */
static void st7789p3_set_window(const struct device *dev, uint16_t xs, uint16_t xe,
                                 uint16_t ys, uint16_t ye) {
    const struct st7789p3_config *config = dev->config;
    uint8_t data[4];

    xs += config->x_offset;
    xe += config->x_offset;
    ys += config->y_offset;
    ye += config->y_offset;

    data[0] = (xs >> 8) & 0xFF; data[1] = xs & 0xFF;
    data[2] = (xe >> 8) & 0xFF; data[3] = xe & 0xFF;
    st7789p3_write_cmd(dev, ST7789_CASET);
    st7789p3_write_data(dev, data, 4);

    data[0] = (ys >> 8) & 0xFF; data[1] = ys & 0xFF;
    data[2] = (ye >> 8) & 0xFF; data[3] = ye & 0xFF;
    st7789p3_write_cmd(dev, ST7789_RASET);
    st7789p3_write_data(dev, data, 4);
}

/* Hardware reset */
static void st7789p3_reset(const struct device *dev) {
    const struct st7789p3_config *config = dev->config;
    if (config->reset_gpio.port == NULL) return;
    /* reset-gpios is GPIO_ACTIVE_LOW; gpio_pin_set_dt() takes logical values,
     * so logical 1 asserts reset and logical 0 releases it. */
    gpio_pin_set_dt(&config->reset_gpio, 1);
    k_msleep(10);
    gpio_pin_set_dt(&config->reset_gpio, 0);
    k_msleep(120);
}

/* Init sequence for the P3 revision in landscape */
static void st7789p3_init_seq(const struct device *dev) {
    const struct st7789p3_config *config = dev->config;

    st7789p3_write_cmd(dev, ST7789_SWRESET);
    k_msleep(150);

    st7789p3_write_cmd(dev, ST7789_SLPOUT);
    k_msleep(120);

    /* 16-bit color (RGB565) */
    st7789p3_write_cmd(dev, ST7789_COLMOD);
    st7789p3_write_data8(dev, config->colmod);
    k_msleep(10);

    /* MADCTL: landscape via MV=1 */
    st7789p3_write_cmd(dev, ST7789_MADCTL);
    st7789p3_write_data8(dev, config->madctl);

    /* Porch control */
    st7789p3_write_cmd(dev, ST7789_PORCTRL);
    { uint8_t d[] = {0x0C, 0x0C, 0x00, 0x33, 0x33}; st7789p3_write_data(dev, d, 5); }

    /* Gate control */
    st7789p3_write_cmd(dev, ST7789_GCTRL);
    st7789p3_write_data8(dev, 0x35);

    /* VCOMS */
    st7789p3_write_cmd(dev, ST7789_VCOMS);
    st7789p3_write_data8(dev, 0x19);

    /* LCM control */
    st7789p3_write_cmd(dev, ST7789_LCMCTRL);
    st7789p3_write_data8(dev, 0x2C);

    /* VDV/VRH enable */
    st7789p3_write_cmd(dev, ST7789_VDVVRHEN);
    st7789p3_write_data8(dev, 0x01);
    k_msleep(1);

    /* VRH */
    st7789p3_write_cmd(dev, ST7789_VRHS);
    st7789p3_write_data8(dev, 0x12);

    /* VDV */
    st7789p3_write_cmd(dev, ST7789_VDVS);
    st7789p3_write_data8(dev, 0x20);

    /* Frame rate */
    st7789p3_write_cmd(dev, ST7789_FRCTRL2);
    st7789p3_write_data8(dev, 0x0F);

    /* Positive gamma */
    st7789p3_write_cmd(dev, ST7789_PVGAMCTRL);
    { uint8_t d[] = {0xD0,0x04,0x0D,0x11,0x13,0x2B,0x3F,0x54,0x4C,0x18,0x0D,0x0B,0x1F,0x23};
      st7789p3_write_data(dev, d, 14); }

    /* Negative gamma */
    st7789p3_write_cmd(dev, ST7789_NVGAMCTRL);
    { uint8_t d[] = {0xD0,0x04,0x0C,0x11,0x13,0x2C,0x3F,0x44,0x51,0x2F,0x1F,0x1F,0x20,0x23};
      st7789p3_write_data(dev, d, 14); }

    st7789p3_write_cmd(dev, config->inversion ? ST7789_INVON : ST7789_INVOFF);
    k_msleep(10);

    st7789p3_write_cmd(dev, ST7789_NORON);
    k_msleep(10);

    st7789p3_write_cmd(dev, ST7789_DISPON);
    k_msleep(120);
}

/* display_driver_api: write() */
static int st7789p3_write(const struct device *dev, const uint16_t x, const uint16_t y,
                           const struct display_buffer_descriptor *desc, const void *buf) {
    if (desc->pitch != desc->width) {
        LOG_ERR("Unsupported pitch %d (width %d)", desc->pitch, desc->width);
        return -ENOTSUP;
    }

    size_t buf_size = (size_t)desc->width * desc->height * 2;
    if (desc->buf_size < buf_size) {
        LOG_ERR("Buffer too small (%zu < %zu)", desc->buf_size, buf_size);
        return -EINVAL;
    }

    st7789p3_set_window(dev, x, x + desc->width - 1, y, y + desc->height - 1);
    st7789p3_write_cmd(dev, ST7789_RAMWR);

    /* CONFIG_LV_COLOR_16_SWAP=y makes LVGL hand us big-endian-on-wire RGB565
     * already, so this goes out in one bulk transfer with no per-pixel swap. */
    st7789p3_write_data(dev, buf, buf_size);

    return 0;
}

/* display_driver_api: blanking_on()/blanking_off() */
static int st7789p3_blanking_on(const struct device *dev) {
    st7789p3_write_cmd(dev, ST7789_DISPOFF);
    return 0;
}

static int st7789p3_blanking_off(const struct device *dev) {
    st7789p3_write_cmd(dev, ST7789_DISPON);
    return 0;
}

/* display_driver_api: get_capabilities() */
static void st7789p3_get_capabilities(const struct device *dev,
                                       struct display_capabilities *caps) {
    const struct st7789p3_config *config = dev->config;

    memset(caps, 0, sizeof(*caps));
    caps->x_resolution = config->width;
    caps->y_resolution = config->height;
    caps->supported_pixel_formats = PIXEL_FORMAT_RGB_565;
    caps->current_pixel_format = PIXEL_FORMAT_RGB_565;
    /* Already landscape by MADCTL, so this is the only orientation there is. */
    caps->current_orientation = DISPLAY_ORIENTATION_NORMAL;
    caps->screen_info = 0;
}

/* display_driver_api: set_pixel_format() */
static int st7789p3_set_pixel_format(const struct device *dev,
                                      const enum display_pixel_format format) {
    return (format == PIXEL_FORMAT_RGB_565) ? 0 : -ENOTSUP;
}

/* display_driver_api: set_orientation() */
/* No runtime rotation: the panel is landscape by MADCTL (see the file header).
 * Saying so is better than accepting a request that would do nothing. */
static int st7789p3_set_orientation(const struct device *dev,
                                     const enum display_orientation orientation) {
    return (orientation == DISPLAY_ORIENTATION_NORMAL) ? 0 : -ENOTSUP;
}

static const struct display_driver_api st7789p3_api = {
    .blanking_on = st7789p3_blanking_on,
    .blanking_off = st7789p3_blanking_off,
    .write = st7789p3_write,
    .read = NULL,
    .get_framebuffer = NULL,
    .set_brightness = NULL,
    .set_contrast = NULL,
    .get_capabilities = st7789p3_get_capabilities,
    .set_pixel_format = st7789p3_set_pixel_format,
    .set_orientation = st7789p3_set_orientation,
};

/* Init */
static int st7789p3_init(const struct device *dev) {
    const struct st7789p3_config *config = dev->config;
    LOG_INF("Initializing ST7789P3 (%dx%d landscape)", config->width, config->height);

    if (!spi_is_ready_dt(&config->spi)) { LOG_ERR("SPI not ready"); return -ENODEV; }

    if (config->reset_gpio.port) gpio_pin_configure_dt(&config->reset_gpio, GPIO_OUTPUT_INACTIVE);
    if (config->dc_gpio.port)     gpio_pin_configure_dt(&config->dc_gpio, GPIO_OUTPUT_INACTIVE);
    if (config->bl_gpio.port)     gpio_pin_configure_dt(&config->bl_gpio, GPIO_OUTPUT_INACTIVE);

    st7789p3_reset(dev);
    st7789p3_init_seq(dev);

    if (config->bl_gpio.port) gpio_pin_set_dt(&config->bl_gpio, 1);

    LOG_INF("ST7789P3 landscape ready");
    return 0;
}

#define ST7789P3_DEVICE(id) \
    static const struct st7789p3_config st7789p3_config_##id = { \
        .spi = SPI_DT_SPEC_INST_GET(id, SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | \
                                          SPI_TRANSFER_MSB | SPI_MODE_CPOL | SPI_MODE_CPHA, 0), \
        .reset_gpio = GPIO_DT_SPEC_INST_GET_OR(id, reset_gpios, {0}), \
        .dc_gpio = GPIO_DT_SPEC_INST_GET_OR(id, dc_gpios, {0}), \
        .bl_gpio = GPIO_DT_SPEC_INST_GET_OR(id, bl_gpios, {0}), \
        .width = DT_INST_PROP_OR(id, width, ST7789_WIDTH), \
        .height = DT_INST_PROP_OR(id, height, ST7789_HEIGHT), \
        .madctl = DT_INST_PROP_OR(id, madctl, MADCTL_MV), \
        .colmod = DT_INST_PROP_OR(id, colmod, 0x55), \
        .inversion = DT_INST_PROP_OR(id, inversion, 0), \
        .x_offset = DT_INST_PROP_OR(id, x_offset, 0), \
        .y_offset = DT_INST_PROP_OR(id, y_offset, 0), \
    }; \
    DEVICE_DT_INST_DEFINE(id, st7789p3_init, NULL, NULL, \
                          &st7789p3_config_##id, POST_KERNEL, CONFIG_DISPLAY_INIT_PRIORITY, \
                          &st7789p3_api);

DT_INST_FOREACH_STATUS_OKAY(ST7789P3_DEVICE)
