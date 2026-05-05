/* ias_svc.c - IAS (Immediate Alert Service) callbacks
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Responsibilities (SRP):
 *   - Own the IAS callback table.
 *   - Map IAS alert levels to app actions via ias_svc_api.
 *   - Self-register at link time via BT_IAS_CB_DEFINE.
 *
 * Dependencies: struct ias_svc_api from app_api.h (set_active).
 */

#include "ias_svc.h"

#include <zephyr/bluetooth/services/ias.h>
#include <zephyr/logging/log.h>

#include "app_api.h"

LOG_MODULE_REGISTER(ias_svc);

/* ---------------------------------------------------------------------------
 * Module-private state
 * -------------------------------------------------------------------------*/

static struct ias_svc_api g_api;

/* ---------------------------------------------------------------------------
 * IAS callbacks (self-registered at link time via BT_IAS_CB_DEFINE)
 * -------------------------------------------------------------------------*/

/* Potential pitfalls:
 * - Reentrancy: runs in BT RX thread context.
 * - g_api.set_active may be NULL if bind not called; guard with check.
 * - Unknown alert level (not 0,1,2) — BT stack should not send these.
 */
static void alert_stop(void)
{
	if (g_api.set_active != NULL) {
		g_api.set_active(false, 0xFFU);
	}
	LOG_INF("IAS: alert stopped");
}

static void alert_mild(void)
{
	/* Phase-2: drive a mild alert (e.g. single LED flash). */
	LOG_INF("IAS: mild alert (phase-2 stub)");
}

static void alert_high(void)
{
	/* Phase-2: drive a high alert (e.g. sustained buzzer). */
	LOG_INF("IAS: high alert (phase-2 stub)");
}

BT_IAS_CB_DEFINE(ias_callbacks) = {
	.no_alert   = alert_stop,
	.mild_alert = alert_mild,
	.high_alert = alert_high,
};

/* ---------------------------------------------------------------------------
 * Service vtable callbacks (called by ble_service registry)
 * -------------------------------------------------------------------------*/

/* Potential pitfalls:
 * - app_api is cast from void* — must be struct app_api*.
 * - g_api stores a single function pointer; ensure it remains valid.
 * - IAS has no Zephyr bt_ias_init() — callbacks self-register via
 *   BT_IAS_CB_DEFINE, so this init just stores the app API pointer.
 */
static int ias_svc_init(void *app_api)
{
	struct app_api *api = (struct app_api *)app_api;

	if (api == NULL) {
		return -EINVAL;
	}

	g_api = api->ias;
	LOG_INF("IAS service initialised");
	return 0;
}

/* ---------------------------------------------------------------------------
 * Service instance (registered in ble_service.c)
 * -------------------------------------------------------------------------*/
struct ble_service g_ias_service = {
	.name = "ias",
	.init = ias_svc_init,
	.tick = NULL, /* IAS is purely event-driven; no per-second tick needed. */
};
