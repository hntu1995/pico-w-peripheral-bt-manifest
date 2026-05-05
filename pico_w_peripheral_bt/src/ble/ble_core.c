/* ble_core.c - Generic BLE stack initialisation and management
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Responsibilities (SRP):
 *   - bt_enable() + advertising lifecycle.
 *   - Connection / disconnect event forwarding via ble_core_events.
 *   - Auth callbacks (pairing confirm, passkey display, etc.).
 *   - Poll-based advertising restart with exponential backoff.
 *
 * Zero knowledge of pill/alarm domain — all app state is
 * communicated through ble_core_events callbacks.
 */

#include "ble_core.h"

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

LOG_MODULE_REGISTER(ble_core);

/* ---------------------------------------------------------------------------
 * Module-private state
 * -------------------------------------------------------------------------*/

static const struct ble_core_events *g_events;

/* Fallback poll-based restart deadline (ms, 32-bit uptime). */
static uint32_t adv_restart_deadline_ms;
/* Guard whether advertising is currently running (prevent duplicate starts). */
static bool adv_running;

/* Advertising backoff state to avoid tight restart loops on repeated failures. */
#define ADV_BACKOFF_INITIAL_MS 1000U
#define ADV_BACKOFF_MAX_MS 60000U
static uint32_t adv_backoff_ms = ADV_BACKOFF_INITIAL_MS;
/* If non-zero, do not attempt to start advertising until this uptime (ms). */
static uint32_t adv_next_start_allowed_ms;
static int adv_consecutive_failures;
static bool auth_cb_registered;
static bool auth_info_registered;

/* ---------------------------------------------------------------------------
 * Auth callbacks
 * -------------------------------------------------------------------------*/

/* Potential pitfalls:
 * - Security UX: auto-confirming Just Works is convenient but weaker than
 *   explicit user confirmation.
 * - Callback context: keep this non-blocking and logging-only.
 * - Missing diagnostics: include peer and return code for failures.
 */
static void auth_pairing_confirm_cb(struct bt_conn *conn)
{
	char addr_str[BT_ADDR_LE_STR_LEN];
	const bt_addr_le_t *peer = bt_conn_get_dst(conn);
	int err;

	if (peer != NULL) {
		bt_addr_le_to_str(peer, addr_str, sizeof(addr_str));
		LOG_INF("pairing confirm requested: peer=%s", addr_str);
	} else {
		LOG_INF("pairing confirm requested: peer=<unknown>");
	}

	err = bt_conn_auth_pairing_confirm(conn);
	if (err != 0) {
		LOG_ERR("bt_conn_auth_pairing_confirm failed (%d)", err);
	}
}

/* Potential pitfalls:
 * - Security tradeoff: auto-confirming numeric comparison weakens manual
 *   user verification against MITM.
 * - Callback context: keep this short and non-blocking.
 * - Error handling: always log failed confirm operations.
 */
static void auth_passkey_confirm_cb(struct bt_conn *conn, unsigned int passkey)
{
	char addr_str[BT_ADDR_LE_STR_LEN];
	const bt_addr_le_t *peer = bt_conn_get_dst(conn);
	int err;

	if (peer != NULL) {
		bt_addr_le_to_str(peer, addr_str, sizeof(addr_str));
		LOG_INF("passkey confirm requested: peer=%s passkey=%06u (auto-accept)",
			addr_str, passkey);
	} else {
		LOG_INF("passkey confirm requested: peer=<unknown> passkey=%06u (auto-accept)",
			passkey);
	}

	err = bt_conn_auth_passkey_confirm(conn);
	if (err != 0) {
		LOG_ERR("bt_conn_auth_passkey_confirm failed (%d)", err);
	}
}

/* Potential pitfalls:
 * - Numeric comparison / display-only pairing can still occur depending on
 *   remote capabilities.
 * - No local UI exists, so log-only is the safest available behavior.
 */
