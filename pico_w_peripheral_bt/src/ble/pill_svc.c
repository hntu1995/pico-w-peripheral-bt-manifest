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
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
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
#define BT_UUID_PILL_KINDS_VAL \
	BT_UUID_128_ENCODE(0xa1b2c3d4, 0x1234, 0x4321, 0xabcd, 0xef0102030409)

static const struct bt_uuid_128 svc_uuid    = BT_UUID_INIT_128(BT_UUID_PILL_SVC_VAL);
static const struct bt_uuid_128 tbl_uuid    = BT_UUID_INIT_128(BT_UUID_PILL_TBL_VAL);
static const struct bt_uuid_128 cmd_uuid    = BT_UUID_INIT_128(BT_UUID_PILL_CMD_VAL);
static const struct bt_uuid_128 status_uuid = BT_UUID_INIT_128(BT_UUID_PILL_STATUS_VAL);
static const struct bt_uuid_128 kinds_uuid  = BT_UUID_INIT_128(BT_UUID_PILL_KINDS_VAL);

/* ---------------------------------------------------------------------------
 * Wire protocol constants
 * -------------------------------------------------------------------------*/
#define WIRE_VERSION         1U
#define WIRE_BYTES_PER_ENTRY 12U
#define CMD_ACK_ALARM        1U
#define CMD_SNOOZE_5_MIN     2U

/* Chunked write frame (protocol v2) for payloads larger than ATT limits.
 * Frame format:
 *   [ver:1][flags:1][transfer_id:1][total_len_le16:2]
 *   [chunk_offset_le16:2][chunk_len_le16:2][chunk_data:chunk_len]
 */
#define WIRE_CHUNK_VERSION         2U
#define WIRE_CHUNK_FLAG_START      0x01U
#define WIRE_CHUNK_FLAG_END        0x02U
#define WIRE_CHUNK_FLAG_ABORT      0x04U
#define WIRE_CHUNK_HEADER_SIZE     9U

#define WIRE_ALARM_TABLE_MAX_LEN (2U + ((size_t)PILL_MAX_ALARMS * WIRE_BYTES_PER_ENTRY))

/* Wire-format constants for pill-kind name table */
#define WIRE_KINDS_ENTRY_NAME_LEN PILL_KIND_NAME_MAX_LEN
#define WIRE_KINDS_ENTRY_SIZE (1U + WIRE_KINDS_ENTRY_NAME_LEN)
#define WIRE_KIND_TABLE_MAX_LEN (2U + ((size_t)PILL_MAX_KINDS * WIRE_KINDS_ENTRY_SIZE))

/* ---------------------------------------------------------------------------
 * Module-private state
 * -------------------------------------------------------------------------*/
static const struct bt_gatt_attr *g_status_attr;
static struct alarm_ctrl_status   g_last_notified;
static bool                       g_notify_enabled;
static const struct alarm_ctrl_api *g_api;

struct pill_chunk_rx_state {
	bool active;
	uint8_t transfer_id;
	uint16_t total_len;
	uint16_t received_len;
};

static struct pill_chunk_rx_state g_alarm_chunk_rx;
static struct pill_chunk_rx_state g_kinds_chunk_rx;
static uint8_t g_alarm_chunk_buf[WIRE_ALARM_TABLE_MAX_LEN];
static uint8_t g_kinds_chunk_buf[WIRE_KIND_TABLE_MAX_LEN];

/* Notification rate-limiting / backoff state. */
static uint32_t g_last_notify_ms = 0U;
static uint32_t g_notify_interval_ms = 1000U; /* default 1s */
static uint8_t  g_notify_failures = 0U;
#define PILL_NOTIFY_FAILURE_THRESHOLD 3U
#define PILL_NOTIFY_BACKOFF_MAX_MS 60000U

LOG_MODULE_REGISTER(pill_svc);

/*
 * Pitfalls:
 * - Reentrancy/races: state is mutable shared module state; run in BT context.
 * - Buffer safety: keep all write lengths bounded by uint16_t and storage_len.
 * - Error handling: caller decides ATT error mapping.
 */
