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
#include <zephyr/sys/printk.h>

#include "pill/alarm_ctrl.h"

static const struct alarm_ctrl_api *g_api;

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

/* ---------------------------------------------------------------------------
 * Connection callbacks (self-registered at link time via BT_CONN_CB_DEFINE)
 * -------------------------------------------------------------------------*/
static void connected_cb(struct bt_conn *conn, uint8_t err)
{
	ARG_UNUSED(conn);

	if (err != 0U) {
		printk("ble_mgr: connection failed (err 0x%02x %s)\n",
		       err, bt_hci_err_to_str(err));
		return;
	}

	if (g_api != NULL) {
		g_api->set_connected(true);
	}
	printk("ble_mgr: connected\n");
}

static void disconnected_cb(struct bt_conn *conn, uint8_t reason)
{
	ARG_UNUSED(conn);

	if (g_api != NULL) {
		g_api->set_connected(false);
		g_api->set_active(false, 0xFFU);
	}
	printk("ble_mgr: disconnected (reason 0x%02x %s)\n",
	       reason, bt_hci_err_to_str(reason));
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
			printk("ble_mgr: settings_load failed (%d)\n", err);
		}
	}

	err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1,
			      ad, ARRAY_SIZE(ad),
			      sd, ARRAY_SIZE(sd));
	if (err != 0) {
		printk("ble_mgr: advertising start failed (%d)\n", err);
		return err;
	}

	printk("ble_mgr: advertising started\n");
	return 0;
}
