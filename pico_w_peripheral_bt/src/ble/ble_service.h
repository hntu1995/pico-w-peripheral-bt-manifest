/* ble_service.h - BLE service vtable interface and registry
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Framework for registering BLE GATT services as self-contained
 * modules. Each service defines a struct ble_service instance and
 * is listed in the central registry array in ble_service.c.
 * Adding/removing a service = edit one array + CMakeLists.txt.
 */

#ifndef BLE_BLE_SERVICE_H_
#define BLE_BLE_SERVICE_H_

#include <stddef.h>
#include <zephyr/types.h>

/**
 * @brief Service vtable — each BLE service implements this.
 *
 * All callbacks are optional (may be NULL).
 */
struct ble_service {
	/** Human-readable name (for logging). */
	const char *name;

	/**
	 * @brief Called once during init, after ble_core_init().
	 *
	 * @param app_api  Opaque pointer to app-layer API struct.
	 *                 Each service knows its own sub-API type
	 *                 and casts internally.
	 * @return 0 on success, negative errno on failure.
	 */
	int (*init)(void *app_api);

	/**
	 * @brief Called once per second from the main loop tick.
	 */
	void (*tick)(void);
};

/**
 * @brief Return pointer to the static registry array.
 *
 * Used by ble_service_init_all() / ble_service_tick_all()
 * to iterate all registered services.
 */
const struct ble_service * const *ble_service_get_all(void);

/** @return Number of registered services. */
size_t ble_service_get_count(void);

/**
 * @brief Call init() on every registered service.
 *
 * @param app_api  Opaque pointer forwarded to each service's init().
 */
void ble_service_init_all(void *app_api);

/** @brief Call tick() on every registered service that has one. */
void ble_service_tick_all(void);

#endif /* BLE_BLE_SERVICE_H_ */
