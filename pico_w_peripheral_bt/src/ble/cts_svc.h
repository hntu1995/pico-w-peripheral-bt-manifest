/* cts_svc.h - CTS (Current Time Service) callbacks (public interface)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef BLE_CTS_SVC_H_
#define BLE_CTS_SVC_H_

struct alarm_ctrl_api;

/**
 * @brief Register CTS callbacks and initialise the service.
 *
 * Calls bt_cts_init() with the module-internal callback table.
 * Must be called after bt_enable().
 */
void cts_svc_init(const struct alarm_ctrl_api *api);

/**
 * @brief Per-second tick: send a CTS notification if a central has subscribed.
 *
 * Call from the main loop on each scheduler tick.
 */
void cts_svc_tick(void);

#endif /* BLE_CTS_SVC_H_ */
