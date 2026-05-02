#include "pill/pill_hw.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include "pill/battery_adc.h"

#define PILL_MOTION_THRESHOLD_MILLI_G CONFIG_PILL_MOTION_THRESHOLD_MILLI_G
#define PILL_MOTION_HITS_TO_CLEAR CONFIG_PILL_MOTION_HITS_TO_CLEAR

#if IS_ENABLED(CONFIG_PILL_BUZZER)
#define ALARM_BUZZER_NODE DT_ALIAS(alarm_buzzer)
static const struct gpio_dt_spec buzzer = GPIO_DT_SPEC_GET_OR(ALARM_BUZZER_NODE,
                              gpios, {0});
#endif /* CONFIG_PILL_BUZZER */

#if IS_ENABLED(CONFIG_PILL_MOTION_SENSOR)
#define MOTION_IMU_NODE DT_ALIAS(motion_imu)
static const struct device *motion_imu = DEVICE_DT_GET_OR_NULL(MOTION_IMU_NODE);
static uint8_t motion_hit_count;
#endif /* CONFIG_PILL_MOTION_SENSOR */

int pill_hw_init(void)
{
#if IS_ENABLED(CONFIG_PILL_BUZZER)
    if (buzzer.port != NULL) {
        if (!device_is_ready(buzzer.port)) {
            printk("Buzzer GPIO not ready\n");
        } else {
            gpio_pin_configure_dt(&buzzer, GPIO_OUTPUT_INACTIVE);
        }
    }
#endif /* CONFIG_PILL_BUZZER */

#if IS_ENABLED(CONFIG_PILL_MOTION_SENSOR)
    if (motion_imu != NULL && !device_is_ready(motion_imu)) {
        printk("MPU-6050 not ready, motion-cancel disabled\n");
    }
    motion_hit_count = 0U;
#endif /* CONFIG_PILL_MOTION_SENSOR */

#if IS_ENABLED(CONFIG_PILL_BATTERY_ADC)
    /* Initialise battery ADC (Phase-2 stub or real implementation). */
    (void)pill_battery_init();
#endif /* CONFIG_PILL_BATTERY_ADC */

    return 0;
}

void pill_hw_set_alarm_active(const struct pill_alarm *alarm, bool active)
{
    ARG_UNUSED(alarm);
#if IS_ENABLED(CONFIG_PILL_BUZZER)
    if (buzzer.port != NULL && device_is_ready(buzzer.port)) {
        gpio_pin_set_dt(&buzzer, active ? 1 : 0);
    }
#else
    ARG_UNUSED(active);
#endif /* CONFIG_PILL_BUZZER */
}

#if IS_ENABLED(CONFIG_PILL_MOTION_SENSOR)
static int32_t sensor_val_to_milli(const struct sensor_value *val)
{
    int64_t result;

    result = (int64_t)val->val1 * 1000 + (val->val2 / 1000);
    return (int32_t)result;
}

static int32_t iabs32(int32_t value)
{
    return (value < 0) ? -value : value;
}
#endif /* CONFIG_PILL_MOTION_SENSOR */

bool pill_hw_poll_motion_clear(void)
{
#if IS_ENABLED(CONFIG_PILL_MOTION_SENSOR)
    struct sensor_value accel[3];
    int32_t x;
    int32_t y;
    int32_t z;
    int32_t motion_mag;

    if (motion_imu == NULL || !device_is_ready(motion_imu)) {
        return false;
    }

    if (sensor_sample_fetch(motion_imu) != 0) {
        return false;
    }
    if (sensor_channel_get(motion_imu, SENSOR_CHAN_ACCEL_XYZ, accel) != 0) {
        return false;
    }

    x = sensor_val_to_milli(&accel[0]);
    y = sensor_val_to_milli(&accel[1]);
    z = sensor_val_to_milli(&accel[2]);
    motion_mag = iabs32(x) + iabs32(y) + iabs32(z - 1000);

    if (motion_mag > PILL_MOTION_THRESHOLD_MILLI_G) {
        if (motion_hit_count < UINT8_MAX) {
            motion_hit_count++;
        }
    } else {
        motion_hit_count = 0U;
    }

    if (motion_hit_count >= PILL_MOTION_HITS_TO_CLEAR) {
        motion_hit_count = 0U;
        return true;
    }

    return false;
#else
    return false;
#endif /* CONFIG_PILL_MOTION_SENSOR */
}

uint8_t pill_hw_get_battery_percent(void)
{
#if IS_ENABLED(CONFIG_PILL_BATTERY_ADC)
    return pill_battery_get_percent();
#else
    /* Placeholder: real ADC measurement disabled. */
    return 95U;
#endif /* CONFIG_PILL_BATTERY_ADC */
}
