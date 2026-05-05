/* app_api.c - Build aggregated app API from alarm_ctrl_api
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stddef.h>

#include "app_api.h"

struct app_api app_api_build(const struct alarm_ctrl_api *alarm_api)
{
	struct app_api api;

	/* Zero-initialise everything to catch missing assignments. */
	api.cts.set_time_base = NULL;
	api.cts.get_epoch_s   = NULL;
	api.ias.set_active    = NULL;
	api.pill.set_active      = NULL;
	api.pill.is_active       = NULL;
	api.pill.get_active_idx  = NULL;
	api.pill.get_table       = NULL;
	api.pill.get_kind_table  = NULL;
	api.pill.get_status      = NULL;

	if (alarm_api == NULL) {
		return api;
	}

	/* CTS service only needs time-base functions. */
	api.cts.set_time_base = alarm_api->set_time_base;
	api.cts.get_epoch_s   = alarm_api->get_epoch_s;

	/* IAS service only needs set_active. */
	api.ias.set_active = alarm_api->set_active;

	/* Pill service needs the full alarm control interface. */
	api.pill.set_active     = alarm_api->set_active;
	api.pill.is_active      = alarm_api->is_active;
	api.pill.get_active_idx = alarm_api->get_active_idx;
	api.pill.get_table      = alarm_api->get_table;
	api.pill.get_kind_table = alarm_api->get_kind_table;
	api.pill.get_status     = alarm_api->get_status;

	return api;
}
