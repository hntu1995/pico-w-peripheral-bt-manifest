/* app_api.h - Aggregated per-service API structs
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Following Interface Segregation Principle, each BLE service
 * defines only the API struct it actually needs. This header
 * aggregates them all into a single struct that is built from
 * the alarm_ctrl_api and passed to ble_service_init_all().
 */

#ifndef APP_API_H_
#define APP_API_H_

#include <stdbool.h>
#include <stdint.h>

#include "pill/alarm_ctrl.h"
#include "pill/alarm_model.h"

/* ---------------------------------------------------------------------------
 * CTS service API — subset of alarm_ctrl_api
 * -------------------------------------------------------------------------*/
struct cts_svc_api {
	void (*set_time_base)(int64_t epoch_s, int64_t uptime_ms);
	int64_t (*get_epoch_s)(void);
};

/* ---------------------------------------------------------------------------
 * IAS service API — subset of alarm_ctrl_api
 * -------------------------------------------------------------------------*/
struct ias_svc_api {
	void (*set_active)(bool active, uint8_t alarm_index);
};

/* ---------------------------------------------------------------------------
 * Pill service API — full alarm_ctrl functions needed by pill_svc
 * -------------------------------------------------------------------------*/
struct pill_svc_api {
	void (*set_active)(bool active, uint8_t alarm_index);
	bool (*is_active)(void);
	uint8_t (*get_active_idx)(void);
	struct pill_alarm_table *(*get_table)(void);
	struct pill_kind_table *(*get_kind_table)(void);
	const struct alarm_ctrl_status *(*get_status)(void);
};

/* ---------------------------------------------------------------------------
 * Aggregated app API — built from alarm_ctrl_get_api()
 * -------------------------------------------------------------------------*/
struct app_api {
	struct cts_svc_api  cts;
	struct ias_svc_api  ias;
	struct pill_svc_api pill;
};

/**
 * @brief Build an app_api struct from the alarm controller API.
 *
 * Wraps each alarm_ctrl_api function pointer into the appropriate
 * per-service sub-struct. Safe to call once at startup.
 *
 * @param alarm_api  Pointer to alarm_ctrl_api (must remain valid).
 * @return Populated app_api struct.
 */
struct app_api app_api_build(const struct alarm_ctrl_api *alarm_api);

#endif /* APP_API_H_ */
