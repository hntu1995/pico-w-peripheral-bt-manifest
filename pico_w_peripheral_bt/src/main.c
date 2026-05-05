/* main.c - Smart Pill Alarm entry point for Raspberry Pi Pico W
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Responsibilities (SRP):
 *   - Init all subsystems in the correct order.
 *   - Run the 1-second application tick loop.
 *   - Route tick results to BLE service outputs.
 *
 * Architecture:
 *   BLE framework (ble_core + ble_service) handles stack init,
 *   advertising, connection management, and service lifecycle.
 *   Each service is a self-contained module in ble/services/<svc>/.
 *
 *   Adding/removing a service:
 *     1. Create/delete directory with svc.h + svc.c
 *     2. Edit registry array in ble_service.c
 *     3. Add/remove source file in CMakeLists.txt
 *     4. No changes to main.c needed.
 */

#include <zephyr/bluetooth/services/bas.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "app_api.h"
#include "ble/ble_core.h"
#include "ble/ble_service.h"
#include "pill/alarm_ctrl.h"
#include "pill/pill_hw.h"
#include "pill/display.h"
#include "pill/leds.h"

LOG_MODULE_REGISTER(main);

/* ---------------------------------------------------------------------------
 * BLE core event callbacks
 * -------------------------------------------------------------------------*/

static void on_connected(bool connected)
{
	if (connected) {
		LOG_INF("app: BLE connected");
		alarm_ctrl_set_connected(true);
	} else {
		LOG_INF("app: BLE disconnected — silencing alarm");
		alarm_ctrl_set_connected(false);
		alarm_ctrl_set_active(false, 0xFFU);
	}
}

static const struct ble_core_events g_core_events = {
	.on_connected = on_connected,
};

/* ---------------------------------------------------------------------------
 * Application entry point
 * -------------------------------------------------------------------------*/

int main(void)
{
	int err;
	const struct alarm_ctrl_api *api;
	struct app_api app;
	bool was_connected = false;
	uint8_t last_battery_percent = 0xFFU;

	LOG_INF("Pico W Smart Pill Alarm starting");

	/* Hardware must be quiet before BLE or domain init runs. */
	err = pill_hw_init();
	if (err != 0) {
		LOG_ERR("pill_hw_init failed (%d)", err);
		/* Continue — hardware init failure is non-fatal. */
	}

	/* Initialise domain state; loads persisted alarms/epoch if enabled. */
	alarm_ctrl_init();
	api = alarm_ctrl_get_api();

	/* Build aggregated app API for the service registry.
	 * Each service extracts only its own sub-API (Interface Segregation). */
	app = app_api_build(api);

	/* Initialise BLE core (bt_enable, callbacks, auth). */
	err = ble_core_init(&g_core_events);
	if (err != 0) {
		LOG_ERR("ble_core_init failed (%d)", err);
		return 0;
	}

	/* Start advertising (loads bonds, sets bondable mode). */
	err = ble_core_start_advertising();
	if (err != 0) {
		LOG_ERR("ble_core_start_advertising failed (%d)", err);
		return 0;
	}

	/* Initialise all registered BLE services.
	 * Each service's init() receives the app_api and extracts
	 * only the API pointers it needs. */
	ble_service_init_all(&app);

	/* Phase-2 presenters: display and weekday LEDs (console fallbacks).
	 * These are no-ops when the corresponding CONFIG flags are disabled. */
	(void)pill_display_init();
	(void)pill_leds_init();

	/* ------------------------------------------------------------------
	 * 1-second application tick loop
	 * ------------------------------------------------------------------*/
	while (1) {
		const struct alarm_ctrl_status *status;

		/* Domain tick: check motion, fire due alarms, update battery. */
		api->tick();
		status = api->get_status();

		/* Publish BAS only when connected and only on meaningful changes.
		 * This avoids periodic battery reports while disconnected.
		 */
		if (status->connected) {
			if (!was_connected ||
			    status->battery_percent != last_battery_percent) {
				int rc = bt_bas_set_battery_level(
						status->battery_percent);

				if (rc != 0) {
					LOG_WRN("bt_bas_set_battery_level returned %d", rc);
				} else {
					last_battery_percent = status->battery_percent;
				}
			}
		}
		was_connected = status->connected;

		/* Tick all registered BLE services (CTS notify, pill status, etc.). */
		ble_service_tick_all();

		/* Poll BLE core for maintenance tasks (advertising restart watchdog). */
		ble_core_poll();

		/* Update display presenter and weekday LEDs (Phase-2). */
		pill_display_show_status(status);
		{
			int64_t epoch = api->get_epoch_s();

			if (epoch > 0) {
				uint8_t weekday = (uint8_t)(((epoch / 86400) + 3) % 7);

				pill_leds_show_weekday(weekday);
			}
		}

		k_sleep(K_SECONDS(1));
	}

	return 0;
}
