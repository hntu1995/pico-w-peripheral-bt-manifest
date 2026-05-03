/* pill_svc.c - Custom pill-alarm BLE GATT service implementation
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Responsibilities (SRP):
 *   - Own the BLE GATT service definition (alarm table, command, status).
 *   - Handle alarm-table wire-protocol encode / decode.
 *   - Send status change notifications to subscribed centrals.
 *   - Delegate all domain logic to alarm_ctrl.
 */

#include "pill_svc.h"

#if defined(CONFIG_PILL_BLE_SERVICE) && (CONFIG_PILL_BLE_SERVICE == 1)

#include <errno.h>
#include <string.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/att.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include "pill/alarm_ctrl.h"
#include "pill/alarm_model.h"
#include "pill/app_settings.h"
#include "pill/ble_validation.h"

/* ---------------------------------------------------------------------------
 * UUID definitions
 * -------------------------------------------------------------------------*/
#define BT_UUID_PILL_SVC_VAL \
	BT_UUID_128_ENCODE(0xa1b2c3d4, 0x1234, 0x4321, 0xabcd, 0xef0102030405)
#define BT_UUID_PILL_TBL_VAL \
	BT_UUID_128_ENCODE(0xa1b2c3d4, 0x1234, 0x4321, 0xabcd, 0xef0102030406)
#define BT_UUID_PILL_CMD_VAL \
	BT_UUID_128_ENCODE(0xa1b2c3d4, 0x1234, 0x4321, 0xabcd, 0xef0102030407)
#define BT_UUID_PILL_STATUS_VAL \
	BT_UUID_128_ENCODE(0xa1b2c3d4, 0x1234, 0x4321, 0xabcd, 0xef0102030408)

static const struct bt_uuid_128 svc_uuid    = BT_UUID_INIT_128(BT_UUID_PILL_SVC_VAL);
static const struct bt_uuid_128 tbl_uuid    = BT_UUID_INIT_128(BT_UUID_PILL_TBL_VAL);
static const struct bt_uuid_128 cmd_uuid    = BT_UUID_INIT_128(BT_UUID_PILL_CMD_VAL);
static const struct bt_uuid_128 status_uuid = BT_UUID_INIT_128(BT_UUID_PILL_STATUS_VAL);

/* ---------------------------------------------------------------------------
 * Wire protocol constants
 * -------------------------------------------------------------------------*/
#define WIRE_VERSION         1U
#define WIRE_BYTES_PER_ENTRY 5U
#define CMD_ACK_ALARM        1U
#define CMD_SNOOZE_5_MIN     2U

/* ---------------------------------------------------------------------------
 * Module-private state
 * -------------------------------------------------------------------------*/
static const struct bt_gatt_attr *g_status_attr;
static struct alarm_ctrl_status   g_last_notified;
static bool                       g_notify_enabled;
static const struct alarm_ctrl_api *g_api;

LOG_MODULE_REGISTER(pill_svc);

/* Helper: log an alarm table in human readable form. */
static void log_alarm_table(const struct pill_alarm_table *tbl)
{
	uint8_t i;

	if (tbl == NULL) {
		LOG_INF("alarm table: <null>");
		return;
	}

	LOG_INF("alarm table: count=%u", tbl->count);
	for (i = 0U; i < tbl->count; i++) {
		const struct pill_alarm *a = &tbl->entries[i];
		LOG_INF("  [%u] %02u:%02u weekdays=0x%02x kind=%u enabled=%u",
				i, a->hour, a->minute, a->weekday_mask,
				a->pill_kind, a->enabled);
	}
}

/* ---------------------------------------------------------------------------
 * Wire protocol helpers
 * -------------------------------------------------------------------------*/
