#include "pill/alarm_scheduler.h"

#include <errno.h>

#include <zephyr/sys/util.h>

void pill_scheduler_init(struct pill_scheduler *scheduler)
{
    uint8_t i;

    scheduler->time_base_epoch_s = 0;
    scheduler->time_base_uptime_ms = 0;
    for (i = 0U; i < PILL_MAX_ALARMS; i++) {
        scheduler->last_fire_epoch_s[i] = -1;
    }
}

void pill_scheduler_set_time_base(struct pill_scheduler *scheduler,
                 int64_t epoch_s_now,
                 int64_t uptime_ms_now)
{
    scheduler->time_base_epoch_s = epoch_s_now;
    scheduler->time_base_uptime_ms = uptime_ms_now;
}

int64_t pill_scheduler_get_epoch_s(const struct pill_scheduler *scheduler,
                   int64_t uptime_ms_now)
{
    int64_t delta_ms;

    delta_ms = uptime_ms_now - scheduler->time_base_uptime_ms;
    if (delta_ms < 0) {
        delta_ms = 0;
    }

    return scheduler->time_base_epoch_s + (delta_ms / 1000);
}

int pill_scheduler_find_due(struct pill_scheduler *scheduler,
               const struct pill_alarm_table *table,
               int64_t epoch_s_now,
               uint8_t weekday_index)
{
    uint8_t i;
    uint8_t now_hour;
    uint8_t now_minute;
    uint8_t weekday_bit;
    int64_t minute_epoch;

    if (weekday_index > 6U) {
        return -EINVAL;
    }

    now_hour = (uint8_t)((epoch_s_now / 3600) % 24);
    now_minute = (uint8_t)((epoch_s_now / 60) % 60);
    weekday_bit = BIT(weekday_index);
    minute_epoch = epoch_s_now - (epoch_s_now % 60);

    for (i = 0U; i < table->count; i++) {
        const struct pill_alarm *alarm = &table->entries[i];

        if (alarm->enabled == 0U) {
            continue;
        }
        /* weekday_mask==0 means one-time alarm: do not filter by weekday. */
        if ((alarm->weekday_mask != 0U) && ((alarm->weekday_mask & weekday_bit) == 0U)) {
            continue;
        }
        if (alarm->hour != now_hour) {
            continue;
        }
        if (alarm->minute != now_minute) {
            continue;
        }
        if (scheduler->last_fire_epoch_s[i] == minute_epoch) {
            continue;
        }

        scheduler->last_fire_epoch_s[i] = minute_epoch;
        return i;
    }

    return -ENOENT;
}
