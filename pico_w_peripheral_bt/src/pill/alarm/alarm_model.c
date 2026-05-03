#include "pill/alarm_model.h"

#include <errno.h>

#include <zephyr/sys/util.h>

void pill_alarm_table_clear(struct pill_alarm_table *table)
{
    uint8_t i;

    table->count = 0U;
    for (i = 0U; i < PILL_MAX_ALARMS; i++) {
        table->entries[i].hour = 0U;
        table->entries[i].minute = 0U;
        table->entries[i].weekday_mask = 0U;
        table->entries[i].pill_kind = 0U;
        table->entries[i].enabled = 0U;
    }
}

bool pill_alarm_validate(const struct pill_alarm *alarm)
{
    if (alarm->hour > 23U) {
        return false;
    }

    if (alarm->minute > 59U) {
        return false;
    }

    if ((alarm->weekday_mask & PILL_WEEKDAY_ALL) == 0U) {
        return false;
    }

    if (alarm->enabled > 1U) {
        return false;
    }

    /*
     * `pill_kind` is a bitmask where each bit selects a pill kind. If the
     * alarm is enabled then at least one kind must be selected. Also reject
     * any bits outside the defined `PILL_KIND_ALL` mask.
     */
    if ((alarm->pill_kind & ~PILL_KIND_ALL) != 0U) {
        return false;
    }

    if ((alarm->enabled != 0U) && (alarm->pill_kind == 0U)) {
        return false;
    }

    return true;
}

int pill_alarm_table_set(struct pill_alarm_table *table, uint8_t index,
             const struct pill_alarm *alarm)
{
    if (index >= PILL_MAX_ALARMS) {
        return -EINVAL;
    }

    if (!pill_alarm_validate(alarm)) {
        return -EINVAL;
    }

    table->entries[index] = *alarm;

    if (index + 1U > table->count) {
        table->count = index + 1U;
    }

    return 0;
}