static int encode_table(uint8_t *out, size_t out_len, size_t *enc_len)
{
	const struct pill_alarm_table *tbl = g_api->get_table();
	size_t required = 2U + ((size_t)tbl->count * WIRE_BYTES_PER_ENTRY);
	uint8_t i;

	if (out_len < required) {
		return -ENOMEM;
	}

	out[0] = WIRE_VERSION;
	out[1] = tbl->count;

	for (i = 0U; i < tbl->count; i++) {
		size_t pos              = 2U + ((size_t)i * WIRE_BYTES_PER_ENTRY);
		const struct pill_alarm *a = &tbl->entries[i];

		out[pos + 0U] = a->hour;
		out[pos + 1U] = a->minute;
		out[pos + 2U] = a->weekday_mask;
		out[pos + 3U] = a->pill_kind;
		out[pos + 4U] = a->enabled;
	}

	/* Log the schedule being encoded and sent to clients */
	log_alarm_table(tbl);

	*enc_len = required;
	return 0;
}

static int decode_table(const uint8_t *buf, uint16_t len)
{
	struct pill_alarm_table *tbl = g_api->get_table();
	struct pill_alarm_table  parsed;
	size_t expected;
	uint8_t count;
	uint8_t i;

	if (len < 2U) {
		return -EINVAL;
	}
	if (buf[0] != WIRE_VERSION) {
		return -ENOTSUP;
	}

	count = buf[1];
	if (count > PILL_MAX_ALARMS) {
		return -EINVAL;
	}

	expected = 2U + ((size_t)count * WIRE_BYTES_PER_ENTRY);
	if (expected != len) {
		return -EINVAL;
	}

	pill_alarm_table_clear(&parsed);
	for (i = 0U; i < count; i++) {
		struct pill_alarm a;
		size_t pos = 2U + ((size_t)i * WIRE_BYTES_PER_ENTRY);

		a.hour         = buf[pos + 0U];
		a.minute       = buf[pos + 1U];
		a.weekday_mask = buf[pos + 2U];
		a.pill_kind    = buf[pos + 3U];
		a.enabled      = buf[pos + 4U];

		if (pill_alarm_table_set(&parsed, i, &a) != 0) {
			return -EINVAL;
		}
	}

	*tbl = parsed;
	return pill_app_settings_save_alarms(tbl);
}

/* ---------------------------------------------------------------------------
 * GATT characteristic callbacks
 * -------------------------------------------------------------------------*/
static ssize_t read_alarm_table(struct bt_conn *conn,
				const struct bt_gatt_attr *attr,
				void *buf, uint16_t len, uint16_t offset)
{
	uint8_t payload[2U + (PILL_MAX_ALARMS * WIRE_BYTES_PER_ENTRY)];
	size_t  payload_len;
	int     err;

	ARG_UNUSED(attr);

	err = encode_table(payload, sizeof(payload), &payload_len);
	if (err != 0) {
		return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
	}

	return bt_gatt_attr_read(conn, attr, buf, len, offset, payload, payload_len);
}

static ssize_t write_alarm_table(struct bt_conn *conn,
				 const struct bt_gatt_attr *attr,
				 const void *buf, uint16_t len,
				 uint16_t offset, uint8_t flags)
{
	int err;

	ARG_UNUSED(conn);
	ARG_UNUSED(attr);
	ARG_UNUSED(flags);

	if (offset != 0U) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}

	/* Pre-validate raw wire payload before attempting decode/commit. */
	err = pill_ble_validate_alarm_table(buf, len);
	if (err == -ENOTSUP) {
		return BT_GATT_ERR(BT_ATT_ERR_NOT_SUPPORTED);
	}
	if (err != 0) {
		return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
	}

	/* Delegate to existing decode/commit path (keeps single source of truth
	 * for applying changes to alarm table and settings). */
	err = decode_table(buf, len);
	if (err == -ENOTSUP) {
		return BT_GATT_ERR(BT_ATT_ERR_NOT_SUPPORTED);
	}
	if (err != 0) {
		return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
	}

	/* Log the newly written schedule for visibility */
	if (g_api != NULL) {
		log_alarm_table(g_api->get_table());
	}

	return len;
}