static uint16_t read_le16(const uint8_t *p)
{
	return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

/*
 * Pitfalls:
 * - Reentrancy/races: do not partially clear state.
 * - Error handling: resets must be safe for abort/restart paths.
 */
static void chunk_rx_reset(struct pill_chunk_rx_state *state)
{
	if (state == NULL) {
		return;
	}

	state->active = false;
	state->transfer_id = 0U;
	state->total_len = 0U;
	state->received_len = 0U;
}

/*
 * Pitfalls:
 * - Reentrancy/races: state/storage must be per-characteristic.
 * - Blocking calls: none; pure parse/copy path.
 * - Buffer overflows: bounds checked for frame, offset and cumulative length.
 * - Missing error handling: returns -EINPROGRESS for partial frames.
 */
static int process_chunked_payload(struct pill_chunk_rx_state *state,
					   uint8_t *storage,
					   size_t storage_len,
					   const void *buf,
					   uint16_t len,
					   const uint8_t **full_payload,
					   uint16_t *full_len)
{
	const uint8_t *frame = buf;
	uint8_t flags;
	uint8_t transfer_id;
	uint16_t total_len;
	uint16_t chunk_offset;
	uint16_t chunk_len;
	bool is_start;
	bool is_end;
	bool is_abort;

	if (state == NULL || storage == NULL || buf == NULL || full_payload == NULL || full_len == NULL) {
		return -EINVAL;
	}

	if (len < WIRE_CHUNK_HEADER_SIZE) {
		return -EINVAL;
	}

	if (frame[0] != WIRE_CHUNK_VERSION) {
		return -ENOTSUP;
	}

	flags = frame[1];
	transfer_id = frame[2];
	total_len = read_le16(&frame[3]);
	chunk_offset = read_le16(&frame[5]);
	chunk_len = read_le16(&frame[7]);
	is_start = (flags & WIRE_CHUNK_FLAG_START) != 0U;
	is_end = (flags & WIRE_CHUNK_FLAG_END) != 0U;
	is_abort = (flags & WIRE_CHUNK_FLAG_ABORT) != 0U;

	if ((size_t)WIRE_CHUNK_HEADER_SIZE + (size_t)chunk_len != (size_t)len) {
		return -EINVAL;
	}

	if (is_abort) {
		chunk_rx_reset(state);
		return -ECANCELED;
	}

	if (total_len == 0U || (size_t)total_len > storage_len) {
		return -EMSGSIZE;
	}

	if (is_start) {
		if (chunk_offset != 0U) {
			return -EINVAL;
		}

		state->active = true;
		state->transfer_id = transfer_id;
		state->total_len = total_len;
		state->received_len = 0U;
	} else {
		if (!state->active) {
			return -EINVAL;
		}
		if (state->transfer_id != transfer_id) {
			return -EINVAL;
		}
		if (state->total_len != total_len) {
			return -EINVAL;
		}
	}

	if (chunk_offset != state->received_len) {
		return -EINVAL;
	}

	if ((uint32_t)chunk_offset + (uint32_t)chunk_len > (uint32_t)state->total_len) {
		return -EINVAL;
	}

	if (chunk_len > 0U) {
		memcpy(&storage[chunk_offset], &frame[WIRE_CHUNK_HEADER_SIZE], chunk_len);
	}

	state->received_len = (uint16_t)(state->received_len + chunk_len);

	if (is_end && state->received_len != state->total_len) {
		return -EINVAL;
	}

	if (state->received_len < state->total_len) {
		return -EINPROGRESS;
	}

	*full_payload = storage;
	*full_len = state->total_len;
	chunk_rx_reset(state);
	return 0;
}

/* Resolve a kind bitmask to a readable comma-separated name list.
 * Falls back to kind index labels when names are missing.
 */
static void format_kind_mask(uint64_t kind_mask, char *out, size_t out_len)
{
	const struct pill_kind_table *ktbl;
	size_t used = 0U;
	bool first = true;
	uint8_t bit;

	if (out == NULL || out_len == 0U) {
		return;
	}

	out[0] = '\0';

	if (kind_mask == 0U) {
		(void)snprintk(out, out_len, "none");
		return;
	}

	ktbl = (g_api != NULL) ? g_api->get_kind_table() : NULL;

	for (bit = 0U; bit < PILL_MAX_KINDS; bit++) {
		char label[PILL_KIND_NAME_MAX_LEN + 8U];
		bool found = false;
		uint8_t j;

		if ((kind_mask & (1ULL << bit)) == 0U) {
			continue;
		}

		if (ktbl != NULL) {
			for (j = 0U; j < ktbl->count; j++) {
				if (ktbl->entries[j].id != bit) {
					continue;
				}

				/* Names are fixed-size arrays; trim at first NUL. */
				size_t n = 0U;
				while (n < PILL_KIND_NAME_MAX_LEN && ktbl->entries[j].name[n] != '\0') {
					n++;
				}

				if (n > 0U) {
					size_t copy_len = MIN(n, sizeof(label) - 1U);
					memcpy(label, ktbl->entries[j].name, copy_len);
					label[copy_len] = '\0';
				} else {
					(void)snprintk(label, sizeof(label), "kind%u", bit);
				}

				found = true;
				break;
			}
		}

		if (!found) {
			(void)snprintk(label, sizeof(label), "kind%u", bit);
		}

		if (used < out_len) {
			int wrote = snprintk(out + used, out_len - used, "%s%s", first ? "" : ",", label);
			if (wrote > 0) {
				used += (size_t)wrote;
			}
		}

		first = false;
	}
}

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
		char kinds_str[128];

		format_kind_mask(a->pill_kind, kinds_str, sizeof(kinds_str));
		LOG_INF("  [%u] %02u:%02u weekdays=0x%02x kinds=%s (0x%016llx) enabled=%u",
			i, a->hour, a->minute, a->weekday_mask,
			kinds_str, (unsigned long long)a->pill_kind, a->enabled);
	}
}

