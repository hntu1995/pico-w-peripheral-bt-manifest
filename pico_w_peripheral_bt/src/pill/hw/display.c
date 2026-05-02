/* display.c - SSD1306 OLED presenter (Phase-2)
 *
 * Implements a small, self-contained SSD1306 presenter using the Zephyr
 * display API when an `oled_display` devicetree alias exists. The
 * presenter uses an in-memory page-oriented framebuffer and a tiny 5x7
 * font sufficient to render status lines (A/I, BATT, CONN, LOW).
 *
 * If no display DT alias is present the module falls back to logging.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "pill/display.h"

#if defined(CONFIG_PILL_DISPLAY) && (CONFIG_PILL_DISPLAY == 1)

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(pill_display, LOG_LEVEL_INF);

#if DT_NODE_HAS_STATUS(DT_ALIAS(oled_display), okay)
#define OLED_NODE DT_ALIAS(oled_display)
/* width/height are declared on the overlay node */
#define OLED_WIDTH DT_PROP(OLED_NODE, width)
#define OLED_HEIGHT DT_PROP(OLED_NODE, height)
static uint8_t fb[OLED_WIDTH * (OLED_HEIGHT / 8)];
static const struct device *oled_dev = DEVICE_DT_GET(OLED_NODE);
#else
/* No display alias present — keep stubs and fall back to logging. */
#define OLED_WIDTH 128
#define OLED_HEIGHT 64
#endif

/* Small 5x7 font for digits and a few letters/marks. Each entry is 5
 * columns, LSB = top row. Only characters used by status strings are
 * provided to keep code size small.
 */
#if DT_NODE_HAS_STATUS(DT_ALIAS(oled_display), okay)
static const uint8_t font_5x7_digits[][5] = {
    /* '0' */ {0x3E,0x51,0x49,0x45,0x3E},
    /* '1' */ {0x00,0x42,0x7F,0x40,0x00},
    /* '2' */ {0x42,0x61,0x51,0x49,0x46},
    /* '3' */ {0x21,0x41,0x45,0x4B,0x31},
    /* '4' */ {0x18,0x14,0x12,0x7F,0x10},
    /* '5' */ {0x27,0x45,0x45,0x45,0x39},
    /* '6' */ {0x3C,0x4A,0x49,0x49,0x30},
    /* '7' */ {0x01,0x71,0x09,0x05,0x03},
    /* '8' */ {0x36,0x49,0x49,0x49,0x36},
    /* '9' */ {0x06,0x49,0x49,0x29,0x1E}
};

static const uint8_t font_misc[][5] = {
    /* ':' */ {0x00,0x36,0x36,0x00,0x00},
    /* '%' approx */ {0x62,0x64,0x08,0x13,0x23},
    /* 'A' */ {0x7E,0x11,0x11,0x11,0x7E},
    /* 'B' */ {0x7F,0x49,0x49,0x49,0x36},
    /* 'C' */ {0x3E,0x41,0x41,0x41,0x22},
    /* 'I' */ {0x00,0x41,0x7F,0x41,0x00},
    /* 'L' */ {0x7F,0x40,0x40,0x40,0x40},
    /* ' ' (space) */ {0x00,0x00,0x00,0x00,0x00}
};

/* Helper: set/clear a pixel in page-oriented framebuffer used by SSD1306
 * layout: byte index = x + (y/8)*width, bit = 1 << (y % 8)
 */
static inline void fb_set_pixel(int x, int y, bool on)
{
    if (x < 0 || x >= (int)OLED_WIDTH || y < 0 || y >= (int)OLED_HEIGHT) {
        return;
    }
    size_t idx = x + (y / 8) * OLED_WIDTH;
    uint8_t mask = 1u << (y & 7);
    if (on) {
        fb[idx] |= mask;
    } else {
        fb[idx] &= (uint8_t)~mask;
    }
}

/* Draw a single 5x7 character at top-left (x,y). Characters that are
 * not present are rendered as spaces. This routine is intentionally
 * small and non-reentrant (uses the global fb). Callers must ensure
 * exclusive access if used from multiple contexts.
 */
