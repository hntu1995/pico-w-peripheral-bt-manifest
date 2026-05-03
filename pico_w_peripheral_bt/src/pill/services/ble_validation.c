/* ble_validation.c - BLE write validators for pill alarm table
 *
 * Performs strict validation of incoming alarm-table wire payloads prior to
 * applying them to runtime structures. Keeps validation logic separate from
 * decode/commit path to reduce blast radius.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "pill/ble_validation.h"
#include "pill/alarm_model.h"

#include <errno.h>
#include <stddef.h>

/* Wire protocol constants (must match pill_svc) */
#define WIRE_VERSION         1U
#define WIRE_BYTES_PER_ENTRY 12U

/* Wire protocol for pill-kind name table */
#define WIRE_KINDS_ENTRY_NAME_LEN PILL_KIND_NAME_MAX_LEN
#define WIRE_KINDS_ENTRY_SIZE (1U + WIRE_KINDS_ENTRY_NAME_LEN)

int pill_ble_validate_alarm_table(const uint8_t *buf, uint16_t len)
{
    if (buf == NULL) {
        return -EINVAL;
    }

    if (len < 2U) {
        return -EINVAL;
    }

    if (buf[0] != WIRE_VERSION) {
        return -ENOTSUP;
    }

    uint8_t count = buf[1];
    if (count > PILL_MAX_ALARMS) {
        return -EINVAL;
    }

    size_t expected = 2U + ((size_t)count * WIRE_BYTES_PER_ENTRY);
    if (expected != (size_t)len) {
        return -EINVAL;
    }

    for (uint8_t i = 0U; i < count; i++) {
        size_t pos = 2U + ((size_t)i * WIRE_BYTES_PER_ENTRY);
        struct pill_alarm a;
        a.hour = buf[pos + 0U];
        a.minute = buf[pos + 1U];
        a.weekday_mask = buf[pos + 2U];
        a.pill_kind = ((uint64_t)buf[pos + 3U]) |
                     ((uint64_t)buf[pos + 4U] << 8) |
                     ((uint64_t)buf[pos + 5U] << 16) |
                     ((uint64_t)buf[pos + 6U] << 24) |
                     ((uint64_t)buf[pos + 7U] << 32) |
                     ((uint64_t)buf[pos + 8U] << 40) |
                     ((uint64_t)buf[pos + 9U] << 48) |
                     ((uint64_t)buf[pos + 10U] << 56);
        a.enabled = buf[pos + 11U];

        if (!pill_alarm_validate(&a)) {
            return -EINVAL;
        }
    }

    return 0;
}

/* Validate a pill-kind name table payload received over BLE. Wire format:
 * [version:1][count:1][ entries: count * (id:1 + name:PILL_KIND_NAME_MAX_LEN) ]
 */
int pill_ble_validate_kind_table(const uint8_t *buf, uint16_t len)
{
    if (buf == NULL) {
        return -EINVAL;
    }

    if (len < 2U) {
        return -EINVAL;
    }

    if (buf[0] != WIRE_VERSION) {
        return -ENOTSUP;
    }

    uint8_t count = buf[1];
    if (count > PILL_MAX_KINDS) {
        return -EINVAL;
    }

    size_t expected = 2U + ((size_t)count * WIRE_KINDS_ENTRY_SIZE);
    if (expected != (size_t)len) {
        return -EINVAL;
    }

    for (uint8_t i = 0U; i < count; i++) {
        size_t pos = 2U + ((size_t)i * WIRE_KINDS_ENTRY_SIZE);
        uint8_t id = buf[pos + 0U];
        if (id >= PILL_MAX_KINDS) {
            return -EINVAL;
        }
        /* name bytes are accepted as-is (fixed length) */
    }

    return 0;
}