static void auth_passkey_display_cb(struct bt_conn *conn, unsigned int passkey)
{
	char addr_str[BT_ADDR_LE_STR_LEN];
	const bt_addr_le_t *peer = bt_conn_get_dst(conn);

	if (peer != NULL) {
		bt_addr_le_to_str(peer, addr_str, sizeof(addr_str));
		LOG_INF("passkey display: peer=%s passkey=%06u", addr_str, passkey);
	} else {
		LOG_INF("passkey display: peer=<unknown> passkey=%06u", passkey);
	}
}

/* Potential pitfalls:
 * - If this fires, the peripheral cannot accept manual entry because there is
 *   no local keypad/UI.
 * - Cancel promptly to make the failure reason explicit in logs.
 */
static void auth_passkey_entry_cb(struct bt_conn *conn)
{
	char addr_str[BT_ADDR_LE_STR_LEN];
	const bt_addr_le_t *peer = bt_conn_get_dst(conn);

	if (peer != NULL) {
		bt_addr_le_to_str(peer, addr_str, sizeof(addr_str));
		LOG_ERR("passkey entry requested but unsupported: peer=%s", addr_str);
	} else {
		LOG_ERR("passkey entry requested but unsupported: peer=<unknown>");
	}

	(void)bt_conn_auth_cancel(conn);
}

/* Potential pitfalls:
 * - Pairing cancellation can happen during disconnect.
 * - Peer address may be unavailable late in teardown.
 */
static void auth_cancel_cb(struct bt_conn *conn)
{
	char addr_str[BT_ADDR_LE_STR_LEN];
	const bt_addr_le_t *peer = bt_conn_get_dst(conn);

	if (peer != NULL) {
		bt_addr_le_to_str(peer, addr_str, sizeof(addr_str));
		LOG_INF("pairing cancelled: peer=%s", addr_str);
	} else {
		LOG_INF("pairing cancelled: peer=<unknown>");
	}
}

static const struct bt_conn_auth_cb auth_cb = {
	.passkey_display = auth_passkey_display_cb,
	.passkey_entry   = auth_passkey_entry_cb,
	.passkey_confirm = auth_passkey_confirm_cb,
	.cancel          = auth_cancel_cb,
	.pairing_confirm = auth_pairing_confirm_cb,
};

/* ---------------------------------------------------------------------------
 * Auth info callbacks
 * -------------------------------------------------------------------------*/

/* Potential pitfalls:
 * - Callback context: keep this non-blocking.
 * - Reentrancy: may run for multiple connections.
 * - Missing error handling: always include peer + reason in logs.
 */
static void pairing_complete_cb(struct bt_conn *conn, bool bonded)
{
	char addr_str[BT_ADDR_LE_STR_LEN];
	const bt_addr_le_t *peer = bt_conn_get_dst(conn);

	if (peer != NULL) {
		bt_addr_le_to_str(peer, addr_str, sizeof(addr_str));
		LOG_INF("pairing complete: peer=%s bonded=%u", addr_str, bonded ? 1U : 0U);
	} else {
		LOG_INF("pairing complete: peer=<unknown> bonded=%u", bonded ? 1U : 0U);
	}
}

/* Potential pitfalls:
 * - Callback context: keep this non-blocking.
 * - Diagnostics: include both numeric and string reason.
 * - Race conditions: peer may be unavailable during teardown.
 */
static void pairing_failed_cb(struct bt_conn *conn, enum bt_security_err reason)
{
	char addr_str[BT_ADDR_LE_STR_LEN];
	const bt_addr_le_t *peer = bt_conn_get_dst(conn);

	if (peer != NULL) {
		bt_addr_le_to_str(peer, addr_str, sizeof(addr_str));
		LOG_ERR("pairing failed: peer=%s reason=%u %s",
			addr_str, reason, bt_security_err_to_str(reason));
	} else {
		LOG_ERR("pairing failed: peer=<unknown> reason=%u %s",
			reason, bt_security_err_to_str(reason));
	}
}