static void draw_char(int x, int y, char c)
{
    const uint8_t *glyph = NULL;
    int i, col;

    if (c >= '0' && c <= '9') {
        glyph = font_5x7_digits[c - '0'];
    } else if (c == ':') {
        glyph = font_misc[0];
    } else if (c == '%') {
        glyph = font_misc[1];
    } else if (c == 'A') {
        glyph = font_misc[2];
    } else if (c == 'B') {
        glyph = font_misc[3];
    } else if (c == 'C') {
        glyph = font_misc[4];
    } else if (c == 'I') {
        glyph = font_misc[5];
    } else if (c == 'L') {
        glyph = font_misc[6];
    } else if (c == ' ') {
        glyph = font_misc[7];
    } else {
        glyph = font_misc[7]; /* space fallback */
    }

    for (col = 0; col < 5; col++) {
        uint8_t coldata = glyph[col];
        for (i = 0; i < 7; i++) {
            bool on = (coldata >> i) & 1u;
            fb_set_pixel(x + col, y + i, on);
        }
    }
}

/* Draw a NUL-terminated string at given position. Adds one pixel of
 * spacing between glyphs.
 */
static void draw_text(int x, int y, const char *s)
{
    int cx = x;
    while (*s) {
        draw_char(cx, y, *s++);
        cx += 6; /* 5 columns + 1 spacing */
        if (cx + 5 >= (int)OLED_WIDTH) {
            break; /* don't wrap */
        }
    }
}

#endif /* DT_NODE_HAS_STATUS(DT_ALIAS(oled_display), okay) */

/*
 * pill_display_init()
 * Pitfalls: Not reentrant; returns -ENODEV if device not available;
 * framebuffer uses ~1KB static RAM; ensure memory budget fits.
 */
int pill_display_init(void)
{
#if DT_NODE_HAS_STATUS(DT_ALIAS(oled_display), okay)
    if (!device_is_ready(oled_dev)) {
        LOG_WRN("oled device not ready, falling back to logs");
        return -ENODEV;
    }

    (void)memset(fb, 0, sizeof(fb));
    display_blanking_off(oled_dev);
    struct display_buffer_descriptor desc = {
        .buf_size = sizeof(fb),
        .width = OLED_WIDTH,
        .height = OLED_HEIGHT,
        .pitch = OLED_WIDTH,
    };
    int rc = display_write(oled_dev, 0, 0, &desc, fb);
    if (rc != 0) {
        LOG_ERR("display_write failed: %d", rc);
        return rc;
    }
    LOG_INF("pill_display: initialized (%ux%u)", OLED_WIDTH, OLED_HEIGHT);
    return 0;
#else
    LOG_INF("pill_display: no display alias, using log fallback");
    return 0;
#endif
}

/*
 * pill_display_show_status()
 * Pitfalls: Uses a global framebuffer (not thread-safe). If display is
 * missing the function logs the status instead. Keep rendering simple
 * to avoid high CPU cost in tight loops.
 */
void pill_display_show_status(const struct alarm_ctrl_status *status)
{
    if (status == NULL) {
        return;
    }

#if DT_NODE_HAS_STATUS(DT_ALIAS(oled_display), okay)
    if (!device_is_ready(oled_dev)) {
        LOG_WRN("oled device not ready, logging status instead");
        LOG_INF("status: active=%u idx=%u batt=%u%% conn=%u low=%u",
                status->active_alarm,
                status->active_alarm_index,
                status->battery_percent,
                status->connected,
                status->low_battery);
        return;
    }

    /* Clear framebuffer */
    memset(fb, 0, sizeof(fb));

    char line[32];
    /* Line 0: A:<0/1> I:<idx> */
    (void)snprintk(line, sizeof(line), "A:%u I:%u",
                   status->active_alarm,
                   status->active_alarm_index);
    draw_text(0, 0, line);

    /* Line 1: B:<percent>% */
    (void)snprintk(line, sizeof(line), "B:%u%",
                   status->battery_percent);
    /* place on row 16 (below first line) */
    draw_text(0, 16, line);

    /* Line 2: C:<conn> L:<low> */
    (void)snprintk(line, sizeof(line), "C:%u L:%u",
                   status->connected, status->low_battery);
    draw_text(0, 32, line);

    struct display_buffer_descriptor desc = {
        .buf_size = sizeof(fb),
        .width = OLED_WIDTH,
        .height = OLED_HEIGHT,
        .pitch = OLED_WIDTH,
    };
    int rc = display_write(oled_dev, 0, 0, &desc, fb);
    if (rc != 0) {
        LOG_ERR("display_write error: %d", rc);
    }
#else
    /* No display: log a compact status line */
    LOG_INF("display: active=%u idx=%u batt=%u%% conn=%u low=%u",
            status->active_alarm,
            status->active_alarm_index,
            status->battery_percent,
            status->connected,
            status->low_battery);
#endif
}

#endif /* CONFIG_PILL_DISPLAY */
