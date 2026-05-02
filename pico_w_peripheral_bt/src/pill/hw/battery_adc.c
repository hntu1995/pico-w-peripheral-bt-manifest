/* battery_adc.c - Battery ADC measurement (Phase-2)
 *
 * Implements ADC sampling on channel 0 (GPIO26 on RP2040) and a simple
 * Li-ion voltage -> percentage mapping. The implementation is defensive
 * and falls back to a placeholder if no ADC device is available.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "pill/battery_adc.h"

#if defined(CONFIG_PILL_BATTERY_ADC) && (CONFIG_PILL_BATTERY_ADC == 1)

#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(pill_battery, LOG_LEVEL_INF);

/* Assumptions / defaults:
 * - ADC channel used: 0 (GPIO26 on RP2040)
 * - ADC reference voltage: VDD (approx 3300 mV)
 * - Voltage divider on battery input: 2 (default); change if your
 *   hardware uses a different divider.
 */
#ifndef PILL_BATTERY_ADC_CHANNEL
#define PILL_BATTERY_ADC_CHANNEL 0
#endif

#ifndef PILL_BATTERY_ADC_VREF_MV
#define PILL_BATTERY_ADC_VREF_MV 3300
#endif

#ifndef PILL_BATTERY_VOLTAGE_DIVIDER_NUM
#define PILL_BATTERY_VOLTAGE_DIVIDER_NUM 2
#endif

static const struct device *adc_dev = NULL;
static bool adc_ready = false;

/*
 * pill_battery_init()
 * Pitfalls: configures ADC channel 0. If the ADC driver name differs on
 * a platform this routine will leave adc_ready=false and callers will
 * receive the fallback percentage.
 */
int pill_battery_init(void)
{
    /* Try common ADC device names. This is conservative and logs if
     * no ADC is found.
     */
    adc_dev = device_get_binding("ADC_0");
    if (adc_dev == NULL) {
        adc_dev = device_get_binding("ADC");
    }
    if (adc_dev == NULL) {
        LOG_WRN("No ADC device found; battery ADC disabled");
        adc_ready = false;
        return -ENODEV;
    }

    if (!device_is_ready(adc_dev)) {
        LOG_WRN("ADC device not ready");
        adc_ready = false;
        return -ENODEV;
    }

    struct adc_channel_cfg ch_cfg = {
        .gain = ADC_GAIN_1,
        .reference = ADC_REF_INTERNAL,
        .acquisition_time = ADC_ACQ_TIME_DEFAULT,
        .channel_id = PILL_BATTERY_ADC_CHANNEL,
    };

    int rc = adc_channel_setup(adc_dev, &ch_cfg);
    if (rc != 0) {
        LOG_ERR("adc_channel_setup failed: %d", rc);
        adc_ready = false;
        return rc;
    }

    adc_ready = true;
    LOG_INF("pill_battery: ADC initialized (chan %d)", PILL_BATTERY_ADC_CHANNEL);
    return 0;
}

/* Map measured millivolts to a percentage using a small piecewise
 * linear Li-ion curve. Breakpoints chosen for typical single-cell
 * Li-ion chemistry.
 */
static uint8_t mv_to_percent(int32_t mv)
{
    const int bp_mv[] = {3300, 3600, 3700, 3800, 3900, 4050, 4200};
    const int bp_pct[] = {0, 20, 40, 60, 80, 95, 100};
    const int n = (int)(sizeof(bp_mv) / sizeof(bp_mv[0]));

    if (mv <= bp_mv[0]) {
        return 0U;
    }
    if (mv >= bp_mv[n - 1]) {
        return 100U;
    }

    for (int i = 1; i < n; i++) {
        if (mv <= bp_mv[i]) {
            int lo_mv = bp_mv[i - 1];
            int hi_mv = bp_mv[i];
            int lo_pct = bp_pct[i - 1];
            int hi_pct = bp_pct[i];
            int pct = lo_pct + (int)((int64_t)(mv - lo_mv) * (hi_pct - lo_pct) / (hi_mv - lo_mv));
            if (pct < 0) {
                pct = 0;
            } else if (pct > 100) {
                pct = 100;
            }
            return (uint8_t)pct;
        }
    }

    return 0U;
}

/*
 * pill_battery_get_percent()
 * Pitfalls: performs a single ADC sample (blocking). If adc_ready==false
 * returns a conservative cached / default percentage.
 */
uint8_t pill_battery_get_percent(void)
{
    if (!adc_ready || adc_dev == NULL) {
        /* Fallback when ADC unavailable */
        return 95U;
    }

    int16_t raw_sample = 0;
    const struct adc_sequence seq = {
        .options = NULL,
        .channels = BIT(PILL_BATTERY_ADC_CHANNEL),
        .buffer = &raw_sample,
        .buffer_size = sizeof(raw_sample),
        .resolution = 12,
        .oversampling = 0,
        .calibrate = false,
    };

    int rc = adc_read(adc_dev, &seq);
    if (rc != 0) {
        LOG_ERR("adc_read failed: %d", rc);
        return 95U;
    }

    /* Convert raw sample to millivolts */
    const int32_t vref_mv = PILL_BATTERY_ADC_VREF_MV;
    const int32_t max_code = (1 << seq.resolution) - 1;
    int32_t mv = (int32_t)((int64_t)raw_sample * vref_mv / max_code);

    /* Compensate for voltage divider (default 2:1) */
    mv = mv * PILL_BATTERY_VOLTAGE_DIVIDER_NUM;

    uint8_t pct = mv_to_percent(mv);
    return pct;
}

#endif /* CONFIG_PILL_BATTERY_ADC */
