/* ble_service.c - BLE service registry
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Central registry of all BLE services in the application.
 * Adding a new service:
 *   1. Create <svc>/svc.h and <svc>/svc.c in services/
 *   2. Add a #include and array entry below
 *   3. Add source file to CMakeLists.txt
 *   4. (Optional) Add Kconfig flag
 *
 * No changes to main.c needed.
 */

#include "ble_service.h"

#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(ble_service);

/* Include each service's header to get its extern instance. */
#include "services/cts/cts_svc.h"
#include "services/ias/ias_svc.h"
#include "services/pill/pill_svc.h"

/* ---------------------------------------------------------------------------
 * Central registry array
 *
 * Add new services here. Each entry should be guarded by its Kconfig
 * flag so the service is only registered when enabled.
 * -------------------------------------------------------------------------*/
static const struct ble_service *const g_services[] = {
#if defined(CONFIG_BT_CTS)
	&g_cts_service,
#endif
#if defined(CONFIG_BT_IAS)
	&g_ias_service,
#endif
#if defined(CONFIG_PILL_BLE_SERVICE)
	&g_pill_service,
#endif
};

const struct ble_service * const *ble_service_get_all(void)
{
	return g_services;
}

size_t ble_service_get_count(void)
{
	return ARRAY_SIZE(g_services);
}

void ble_service_init_all(void *app_api)
{
	size_t i;

	for (i = 0U; i < ARRAY_SIZE(g_services); i++) {
		const struct ble_service *svc = g_services[i];

		if (svc == NULL) {
			continue;
		}
		if (svc->init == NULL) {
			LOG_DBG("service '%s': no init callback", svc->name ? svc->name : "?");
			continue;
		}

		int err = svc->init(app_api);
		if (err != 0) {
			LOG_ERR("service '%s' init failed (%d)", svc->name ? svc->name : "?", err);
		} else {
			LOG_INF("service '%s' initialised", svc->name ? svc->name : "?");
		}
	}
}

void ble_service_tick_all(void)
{
	size_t i;

	for (i = 0U; i < ARRAY_SIZE(g_services); i++) {
		const struct ble_service *svc = g_services[i];

		if (svc == NULL || svc->tick == NULL) {
			continue;
		}

		svc->tick();
	}
}
