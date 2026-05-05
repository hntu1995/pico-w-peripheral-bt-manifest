/* ble_core.h - Generic BLE stack initialisation and management
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Responsibilities (SRP):
 *   - Call bt_enable() once at startup.
 *   - Own the advertising payload and start/stop advertising.
 *   - Handle BLE connect/disconnect events and notify the app.
 *   - Register auth and security callbacks (pairing, passkey).
 *   - Run fallback poll-based advertising restart watchdog.
 *
 * This module has zero knowledge of pill-specific alarm state.
 * Connection events are forwarded to the app via ble_core_events.
 */

#ifndef BLE_BLE_CORE_H_
#define BLE_BLE_CORE_H_

#include <stdbool.h>
#include <zephyr/types.h>

/**
 * @brief App-provided callbacks for BLE core events.
 *
 * All callbacks run in BT RX thread context — keep them short
 * and non-blocking.
 */
struct ble_core_events {
	/**
	 * @brief Called when a BLE connection is established or lost.
	 *
	 * @param connected  true if connected, false if disconnected.
	 */
	void (*on_connected)(bool connected);
};

/**
 * @brief Core BLE API that registered services receive.
 *
 * Provides basic BLE status queries without coupling services
 * to the full ble_core internals.
 */
struct ble_core_api {
	/** @return true if at least one BLE central is connected. */
	bool (*is_connected)(void);
};

/**
 * @brief Initialise the BLE stack and register core callbacks.
 *
 * Calls bt_enable() internally, then registers connection,
 * security, and auth callbacks. Must be called before any other
 * BLE operation.
 *
 * Potential pitfalls:
 * - Must be called once only; double-init returns -EALREADY.
 * - Blocking: bt_enable() waits for the BT controller to initialise
 *   (several seconds on Pico W with CYW43 driver).
 * - Stack usage: CYW43 driver is stack-hungry during init.
 *
 * @param events  Optional event callbacks (may be NULL).
 * @return 0 on success, negative errno on failure.
 */
int ble_core_init(const struct ble_core_events *events);

/**
 * @brief Load bond settings and start connectable advertising.
 *
 * Registers auth callbacks, sets bondable mode, then starts
 * advertising with the configured AD/SD payload.
 *
 * Potential pitfalls:
 * - Must call after ble_core_init().
 * - Advertising may fail silently if controller is not ready;
 *   poll-based fallback in ble_core_poll() retries.
 * - settings_load() is called here; ensure CONFIG_SETTINGS=y.
 *
 * @return 0 on success, negative errno on failure.
 */
int ble_core_start_advertising(void);

/**
 * @brief Per-second poll for maintenance tasks.
 *
 * Implements a fallback advertising-restart watchdog with
 * exponential backoff. Call once per second from the main loop.
 *
 * Potential pitfalls:
 * - Not thread-safe; call only from main loop context.
 * - Does not block; returns immediately if no work to do.
 */
void ble_core_poll(void);

#endif /* BLE_BLE_CORE_H_ */