/* Helper: print received alarm-table payload entry-by-entry before any
 * validation/commit so malformed sync frames are still visible in logs.
 */
static void log_alarm_table_payload(const uint8_t *buf, uint16_t len)
{
	uint8_t count;
	size_t expected;
	uint8_t i;

	if (buf == NULL) {
		LOG_WRN("alarm payload: <null>");
		return;
	}

	if (len < 2U) {
		LOG_WRN("alarm payload too short: len=%u", (unsigned)len);
		return;
	}

	count = buf[1];
	expected = 2U + ((size_t)count * WIRE_BYTES_PER_ENTRY);
	LOG_INF("sync alarm payload: ver=%u count=%u len=%u expected=%u",
		buf[0], count, (unsigned)len, (unsigned)expected);

	if (expected != (size_t)len) {
		LOG_WRN("sync alarm payload size mismatch (len=%u expected=%u)",
			(unsigned)len, (unsigned)expected);
		return;
	}

	for (i = 0U; i < count; i++) {
		size_t pos = 2U + ((size_t)i * WIRE_BYTES_PER_ENTRY);
		uint8_t hour = buf[pos + 0U];
		uint8_t minute = buf[pos + 1U];
		uint8_t weekday = buf[pos + 2U];
		uint64_t kinds = ((uint64_t)buf[pos + 3U]) |
			((uint64_t)buf[pos + 4U] << 8) |
			((uint64_t)buf[pos + 5U] << 16) |
			((uint64_t)buf[pos + 6U] << 24) |
			((uint64_t)buf[pos + 7U] << 32) |
			((uint64_t)buf[pos + 8U] << 40) |
			((uint64_t)buf[pos + 9U] << 48) |
			((uint64_t)buf[pos + 10U] << 56);
		uint8_t enabled = buf[pos + 11U];

		LOG_INF("  rx[%u] %02u:%02u weekdays=0x%02x kinds=0x%016llx enabled=%u",
			i, hour, minute, weekday, (unsigned long long)kinds, enabled);
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
		out[pos + 3U] = (uint8_t)(a->pill_kind & 0xFFULL);
		out[pos + 4U] = (uint8_t)((a->pill_kind >> 8) & 0xFFULL);
		out[pos + 5U] = (uint8_t)((a->pill_kind >> 16) & 0xFFULL);
		out[pos + 6U] = (uint8_t)((a->pill_kind >> 24) & 0xFFULL);
		out[pos + 7U] = (uint8_t)((a->pill_kind >> 32) & 0xFFULL);
		out[pos + 8U] = (uint8_t)((a->pill_kind >> 40) & 0xFFULL);
		out[pos + 9U] = (uint8_t)((a->pill_kind >> 48) & 0xFFULL);
		out[pos + 10U] = (uint8_t)((a->pill_kind >> 56) & 0xFFULL);
		out[pos + 11U] = a->enabled;
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
		a.pill_kind    = ((uint64_t)buf[pos + 3U]) |
				((uint64_t)buf[pos + 4U] << 8) |
				((uint64_t)buf[pos + 5U] << 16) |
				((uint64_t)buf[pos + 6U] << 24) |
				((uint64_t)buf[pos + 7U] << 32) |
				((uint64_t)buf[pos + 8U] << 40) |
				((uint64_t)buf[pos + 9U] << 48) |
				((uint64_t)buf[pos + 10U] << 56);
		a.enabled      = buf[pos + 11U];

		if (pill_alarm_table_set(&parsed, i, &a) != 0) {
			return -EINVAL;
		}
	}

	/* Print every received sync entry before committing to live state. */
	LOG_INF("sync alarm table received: count=%u", parsed.count);
	log_alarm_table(&parsed);

	*tbl = parsed;
	return pill_app_settings_save_alarms(tbl);
}

