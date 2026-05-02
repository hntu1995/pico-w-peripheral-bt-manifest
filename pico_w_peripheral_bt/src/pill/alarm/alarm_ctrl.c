/* alarm_ctrl.c - Runtime alarm control state (domain layer, no BLE deps)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "pill/alarm_ctrl.h"

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "pill/alarm_model.h"
#include "pill/alarm_scheduler.h"
#include "pill/app_settings.h"
#include "pill/pill_hw.h"

static struct pill_alarm_table  g_table;
static struct pill_scheduler    g_scheduler;
static struct alarm_ctrl_status g_status;
static bool                     g_alarm_active;
static uint8_t                  g_active_alarm_idx;
static int64_t                  g_last_synced_epoch_s;

/* Map Unix epoch to 0=Mon..6=Sun (same as before: (days_since_unix+3)%7). */
static uint8_t weekday_from_epoch(int64_t epoch_s)
{
    int64_t days;

    if (epoch_s < 0) {
        return 0U;
    }
    days = epoch_s / 86400;
    return (uint8_t)((days + 3) % 7);
}

int alarm_ctrl_init(void)
{
    pill_alarm_table_clear(&g_table);
    pill_scheduler_init(&g_scheduler);
    g_alarm_active         = false;
    g_active_alarm_idx     = 0xFFU;
    g_last_synced_epoch_s  = 0;
    memset(&g_status, 0, sizeof(g_status));
    g_status.active_alarm_index = 0xFFU;

    /* Ensure hardware is silent at startup. */
    pill_hw_set_alarm_active(NULL, false);

#if IS_ENABLED(CONFIG_PILL_SETTINGS)
    int err = pill_app_settings_init(&g_table, &g_last_synced_epoch_s);

    if (err != 0) {
        printk("alarm_ctrl: settings init failed (%d), using defaults\n", err);
    }
    if (g_last_synced_epoch_s > 0) {
        pill_scheduler_set_time_base(&g_scheduler,
                         g_last_synced_epoch_s,
                         k_uptime_get());
    }
#endif

    return 0;
}

void alarm_ctrl_set_active(bool active, uint8_t alarm_index)
{
    g_alarm_active         = active;
    g_active_alarm_idx     = alarm_index;
    g_status.active_alarm  = active ? 1U : 0U;
    g_status.active_alarm_index = active ? alarm_index : 0xFFU;

    if (active && alarm_index < g_table.count) {
        pill_hw_set_alarm_active(&g_table.entries[alarm_index], true);
    } else {
        pill_hw_set_alarm_active(NULL, false);
    }
}

bool alarm_ctrl_is_active(void)
{
    return g_alarm_active;
}

uint8_t alarm_ctrl_get_active_idx(void)
{
    return g_active_alarm_idx;
}

struct pill_alarm_table *alarm_ctrl_get_table(void)
{
    return &g_table;
}

void alarm_ctrl_set_time_base(int64_t epoch_s, int64_t uptime_ms)
{
    pill_scheduler_set_time_base(&g_scheduler, epoch_s, uptime_ms);
    g_last_synced_epoch_s = epoch_s;

#if IS_ENABLED(CONFIG_PILL_SETTINGS)
    (void)pill_app_settings_save_last_epoch_s(epoch_s);
#endif
}

int64_t alarm_ctrl_get_epoch_s(void)
{
    return pill_scheduler_get_epoch_s(&g_scheduler, k_uptime_get());
}

void alarm_ctrl_set_connected(bool connected)
{
    g_status.connected = connected ? 1U : 0U;
}

const struct alarm_ctrl_status *alarm_ctrl_get_status(void)
{
    return &g_status;
}

void alarm_ctrl_tick(void)
{
    int64_t epoch_s;
    uint8_t weekday;
    int     due_index;
    uint8_t battery_percent;

    epoch_s = pill_scheduler_get_epoch_s(&g_scheduler, k_uptime_get());
    if (epoch_s <= 0) {
        return;
    }

    /* Motion-cancel: silence a firing alarm when movement is detected. */
    if (g_alarm_active && pill_hw_poll_motion_clear()) {
        printk("alarm_ctrl: motion detected, alarm cleared\n");
        alarm_ctrl_set_active(false, 0xFFU);
    }

    /* Fire a due alarm. */
    weekday   = weekday_from_epoch(epoch_s);
    due_index = pill_scheduler_find_due(&g_scheduler, &g_table, epoch_s, weekday);
    if (!g_alarm_active && due_index >= 0) {
        printk("alarm_ctrl: alarm %d triggered\n", due_index);
        alarm_ctrl_set_active(true, (uint8_t)due_index);
    }

    /* Update battery snapshot (BLE callers apply bt_bas / notify after tick). */
    battery_percent         = pill_hw_get_battery_percent();
    g_status.battery_percent = battery_percent;
    g_status.low_battery     =
        (battery_percent <= CONFIG_PILL_LOW_BATTERY_THRESHOLD_PERCENT) ? 1U : 0U;
}

/* ---------------------------------------------------------------------------
 * Exported API struct
 * -------------------------------------------------------------------------*/
static const struct alarm_ctrl_api alarm_ctrl_api_impl = {
    .set_active    = alarm_ctrl_set_active,
    .is_active     = alarm_ctrl_is_active,
    .get_active_idx= alarm_ctrl_get_active_idx,
    .get_table     = alarm_ctrl_get_table,
    .set_time_base = alarm_ctrl_set_time_base,
    .get_epoch_s   = alarm_ctrl_get_epoch_s,
    .set_connected = alarm_ctrl_set_connected,
    .get_status    = alarm_ctrl_get_status,
    .tick          = alarm_ctrl_tick,
};

const struct alarm_ctrl_api *alarm_ctrl_get_api(void)
{
    return &alarm_ctrl_api_impl;
}
