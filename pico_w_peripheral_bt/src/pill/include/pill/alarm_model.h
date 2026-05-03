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
#define PILL_MAX_KINDS CONFIG_PILL_MAX_KINDS

/* Compile-time guard: this implementation uses a 64-bit mask */
#if (PILL_MAX_KINDS > 64)
#error "PILL_MAX_KINDS > 64 not supported"
#endif

/* Mask covering all supported kinds (computed from PILL_MAX_KINDS) */
#if (PILL_MAX_KINDS >= 64)
#define PILL_KIND_ALL 0xFFFFFFFFFFFFFFFFULL
#else
#define PILL_KIND_ALL ((1ULL << PILL_MAX_KINDS) - 1ULL)
#endif

/* Maximum bytes for a human-readable pill kind name */
#define PILL_KIND_NAME_MAX_LEN 20U

/* Pill-kind name table entry and container. The phone/app can send a
 * mapping of kind indices to names which the device stores in flash.
 */
struct pill_kind_entry {
    uint8_t id; /* index in the table */
    char name[PILL_KIND_NAME_MAX_LEN];
};

struct pill_kind_table {
    uint8_t count;
    struct pill_kind_entry entries[PILL_MAX_KINDS];
};

struct pill_alarm {
    uint8_t hour;
    uint8_t minute;
    uint8_t weekday_mask;
    /* bitmask (up to PILL_MAX_KINDS bits): multiple bits allowed */
    uint64_t pill_kind;
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