/* ---------------------------------------------------------------------------
 * Kind-name table encode / decode
 * -------------------------------------------------------------------------*/
static int encode_kinds(uint8_t *out, size_t out_len, size_t *enc_len)
{
	const struct pill_kind_table *ktbl = g_api->get_kind_table();
	size_t entry_size = WIRE_KINDS_ENTRY_SIZE;
	size_t required = 2U + ((size_t)ktbl->count * entry_size);
	uint8_t i;

	if (out_len < required) {
		return -ENOMEM;
	}

	out[0] = WIRE_VERSION;
	out[1] = ktbl->count;

	for (i = 0U; i < ktbl->count; i++) {
		size_t pos = 2U + ((size_t)i * entry_size);
		const struct pill_kind_entry *e = &ktbl->entries[i];

		out[pos + 0U] = e->id;
		memcpy(&out[pos + 1U], e->name, PILL_KIND_NAME_MAX_LEN);
	}

	*enc_len = required;
	return 0;
}

static int decode_kinds(const uint8_t *buf, uint16_t len)
{
	struct pill_kind_table *ktbl = g_api->get_kind_table();
	/* Declare as static to avoid a ~1345-byte stack allocation inside the
	 * BT RX callback. GATT write callbacks are serialised (single BT thread),
	 * so a static local is safe here. */
	static struct pill_kind_table parsed;
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
	if (count > PILL_MAX_KINDS) {
		return -EINVAL;
	}

	expected = 2U + ((size_t)count * WIRE_KINDS_ENTRY_SIZE);
	if (expected != len) {
		return -EINVAL;
	}

	(void)memset(&parsed, 0, sizeof(parsed));
	for (i = 0U; i < count; i++) {
		size_t pos = 2U + ((size_t)i * WIRE_KINDS_ENTRY_SIZE);
		parsed.entries[i].id = buf[pos + 0U];
		memcpy(parsed.entries[i].name, &buf[pos + 1U], PILL_KIND_NAME_MAX_LEN);
		if (parsed.entries[i].id >= PILL_MAX_KINDS) {
			return -EINVAL;
		}
	}
	parsed.count = count;

	*ktbl = parsed;
	return pill_app_settings_save_kinds(ktbl);
}

/* ---------------------------------------------------------------------------
 * GATT characteristic callbacks
 * -------------------------------------------------------------------------*/
static ssize_t read_alarm_table(struct bt_conn *conn,
				const struct bt_gatt_attr *attr,
				void *buf, uint16_t len, uint16_t offset)
{
	static uint8_t payload[2U + (PILL_MAX_ALARMS * WIRE_BYTES_PER_ENTRY)];
	size_t  payload_len;
	int     err;

	ARG_UNUSED(attr);

	err = encode_table(payload, sizeof(payload), &payload_len);
	if (err != 0) {
		return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
	}

	return bt_gatt_attr_read(conn, attr, buf, len, offset, payload, payload_len);
}