static ssize_t write_command(struct bt_conn *conn,
			     const struct bt_gatt_attr *attr,
			     const void *buf, uint16_t len,
			     uint16_t offset, uint8_t flags)
{
	const uint8_t          *cmd = buf;
	struct pill_alarm_table *tbl;
	struct pill_alarm        snooze;
	uint8_t                  active_idx;
	int                      err;

	ARG_UNUSED(conn);
	ARG_UNUSED(attr);
	ARG_UNUSED(flags);

	if (offset != 0U) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}
	if (len != 1U) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}

	if (*cmd == CMD_ACK_ALARM) {
		g_api->set_active(false, 0xFFU);
		return len;
	}

	if (*cmd == CMD_SNOOZE_5_MIN) {
		tbl        = g_api->get_table();
		active_idx = g_api->get_active_idx();

		if (!g_api->is_active() || active_idx >= tbl->count) {
			return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
		}

		snooze        = tbl->entries[active_idx];
		snooze.minute = (uint8_t)((snooze.minute + 5U) % 60U);

		err = pill_alarm_table_set(tbl, active_idx, &snooze);
		if (err != 0) {
			return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
		}
		(void)pill_app_settings_save_alarms(tbl);
		g_api->set_active(false, 0xFFU);
		return len;
	}

	return BT_GATT_ERR(BT_ATT_ERR_NOT_SUPPORTED);
}

static ssize_t read_status(struct bt_conn *conn,
			   const struct bt_gatt_attr *attr,
			   void *buf, uint16_t len, uint16_t offset)
{
	const struct alarm_ctrl_status *s = g_api->get_status();

	return bt_gatt_attr_read(conn, attr, buf, len, offset, s, sizeof(*s));
}

static void status_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	ARG_UNUSED(attr);
	g_notify_enabled = (value == BT_GATT_CCC_NOTIFY);
}

/* ---------------------------------------------------------------------------
 * GATT service definition (linker-section registered, no init call needed)
 * -------------------------------------------------------------------------*/
BT_GATT_SERVICE_DEFINE(pill_svc,
	BT_GATT_PRIMARY_SERVICE(&svc_uuid),
	BT_GATT_CHARACTERISTIC(&tbl_uuid.uuid,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
			       BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT,
			       read_alarm_table, write_alarm_table, NULL),
	BT_GATT_CHARACTERISTIC(&cmd_uuid.uuid,
			       BT_GATT_CHRC_WRITE_WITHOUT_RESP,
			       BT_GATT_PERM_WRITE_ENCRYPT,
			       NULL, write_command, NULL),
	BT_GATT_CHARACTERISTIC(&status_uuid.uuid,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ,
			       read_status, NULL, NULL),
	BT_GATT_CCC(status_ccc_changed,
		    BT_GATT_PERM_READ | BT_GATT_PERM_WRITE_ENCRYPT),
);

/* ---------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------*/
int pill_svc_init(const struct alarm_ctrl_api *api)
{
	g_api = api;
	if (g_api == NULL) {
		return -EINVAL;
	}
	g_status_attr = bt_gatt_find_by_uuid(pill_svc.attrs, pill_svc.attr_count,
						 &status_uuid.uuid);
	memset(&g_last_notified, 0xFF, sizeof(g_last_notified));
	return 0;
}

void pill_svc_notify_status(const struct alarm_ctrl_status *status)
{
	if (!g_notify_enabled || g_status_attr == NULL) {
		return;
	}
	if (memcmp(status, &g_last_notified, sizeof(*status)) == 0) {
		return;
	}

	/* Log status contents before notifying for visibility */
	LOG_INF("notify status: active=%u batt=%u conn=%u low=%u idx=%u",
			status->active_alarm,
			status->battery_percent,
			status->connected,
			status->low_battery,
			status->active_alarm_index);

	g_last_notified = *status;
	(void)bt_gatt_notify(NULL, g_status_attr, status, sizeof(*status));
}

#endif /* CONFIG_PILL_BLE_SERVICE */
