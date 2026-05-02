/* ias_svc.c - IAS (Immediate Alert Service) callbacks
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Responsibilities (SRP):
 *   - Own the IAS callback table.
 *   - Map IAS alert levels to alarm_ctrl actions.
 *   - Self-register at link time via BT_IAS_CB_DEFINE (no init needed).
 */

#include "ias_svc.h"

#include <zephyr/bluetooth/services/ias.h>
#include <zephyr/sys/printk.h>

#include "pill/alarm_ctrl.h"

static const struct alarm_ctrl_api *g_api;

static void alert_stop(void)
{
	if (g_api != NULL) {
		g_api->set_active(false, 0xFFU);
	} else {
		alarm_ctrl_set_active(false, 0xFFU);
	}
	printk("IAS: alert stopped\n");
}

static void alert_mild(void)
{
	/* Phase-2: drive a mild alert (e.g. single LED flash). */
	printk("IAS: mild alert\n");
}

static void alert_high(void)
{
	/* Phase-2: drive a high alert (e.g. sustained buzzer). */
	printk("IAS: high alert\n");
}

BT_IAS_CB_DEFINE(ias_callbacks) = {
	.no_alert   = alert_stop,
	.mild_alert = alert_mild,
	.high_alert = alert_high,
};

void ias_svc_bind_api(const struct alarm_ctrl_api *api)
{
	g_api = api;
}