static struct bt_conn_auth_info_cb auth_info_cb = {
	.pairing_complete = pairing_complete_cb,
	.pairing_failed   = pairing_failed_cb,
};

/* ---------------------------------------------------------------------------
 * Auth registration helpers
 * -------------------------------------------------------------------------*/

/* Potential pitfalls:
 * - Double registration: guard with local flag and -EALREADY handling.
 * - Startup ordering: call after bt_enable() and before active pairing.
 * - Error propagation: keep startup running, but log failures loudly.
 */
static void register_auth_info_callbacks_once(void)
{
	int err;

	if (auth_info_registered) {
		return;
	}

	err = bt_conn_auth_info_cb_register(&auth_info_cb);
	if (err == 0 || err == -EALREADY) {
		auth_info_registered = true;
		LOG_DBG("auth info callbacks registered (err=%d)", err);
	} else {
		LOG_ERR("bt_conn_auth_info_cb_register failed (%d)", err);
	}
}

/* Potential pitfalls:
 * - Double registration: guard locally and tolerate -EALREADY.
 * - Startup ordering: register after bt_enable() and before pairing starts.
 * - Security behavior: this controls whether incoming pairing is accepted.
 */
static void register_auth_callbacks_once(void)
{
	int err;

	if (auth_cb_registered) {
		return;
	}

	err = bt_conn_auth_cb_register(&auth_cb);
	if (err == 0 || err == -EALREADY) {
		auth_cb_registered = true;
		LOG_DBG("auth callbacks registered (err=%d)", err);
	} else {
		LOG_ERR("bt_conn_auth_cb_register failed (%d)", err);
	}
}

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

/* Use identity address for connectable advertising to avoid scanner-side
 * address churn when privacy is enabled.
 */
#define BLE_CORE_ADV_PARAM_IDENTITY_FAST_1 \
	BT_LE_ADV_PARAM(BT_LE_ADV_OPT_CONN | BT_LE_ADV_OPT_USE_IDENTITY, \
			BT_GAP_ADV_FAST_INT_MIN_1, BT_GAP_ADV_FAST_INT_MAX_1, NULL)

/* ---------------------------------------------------------------------------
 * Connection callbacks (self-registered at link time via BT_CONN_CB_DEFINE)
 * -------------------------------------------------------------------------*/

/* Potential pitfalls:
 * - Callback context: runs in BT RX thread, keep non-blocking.
 * - Missing error handling: include peer address in all logs.
 * - Race conditions: may fire for stale connections during teardown.
 */
static void security_changed_cb(struct bt_conn *conn, bt_security_t level,
				enum bt_security_err err)
{
	char addr_str[BT_ADDR_LE_STR_LEN];
	const bt_addr_le_t *peer = bt_conn_get_dst(conn);

	if (peer != NULL) {
		bt_addr_le_to_str(peer, addr_str, sizeof(addr_str));
		LOG_INF("security changed: peer=%s level=%u err=%u %s",
			addr_str, level, err, bt_security_err_to_str(err));
	} else {
		LOG_INF("security changed: peer=<unknown> level=%u err=%u %s",
			level, err, bt_security_err_to_str(err));
	}
}

static void connected_cb(struct bt_conn *conn, uint8_t err)
{
	if (err != 0U) {
		LOG_ERR("connection failed (err 0x%02x %s)", err,
			bt_hci_err_to_str(err));
		return;
	}

	/* Log peer address and connection info for diagnostics. */
	{
		char addr_str[BT_ADDR_LE_STR_LEN];
		const bt_addr_le_t *peer = bt_conn_get_dst(conn);

		if (peer != NULL) {
			bt_addr_le_to_str(peer, addr_str, sizeof(addr_str));
			LOG_INF("connected: peer=%s", addr_str);
		} else {
			LOG_INF("connected: peer=<unknown>");
		}
	}

	/* Notify app layer — services are notified via their own init. */
	if (g_events != NULL && g_events->on_connected != NULL) {
		g_events->on_connected(true);
	}

	/* Request encryption/bonding as soon as the link is established. */
	{
		int sec_err = bt_conn_set_security(conn, BT_SECURITY_L2);

		if (sec_err != 0 && sec_err != -EALREADY) {
			LOG_WRN("bt_conn_set_security(L2) failed (%d)", sec_err);
		} else {
			LOG_INF("bt_conn_set_security(L2) returned %d", sec_err);
		}
	}

	/* Cancel any pending advertising restart since we're now connected. */
	adv_restart_deadline_ms = 0U;
}

