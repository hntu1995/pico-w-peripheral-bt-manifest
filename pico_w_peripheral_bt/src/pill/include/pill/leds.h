/* leds.h - Weekday LED indicators (Phase-2 scaffold)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PILL_LEDS_H_
#define PILL_LEDS_H_

#include <stdint.h>

#if defined(CONFIG_PILL_WEEKDAY_LEDS) && (CONFIG_PILL_WEEKDAY_LEDS == 1)
int pill_leds_init(void);
void pill_leds_show_weekday(uint8_t weekday);
#else
static inline int pill_leds_init(void) { return 0; }
static inline void pill_leds_show_weekday(uint8_t weekday) { (void)weekday; }
#endif

#endif /* PILL_LEDS_H_ */
