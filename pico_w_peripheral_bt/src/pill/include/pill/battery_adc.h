/* battery_adc.h - Battery ADC measurement API (Phase-2 scaffold)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PILL_BATTERY_ADC_H_
#define PILL_BATTERY_ADC_H_

#include <stdint.h>

#if defined(CONFIG_PILL_BATTERY_ADC) && (CONFIG_PILL_BATTERY_ADC == 1)
int pill_battery_init(void);
uint8_t pill_battery_get_percent(void);
#else
static inline int pill_battery_init(void) { return 0; }
static inline uint8_t pill_battery_get_percent(void) { return 95U; }
#endif

#endif /* PILL_BATTERY_ADC_H_ */
