/* pill_hw.h - Hardware abstraction for pill board
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PILL_HW_H_
#define PILL_HW_H_

#include <stdbool.h>
#include <stdint.h>

#include "pill/alarm_model.h"

int pill_hw_init(void);
void pill_hw_set_alarm_active(const struct pill_alarm *alarm, bool active);
bool pill_hw_poll_motion_clear(void);
uint8_t pill_hw_get_battery_percent(void);

#endif
