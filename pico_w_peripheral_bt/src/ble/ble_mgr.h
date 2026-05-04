/* ble_mgr.h - BLE connection management and advertising (public interface)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef BLE_BLE_MGR_H_
#define BLE_BLE_MGR_H_

struct alarm_ctrl_api;

/**
 * @brief Load settings (bond data) and start BLE advertising.
 *
 * Calls settings_load() when CONFIG_SETTINGS is enabled, then starts
 * connectable advertising with the product payload using the identity
 * address (stable scanner-visible address across restarts).
 * The BT_CONN_CB_DEFINE connection callbacks are self-registered at
 * link time — no separate call is required.
 *
 * Must be called after bt_enable().
 *
 * @return 0 on success, negative errno on advertising start failure.
 */
int ble_mgr_start_advertising(const struct alarm_ctrl_api *api);

/* Polling entry called from the main loop to perform maintenance work such
 * as restarting advertising if a disconnect deadline has expired.
 */
void ble_mgr_poll(void);

#endif /* BLE_BLE_MGR_H_ */
