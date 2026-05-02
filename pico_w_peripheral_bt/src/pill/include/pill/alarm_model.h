/* alarm_model.h - Alarm model definitions
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PILL_ALARM_MODEL_H_
#define PILL_ALARM_MODEL_H_

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/autoconf.h>

#define PILL_MAX_ALARMS CONFIG_PILL_MAX_ALARMS

#define PILL_WEEKDAY_MON 0x01U
#define PILL_WEEKDAY_TUE 0x02U
#define PILL_WEEKDAY_WED 0x04U
#define PILL_WEEKDAY_THU 0x08U
#define PILL_WEEKDAY_FRI 0x10U
#define PILL_WEEKDAY_SAT 0x20U
#define PILL_WEEKDAY_SUN 0x40U
#define PILL_WEEKDAY_ALL 0x7FU

struct pill_alarm {
    uint8_t hour;
    uint8_t minute;
    uint8_t weekday_mask;
    uint8_t pill_kind;
    uint8_t enabled;
};

struct pill_alarm_table {
    uint8_t count;
    struct pill_alarm entries[PILL_MAX_ALARMS];
};

void pill_alarm_table_clear(struct pill_alarm_table *table);
bool pill_alarm_validate(const struct pill_alarm *alarm);
int pill_alarm_table_set(struct pill_alarm_table *table, uint8_t index,
             const struct pill_alarm *alarm);

#endif