static void disconnected_cb(struct bt_conn *conn, uint8_t reason)
{
	ARG_UNUSED(conn);

	/* Log peer address for diagnostics. */
	{
		char addr_str[BT_ADDR_LE_STR_LEN];
		const bt_addr_le_t *peer = bt_conn_get_dst(conn);

		if (peer != NULL) {
			bt_addr_le_to_str(peer, addr_str, sizeof(addr_str));
			LOG_INF("disconnected: peer=%s reason=0x%02x %s",
				addr_str, reason, bt_hci_err_to_str(reason));
		} else {
			LOG_INF("disconnected: peer=<unknown> reason=0x%02x %s",
				reason, bt_hci_err_to_str(reason));
		}
	}

	/* Notify app layer — silence alarm, update connected flag. */
	if (g_events != NULL && g_events->on_connected != NULL) {
		g_events->on_connected(false);
	}

	/* Schedule advertising restart after a short delay. */
	adv_restart_deadline_ms = k_uptime_get_32() + 5000U;
	LOG_DBG("fallback adv restart deadline set=%u", adv_restart_deadline_ms);
}

BT_CONN_CB_DEFINE(ble_core_conn_callbacks) = {
	.connected       = connected_cb,
	.disconnected    = disconnected_cb,
	.security_changed = security_changed_cb,
};

/* ---------------------------------------------------------------------------
 * Public API — ble_core_poll
 * -------------------------------------------------------------------------*/

/* NOTE: Interacting with the controller (bt_le_adv_*) from the connection
 * callback context can cause subtle driver / HCI issues on some setups.
 * Instead we use a simple deadline monitored by the main loop via
 * ble_core_poll() which runs in thread context.
 */
void ble_core_poll(void)
{
	uint32_t now = k_uptime_get_32();

	LOG_DBG("ble_core_poll: now=%u deadline=%u", now, adv_restart_deadline_ms);

	if (adv_restart_deadline_ms == 0U) {
		return;
	}

	if ((int32_t)(now - adv_restart_deadline_ms) < 0) {
		return; /* not yet */
	}

	/* Clear deadline to avoid re-entering. */
	adv_restart_deadline_ms = 0U;

	LOG_INF("poll: restart deadline reached, attempting advertising restart");

	/* Backoff: only attempt restart if we've waited long enough after
	 * previous failures.
	 */
	if (adv_next_start_allowed_ms != 0U &&
	    (int32_t)(now - adv_next_start_allowed_ms) < 0) {
		LOG_WRN("poll: restart suppressed by backoff until %u",
			adv_next_start_allowed_ms);
		return;
	}

	/* Stop advertising only if our guard says it's running. */
	if (adv_running) {
		int err2 = bt_le_adv_stop();

		if (err2 != 0 && err2 != -EALREADY) {
			LOG_WRN("bt_le_adv_stop returned %d in poll-restart", err2);
		}
		adv_running = false;
	} else {
		LOG_DBG("poll: bt_le_adv_stop skipped; adv_running=false");
	}

	if (adv_running) {
		LOG_INF("poll: advertising already running; skipping start");
		return;
	}

	int err2 = bt_le_adv_start(BLE_CORE_ADV_PARAM_IDENTITY_FAST_1,
				   ad, ARRAY_SIZE(ad),
				   sd, ARRAY_SIZE(sd));

	LOG_INF("bt_le_adv_start returned %d", err2);
	if (err2 == 0 || err2 == -EALREADY) {
		adv_running = true;
		/* Reset backoff on success. */
		adv_consecutive_failures = 0;
		adv_backoff_ms = ADV_BACKOFF_INITIAL_MS;
		adv_next_start_allowed_ms = 0U;
		LOG_INF("poll: advertising restarted after disconnect");
	} else {
		/* Start failed: increase backoff and schedule next allowed time. */
		adv_consecutive_failures++;
		if (adv_backoff_ms < ADV_BACKOFF_MAX_MS / 2U) {
			adv_backoff_ms *= 2U;
		} else {
			adv_backoff_ms = ADV_BACKOFF_MAX_MS;
		}
		adv_next_start_allowed_ms = now + adv_backoff_ms;
		LOG_ERR("poll: advertising restart failed (%d), backoff=%u ms next_allowed=%u",
			err2, adv_backoff_ms, adv_next_start_allowed_ms);
	}
}

