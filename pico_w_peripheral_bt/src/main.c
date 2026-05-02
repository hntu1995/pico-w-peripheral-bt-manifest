/* main.c - Smart Pill Alarm entry point for Raspberry Pi Pico W
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Responsibilities (SRP):
 *   - Init all subsystems in the correct order.
 *   - Run the 1-second application tick loop.
 *   - Route tick results to BLE service outputs.
 *
 * Everything else lives in its own module:
 *   pill/alarm_ctrl  -- domain state (scheduler, alarm table, hardware)
 *   pill/pill_hw     -- hardware abstraction (buzzer, motion, ADC)
 *   ble/ble_mgr      -- advertising + connection callbacks
 *   ble/cts_svc      -- Current Time Service
 *   ble/ias_svc      -- Immediate Alert Service (self-registers)
 *   ble/pill_svc     -- custom pill GATT service
 */

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/services/bas.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "ble/ble_mgr.h"
#include "ble/cts_svc.h"
#include "ble/pill_svc.h"
#include "ble/ias_svc.h"
#include "pill/alarm_ctrl.h"
#include "pill/pill_hw.h"
#include "pill/display.h"
#include "pill/leds.h"

int main(void)
{
	int err;
	const struct alarm_ctrl_api *api;

	printk("Pico W Smart Pill Alarm starting\n");

	/* Hardware must be quiet before BLE or domain init runs. */
	err = pill_hw_init();
	if (err != 0) {
		printk("pill_hw_init failed (%d)\n", err);
	}

	/* Initialise domain state; loads persisted alarms/epoch if enabled. */
	alarm_ctrl_init();
	api = alarm_ctrl_get_api();

	/* Start BLE stack. */
	err = bt_enable(NULL);
	if (err != 0) {
		printk("bt_enable failed (%d)\n", err);
		return 0;
	}

	/* Load bond settings + start advertising. */
	err = ble_mgr_start_advertising(api);
	if (err != 0) {
		return 0;
	}

	/* Initialise services that require an explicit init call. */
	cts_svc_init(api);
	ias_svc_bind_api(api);
	pill_svc_init(api);

	/* Phase-2 presenters: display and weekday LEDs (console fallbacks).
	 * These are no-ops when the corresponding CONFIG flags are disabled. */
	(void)pill_display_init();
	(void)pill_leds_init();

	/* 1-second application tick loop. */
	while (1) {
		api->tick();

		/* Propagate battery level to BAS (standard Zephyr service). */
		bt_bas_set_battery_level(api->get_status()->battery_percent);

		/* Send CTS notification if a central has subscribed. */
		cts_svc_tick();

		/* Send pill-status notification if state changed. */
		pill_svc_notify_status(api->get_status());

		/* Update display presenter and weekday LEDs (Phase-2). */
		pill_display_show_status(api->get_status());
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