static ssize_t read_kinds(struct bt_conn *conn,
				const struct bt_gatt_attr *attr,
				void *buf, uint16_t len, uint16_t offset)
{
	static uint8_t payload[2U + (PILL_MAX_KINDS * WIRE_KINDS_ENTRY_SIZE)];
	size_t payload_len;
	int err;

	ARG_UNUSED(attr);

	err = encode_kinds(payload, sizeof(payload), &payload_len);
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
	const uint8_t *payload = buf;
	uint16_t payload_len = len;

	/* Log writer and payload length for diagnostics. */
	{
		char addr_str[BT_ADDR_LE_STR_LEN];
		const bt_addr_le_t *peer = bt_conn_get_dst(conn);
		if (peer != NULL) {
			bt_addr_le_to_str(peer, addr_str, sizeof(addr_str));
			LOG_INF("write_alarm_table from %s len=%u", addr_str, (unsigned)len);
		} else {
			LOG_INF("write_alarm_table from <unknown> len=%u", (unsigned)len);
		}
	}
	ARG_UNUSED(attr);
	ARG_UNUSED(flags);

	if (offset != 0U) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}

	if (len > 0U && ((const uint8_t *)buf)[0] == WIRE_CHUNK_VERSION) {
		err = process_chunked_payload(&g_alarm_chunk_rx,
					      g_alarm_chunk_buf,
					      sizeof(g_alarm_chunk_buf),
					      buf,
					      len,
					      &payload,
					      &payload_len);
		if (err == -ECANCELED) {
			LOG_INF("write_alarm_table chunk transfer aborted");
			return len;
		}
		if (err == -EINPROGRESS) {
			LOG_DBG("write_alarm_table chunk accepted (%u/%u)",
				(unsigned)g_alarm_chunk_rx.received_len,
				(unsigned)g_alarm_chunk_rx.total_len);
			return len;
		}
		if (err != 0) {
			LOG_WRN("write_alarm_table chunk parse failed (%d)", err);
			chunk_rx_reset(&g_alarm_chunk_rx);
			return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
		}

		LOG_INF("write_alarm_table chunk transfer complete len=%u", (unsigned)payload_len);
	}

	/* Always print received payload entries for sync diagnostics. */
	log_alarm_table_payload(payload, payload_len);

	/* Pre-validate raw wire payload before attempting decode/commit. */
	err = pill_ble_validate_alarm_table(payload, payload_len);
	if (err == -ENOTSUP) {
		LOG_WRN("write_alarm_table rejected: unsupported wire version");
		return BT_GATT_ERR(BT_ATT_ERR_NOT_SUPPORTED);
	}
	if (err != 0) {
		LOG_WRN("write_alarm_table rejected by validator (%d)", err);
		return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
	}

	/* Delegate to existing decode/commit path (keeps single source of truth
	 * for applying changes to alarm table and settings). */
	err = decode_table(payload, payload_len);
	if (err == -ENOTSUP) {
		LOG_WRN("write_alarm_table decode failed: unsupported wire version");
		return BT_GATT_ERR(BT_ATT_ERR_NOT_SUPPORTED);
	}
	if (err != 0) {
		LOG_WRN("write_alarm_table decode failed (%d)", err);
		return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
	}

	/* Log the newly written schedule for visibility */
	if (g_api != NULL) {
		log_alarm_table(g_api->get_table());
	}

	return len;
}