/* ---------------------------------------------------------------------------
 * Public API — ble_core_init, ble_core_start_advertising
 * -------------------------------------------------------------------------*/

int ble_core_init(const struct ble_core_events *events)
{
	int err;

	g_events = events;
	adv_running = false;
	adv_restart_deadline_ms = 0U;
	adv_next_start_allowed_ms = 0U;
	adv_consecutive_failures = 0;
	adv_backoff_ms = ADV_BACKOFF_INITIAL_MS;

	err = bt_enable(NULL);
	if (err != 0) {
		LOG_ERR("bt_enable failed (%d)", err);
		return err;
	}

	LOG_INF("BLE stack initialised");
	return 0;
}

int ble_core_start_advertising(void)
{
	int err;

	/* Keep the local peripheral bondable for SMP pairing requests. */
	bt_set_bondable(true);
	register_auth_callbacks_once();
	register_auth_info_callbacks_once();

	if (adv_running) {
		LOG_INF("advertising already running; skipping start");
		return 0;
	}

	/* Respect any backoff window from previous failures. */
	{
		uint32_t now = k_uptime_get_32();

		if (adv_next_start_allowed_ms != 0U &&
		    (int32_t)(now - adv_next_start_allowed_ms) < 0) {
			LOG_WRN("advertising start suppressed by backoff until %u",
				adv_next_start_allowed_ms);
			return -EAGAIN;
		}
	}

	/* Load bond/settings data before starting advertising. */
	if (IS_ENABLED(CONFIG_SETTINGS)) {
		err = settings_load();
		if (err != 0) {
			LOG_ERR("settings_load failed (%d)", err);
			/* Continue — advertising can work without loaded bonds. */
		} else {
			LOG_INF("settings loaded");
		}
	}

	err = bt_le_adv_start(BLE_CORE_ADV_PARAM_IDENTITY_FAST_1,
			      ad, ARRAY_SIZE(ad),
			      sd, ARRAY_SIZE(sd));
	if (err == 0 || err == -EALREADY) {
		adv_running = true;
		adv_consecutive_failures = 0;
		adv_backoff_ms = ADV_BACKOFF_INITIAL_MS;
		adv_next_start_allowed_ms = 0U;
		LOG_INF("advertising started");
		return 0;
	}

	/* Start failed: set backoff. */
	adv_consecutive_failures++;
	if (adv_backoff_ms < ADV_BACKOFF_MAX_MS / 2U) {
		adv_backoff_ms *= 2U;
	} else {
		adv_backoff_ms = ADV_BACKOFF_MAX_MS;
	}
	adv_next_start_allowed_ms = k_uptime_get_32() + adv_backoff_ms;
	LOG_ERR("bt_le_adv_start failed (%d), backoff=%u ms", err, adv_backoff_ms);
	return err;
}
