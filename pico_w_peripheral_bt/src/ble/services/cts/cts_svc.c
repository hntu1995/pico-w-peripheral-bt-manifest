/* cts_svc.c - CTS (Current Time Service) callbacks
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Responsibilities (SRP):
 *   - Own the CTS callback table and local-time state.
 *   - Translate CTS time-writes into alarm_ctrl time-base updates.
 *   - Send periodic CTS notifications when a central has subscribed.
 *
 * Dependencies: struct cts_svc_api from app_api.h (set_time_base, get_epoch_s).
 */

#include "cts_svc.h"

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/services/cts.h>
#include <zephyr/logging/log.h>

#include "app_api.h"

LOG_MODULE_REGISTER(cts_svc);

/* ---------------------------------------------------------------------------
 * Module-private state
 * -------------------------------------------------------------------------*/

static struct bt_cts_local_time local_time = {
	.timezone_offset = BT_CTS_TIMEZONE_DEFAULT_VALUE,
	.dst_offset      = BT_CTS_DST_OFFSET_UNKNOWN,
};
static bool cts_notify_enabled;
static struct cts_svc_api g_api;

/* ---------------------------------------------------------------------------
 * CTS callbacks
 * -------------------------------------------------------------------------*/

/* Potential pitfalls:
 * - Reentrancy: runs in BT RX thread context.
 * - Blocking calls: bt_cts_time_to_unix_ms is pure math, non-blocking.
 * - Missing error handling: return err to BT stack if conversion fails.
 * - g_api function pointers must be checked for NULL before calling.
 */
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
	if (g_api.set_time_base != NULL) {
		g_api.set_time_base(epoch_s, k_uptime_get());
	}
	return 0;
}

/* Potential pitfalls:
 * - g_api.get_epoch_s may be NULL; fall back to 0.
 * - bt_cts_time_from_unix_ms may fail if epoch_s is invalid.
 */
static int fill_current_time_cb(struct bt_cts_time_format *cts_time)
{
	int64_t epoch_s;

	if (!IS_ENABLED(CONFIG_BT_CTS_HELPER_API)) {
		return -ENOTSUP;
	}

	epoch_s = (g_api.get_epoch_s != NULL) ? g_api.get_epoch_s() : 0;
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

/* ---------------------------------------------------------------------------
 * Service vtable callbacks (called by ble_service registry)
 * -------------------------------------------------------------------------*/

/* Potential pitfalls:
 * - app_api is cast from void* — must be struct app_api*.
 * - g_api stores function pointers; ensure they remain valid for lifetime.
 * - bt_cts_init registers callbacks; must be called after bt_enable().
 * - Must only be called once; init order is managed by ble_service_init_all().
 */
static int cts_svc_init(void *app_api)
{
	struct app_api *api = (struct app_api *)app_api;

	if (api == NULL) {
		return -EINVAL;
	}

	g_api = api->cts;
	bt_cts_init(&cts_cb);
	LOG_INF("CTS service initialised");
	return 0;
}

/* Potential pitfalls:
 * - cts_notify_enabled is set in BT RX thread context via notification_changed_cb.
 * - bt_cts_send_notification may fail if no connection or no subscription.
 * - Call once per second from main loop context.
 */
static void cts_svc_tick(void)
{
	if (cts_notify_enabled) {
		int err = bt_cts_send_notification(BT_CTS_UPDATE_REASON_MANUAL);
		if (err != 0) {
			LOG_DBG("CTS notify returned %d", err);
		}
	}
}

/* ---------------------------------------------------------------------------
 * Service instance (registered in ble_service.c)
 * -------------------------------------------------------------------------*/
struct ble_service g_cts_service = {
	.name = "cts",
	.init = cts_svc_init,
	.tick = cts_svc_tick,
};