static ssize_t write_kinds(struct bt_conn *conn,
				 const struct bt_gatt_attr *attr,
				 const void *buf, uint16_t len,
				 uint16_t offset, uint8_t flags)
{
	int err;
	const uint8_t *payload = buf;
	uint16_t payload_len = len;

	/* Log writer and payload length for diagnostics. */
	{
		char addr_str[BT_ADDR_LE_STR_LEN];
		const bt_addr_le_t *peer = bt_conn_get_dst(conn);
		if (peer != NULL) {
			bt_addr_le_to_str(peer, addr_str, sizeof(addr_str));
			LOG_INF("write_kinds from %s len=%u", addr_str, (unsigned)len);
		} else {
			LOG_INF("write_kinds from <unknown> len=%u", (unsigned)len);
		}
	}
	ARG_UNUSED(attr);
	ARG_UNUSED(flags);

	if (offset != 0U) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}

	if (len > 0U && ((const uint8_t *)buf)[0] == WIRE_CHUNK_VERSION) {
		err = process_chunked_payload(&g_kinds_chunk_rx,
					      g_kinds_chunk_buf,
					      sizeof(g_kinds_chunk_buf),
					      buf,
					      len,
					      &payload,
					      &payload_len);
		if (err == -ECANCELED) {
			LOG_INF("write_kinds chunk transfer aborted");
			return len;
		}
		if (err == -EINPROGRESS) {
			LOG_DBG("write_kinds chunk accepted (%u/%u)",
				(unsigned)g_kinds_chunk_rx.received_len,
				(unsigned)g_kinds_chunk_rx.total_len);
			return len;
		}
		if (err != 0) {
			LOG_WRN("write_kinds chunk parse failed (%d)", err);
			chunk_rx_reset(&g_kinds_chunk_rx);
			return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
		}

		LOG_INF("write_kinds chunk transfer complete len=%u", (unsigned)payload_len);
	}

	/* Pre-validate raw wire payload before attempting decode/commit. */
	err = pill_ble_validate_kind_table(payload, payload_len);
	if (err == -ENOTSUP) {
		return BT_GATT_ERR(BT_ATT_ERR_NOT_SUPPORTED);
	}
	if (err != 0) {
		return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
	}

	/* Delegate to decode/commit path */
	err = decode_kinds(payload, payload_len);
	if (err == -ENOTSUP) {
		return BT_GATT_ERR(BT_ATT_ERR_NOT_SUPPORTED);
	}
	if (err != 0) {
		return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
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

	/* Log writer and command for diagnostics. */
	{
		char addr_str[BT_ADDR_LE_STR_LEN];
		const bt_addr_le_t *peer = bt_conn_get_dst(conn);
		if (peer != NULL) {
			bt_addr_le_to_str(peer, addr_str, sizeof(addr_str));
			LOG_INF("write_command from %s len=%u cmd=0x%02x", addr_str, (unsigned)len, (len>0)?cmd[0]:0);
		} else {
			LOG_INF("write_command from <unknown> len=%u cmd=0x%02x", (unsigned)len, (len>0)?cmd[0]:0);
		}
	}
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
	LOG_INF("pill_svc: status CCC %s", g_notify_enabled ? "enabled" : "disabled");
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
	BT_GATT_CHARACTERISTIC(&kinds_uuid.uuid,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
			       BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT,
			       read_kinds, write_kinds, NULL),
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

	uint32_t now = k_uptime_get_32();
	if (g_last_notify_ms != 0U && (int32_t)(now - g_last_notify_ms) < (int32_t)g_notify_interval_ms) {
		LOG_DBG("pill_svc: notify suppressed by rate limit (delta=%u ms)", (unsigned)(now - g_last_notify_ms));
		return;
	}

	/* Log status contents before notifying for visibility */
	LOG_INF("notify status: active=%u batt=%u conn=%u low=%u idx=%u",
			status->active_alarm,
			status->battery_percent,
			status->connected,
			status->low_battery,
			status->active_alarm_index);

	int rc = bt_gatt_notify(NULL, g_status_attr, status, sizeof(*status));
	if (rc == -ENOTCONN) {
		LOG_DBG("pill_svc: notify: no connection");
		g_notify_failures++;
	} else if (rc != 0) {
		LOG_WRN("pill_svc: bt_gatt_notify returned %d", rc);
		g_notify_failures++;
	} else {
		/* Success: update last-notified snapshot and reset any backoff. */
		g_last_notified = *status;
		g_last_notify_ms = now;
		g_notify_failures = 0U;
		if (g_notify_interval_ms > 1000U) {
			g_notify_interval_ms = 1000U;
			LOG_DBG("pill_svc: notify interval restored to %u ms", (unsigned)g_notify_interval_ms);
		}
		return;
	}

	/* Failure path: apply exponential backoff once failures exceed threshold. */
	if (g_notify_failures >= PILL_NOTIFY_FAILURE_THRESHOLD) {
		uint32_t new_interval = g_notify_interval_ms * 2U;
		if (new_interval > PILL_NOTIFY_BACKOFF_MAX_MS) {
			new_interval = PILL_NOTIFY_BACKOFF_MAX_MS;
		}
		if (new_interval != g_notify_interval_ms) {
			g_notify_interval_ms = new_interval;
			LOG_WRN("pill_svc: notify backoff increased to %u ms (failures=%u)",
					(unsigned)g_notify_interval_ms, (unsigned)g_notify_failures);
		}
	} else {
		LOG_DBG("pill_svc: notify failure count=%u", (unsigned)g_notify_failures);
	}
}

#endif /* CONFIG_PILL_BLE_SERVICE */
