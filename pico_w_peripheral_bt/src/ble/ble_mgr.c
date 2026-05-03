/* ble_mgr.c - BLE connection management and advertising
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Responsibilities (SRP):
 *   - Own the advertising data (AD / SD payloads).
 *   - Handle BLE connect / disconnect events and update alarm_ctrl state.
 *   - Start advertising after bt_enable().
 *   - Load bond/settings data before starting advertising.
 */

#include "ble_mgr.h"

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/services/bas.h>
#include <zephyr/bluetooth/services/cts.h>
#include <zephyr/bluetooth/services/ias.h>
#include <zephyr/settings/settings.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "pill/alarm_ctrl.h"

static const struct alarm_ctrl_api *g_api;

/* Fallback poll-based restart deadline (ms, 32-bit uptime). */
static uint32_t adv_restart_deadline_ms;

LOG_MODULE_REGISTER(ble_mgr);

/* Pull in the pill-service UUID for the UUID128 AD record when enabled. */
/* ---------------------------------------------------------------------------
 * Advertising payload
 * -------------------------------------------------------------------------*/

/* UUID16 services always advertised. */
static const uint8_t ad_uuid16_bytes[] = {
	BT_UUID_16_ENCODE(BT_UUID_BAS_VAL),
	BT_UUID_16_ENCODE(BT_UUID_CTS_VAL),
	BT_UUID_16_ENCODE(BT_UUID_IAS_VAL),
};

/* Named array avoids a compound-literal in a file-scope static initializer. */
#if IS_ENABLED(CONFIG_PILL_BLE_SERVICE)
static const uint8_t ad_pill_uuid128[] = {
	BT_UUID_128_ENCODE(0xa1b2c3d4, 0x1234, 0x4321, 0xabcd, 0xef0102030405)
};
#endif

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_UUID16_ALL, ad_uuid16_bytes, sizeof(ad_uuid16_bytes)),
#if IS_ENABLED(CONFIG_PILL_BLE_SERVICE)
	BT_DATA(BT_DATA_UUID128_ALL, ad_pill_uuid128, sizeof(ad_pill_uuid128)),
#endif
};

static const struct bt_data sd[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME,
		sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

/* NOTE: We previously used a k_timer to restart advertising from the
 * Bluetooth callback context. Interacting with the controller (bt_le_adv_*)
 * from that context can cause subtle driver / HCI issues on some setups.
 * Instead we use a simple deadline monitored by the main loop via
 * ble_mgr_poll() which runs in thread context and performs the restart.
 */

/* Called from the main loop once per second to perform maintenance tasks.
 * This implements a fallback watchdog: if the disconnect occurred and the
 * deadline has passed without the timer handler running, restart
 * advertising here.
 */
void ble_mgr_poll(void)
{
	uint32_t now = k_uptime_get_32();

	LOG_DBG("ble_mgr_poll called: now=%u deadline=%u", now, adv_restart_deadline_ms);

	if (adv_restart_deadline_ms == 0U) {
		return;
	}

	if ((int32_t)(now - adv_restart_deadline_ms) < 0) {
		return; /* not yet */
	}

	/* Clear deadline to avoid re-entering. */
	adv_restart_deadline_ms = 0U;

	LOG_INF("ble_mgr_poll: restart deadline reached, attempting advertising restart (poll)");

	/* settings_load() is called at startup in ble_mgr_start_advertising().
	 * Calling it again here can attempt to re-add IRKs to the controller
	 * and trigger non-fatal HCI errors. Avoid re-loading settings on
	 * poll-based restarts.
	 */

	int err2 = bt_le_adv_stop();
	if (err2 != 0 && err2 != -EALREADY) {
		LOG_WRN("bt_le_adv_stop returned %d in poll-restart", err2);
	}

	err2 = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1,
				  ad, ARRAY_SIZE(ad),
				  sd, ARRAY_SIZE(sd));
	if (err2 != 0) {
		LOG_ERR("poll: advertising restart failed (%d)", err2);
	} else {
		LOG_INF("poll: advertising restarted after disconnect");
	}
}

/* ---------------------------------------------------------------------------
 * Connection callbacks (self-registered at link time via BT_CONN_CB_DEFINE)
 * -------------------------------------------------------------------------*/
static void connected_cb(struct bt_conn *conn, uint8_t err)
{
	ARG_UNUSED(conn);

	if (err != 0U) {
		LOG_ERR("connection failed (err 0x%02x %s)", err,
				bt_hci_err_to_str(err));
		return;
	}

	if (g_api != NULL) {
		g_api->set_connected(true);
	}
	/* Cancel any pending advertising restart since we're now connected. */
	LOG_INF("connected; cancelling pending advertising restart (if any)");
	/* Clear any pending poll-based deadline. */
	adv_restart_deadline_ms = 0U;
}

static void disconnected_cb(struct bt_conn *conn, uint8_t reason)
{
	ARG_UNUSED(conn);

	if (g_api != NULL) {
		g_api->set_connected(false);
		g_api->set_active(false, 0xFFU);
	}
	LOG_INF("disconnected (reason 0x%02x %s)", reason,
			bt_hci_err_to_str(reason));

	/* Restart advertising after a short delay to allow central to settle.
	 * Schedule a single-shot timer to start advertising in 5s. */
	LOG_INF("scheduling advertising restart in 5s");

	/* Only set a deadline for the main-loop poll to perform the restart.
	 * This avoids invoking controller APIs from the connection callback
	 * context which has caused issues on some setups. The main loop will
	 * call ble_mgr_poll() once per second and perform the actual restart.
	 */
	adv_restart_deadline_ms = k_uptime_get_32() + 5000U;
	LOG_DBG("fallback deadline set=%u", adv_restart_deadline_ms);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected    = connected_cb,
	.disconnected = disconnected_cb,
};

/* ---------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------*/
int ble_mgr_start_advertising(const struct alarm_ctrl_api *api)
{
	int err;

	g_api = api;

	/* Load BLE bond / security settings before advertising so that
	 * previously bonded centrals are recognised immediately on reconnect. */
	if (IS_ENABLED(CONFIG_PILL_SETTINGS)) {
		err = settings_load();
		if (err != 0) {
			LOG_ERR("settings_load failed (%d)", err);
		}
	}

	err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1,
				  ad, ARRAY_SIZE(ad),
				  sd, ARRAY_SIZE(sd));
	if (err != 0) {
		LOG_ERR("advertising start failed (%d)", err);
		return err;
	}

	LOG_INF("advertising started");
	return 0;
}
