/* leds.c - Weekday LED indicators (Phase-2)
 *
 * Uses devicetree aliases `weekday-led0`..`weekday-led6` configured in the
 * board overlay. If aliases are missing the module logs activity instead.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "pill/leds.h"

#if defined(CONFIG_PILL_WEEKDAY_LEDS) && (CONFIG_PILL_WEEKDAY_LEDS == 1)

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(pill_leds, LOG_LEVEL_INF);

/* Build an array of GPIO specs. If an alias is missing the macro returns
 * an empty spec with .port == NULL which we use to skip configuration.
 */
static const struct gpio_dt_spec weekday_leds[7] = {
    GPIO_DT_SPEC_GET_OR(DT_ALIAS(weekday_led0), gpios, {0}),
    GPIO_DT_SPEC_GET_OR(DT_ALIAS(weekday_led1), gpios, {0}),
    GPIO_DT_SPEC_GET_OR(DT_ALIAS(weekday_led2), gpios, {0}),
    GPIO_DT_SPEC_GET_OR(DT_ALIAS(weekday_led3), gpios, {0}),
    GPIO_DT_SPEC_GET_OR(DT_ALIAS(weekday_led4), gpios, {0}),
    GPIO_DT_SPEC_GET_OR(DT_ALIAS(weekday_led5), gpios, {0}),
    GPIO_DT_SPEC_GET_OR(DT_ALIAS(weekday_led6), gpios, {0}),
};

/*
 * pill_leds_init()
 * Pitfalls: Not reentrant; silently skips missing aliases. GPIO
 * configuration failures are logged but do not abort the system.
 */
int pill_leds_init(void)
{
    for (int i = 0; i < 7; i++) {
        const struct gpio_dt_spec *spec = &weekday_leds[i];
        if (spec->port == NULL) {
            LOG_DBG("weekday_led%u alias not present", i);
            continue;
        }
        if (!device_is_ready(spec->port)) {
            LOG_WRN("weekday_led%u: device not ready", i);
            continue;
        }
        int rc = gpio_pin_configure_dt(spec, GPIO_OUTPUT_INACTIVE);
        if (rc != 0) {
            LOG_ERR("weekday_led%u: gpio_pin_configure_dt failed: %d", i, rc);
        }
    }
    LOG_INF("pill_leds: init complete");
    return 0;
}

/*
 * pill_leds_show_weekday()
 * Pitfalls: Rapid calls will toggle GPIOs quickly; callers should
 * debounce / rate-limit if driven from a fast timer.
 */
void pill_leds_show_weekday(uint8_t weekday)
{
    /* weekday: 0 = Monday .. 6 = Sunday */
    for (int i = 0; i < 7; i++) {
        const struct gpio_dt_spec *spec = &weekday_leds[i];
        if (spec->port == NULL || !device_is_ready(spec->port)) {
            continue;
        }
        int val = (i == (int)weekday) ? 1 : 0;
        (void)gpio_pin_set_dt(spec, val);
    }
}

#endif /* CONFIG_PILL_WEEKDAY_LEDS */
