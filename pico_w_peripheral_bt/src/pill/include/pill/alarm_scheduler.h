/* alarm_scheduler.h - Scheduler API for pill alarms
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PILL_ALARM_SCHEDULER_H_
#define PILL_ALARM_SCHEDULER_H_

#include <stdbool.h>
#include <stdint.h>

#include "pill/alarm_model.h"

struct pill_scheduler {
    int64_t time_base_epoch_s;
    int64_t time_base_uptime_ms;
    int64_t last_fire_epoch_s[PILL_MAX_ALARMS];
};

void pill_scheduler_init(struct pill_scheduler *scheduler);
void pill_scheduler_set_time_base(struct pill_scheduler *scheduler,
                 int64_t epoch_s_now,
                 int64_t uptime_ms_now);
int64_t pill_scheduler_get_epoch_s(const struct pill_scheduler *scheduler,
                   int64_t uptime_ms_now);
int pill_scheduler_find_due(struct pill_scheduler *scheduler,
               const struct pill_alarm_table *table,
               int64_t epoch_s_now,
               uint8_t weekday_index);

#endif
