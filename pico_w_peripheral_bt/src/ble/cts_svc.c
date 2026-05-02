/* cts_svc.c - CTS (Current Time Service) callbacks
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Responsibilities (SRP):
 *   - Own the CTS callback table and local-time state.
 *   - Translate CTS time-writes into alarm_ctrl time-base updates.
 *   - Send periodic CTS notifications when a central has subscribed.
 */

#include "cts_svc.h"

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/services/cts.h>

#include "pill/alarm_ctrl.h"

/* Module-private state -------------------------------------------------------*/
static struct bt_cts_local_time local_time = {
	.timezone_offset = BT_CTS_TIMEZONE_DEFAULT_VALUE,
	.dst_offset      = BT_CTS_DST_OFFSET_UNKNOWN,
};
static bool cts_notify_enabled;
static const struct alarm_ctrl_api *g_api;

/* CTS callbacks -------------------------------------------------------------*/
static int time_write_cb(struct bt_cts_time_format *cts_time)
{
	int64_t unix_ms;
	int64_t epoch_s;
	int     err;

	if (!IS_ENABLED(CONFIG_BT_CTS_HELPER_API)) {
		return -ENOTSUP;
	}

	err = bt_cts_time_to_unix_ms(cts_time, &unix_ms);
	if (err != 0) {
		return err;
	}

	epoch_s = unix_ms / 1000;
	if (g_api != NULL) {
		g_api->set_time_base(epoch_s, k_uptime_get());
	}
	return 0;
}

static int fill_current_time_cb(struct bt_cts_time_format *cts_time)
{
	int64_t epoch_s;

	if (!IS_ENABLED(CONFIG_BT_CTS_HELPER_API)) {
		return -ENOTSUP;
	}

	epoch_s = (g_api != NULL) ? g_api->get_epoch_s() : 0;
	return bt_cts_time_from_unix_ms(cts_time, epoch_s * 1000);
}

static int local_time_write_cb(const struct bt_cts_local_time *lt)
{
	memcpy(&local_time, lt, sizeof(local_time));
	return 0;
}

static int fill_local_time_cb(struct bt_cts_local_time *lt)
{
	memcpy(lt, &local_time, sizeof(local_time));
	return 0;
}

static void notification_changed_cb(bool enabled)
{
	cts_notify_enabled = enabled;
}

static const struct bt_cts_cb cts_cb = {
	.notification_changed        = notification_changed_cb,
	.cts_time_write              = time_write_cb,
	.fill_current_cts_time       = fill_current_time_cb,
	.cts_local_time_write        = local_time_write_cb,
	.fill_current_cts_local_time = fill_local_time_cb,
};

/* Public API ----------------------------------------------------------------*/
void cts_svc_init(const struct alarm_ctrl_api *api)
{
	g_api = api;
	bt_cts_init(&cts_cb);
}

void cts_svc_tick(void)
{
	if (cts_notify_enabled) {
		bt_cts_send_notification(BT_CTS_UPDATE_REASON_MANUAL);
	}
}
