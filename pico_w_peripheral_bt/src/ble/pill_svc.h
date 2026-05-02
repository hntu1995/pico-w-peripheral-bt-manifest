/* pill_svc.h - Custom pill-alarm BLE GATT service (public interface)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef BLE_PILL_SVC_H_
#define BLE_PILL_SVC_H_

#include "pill/alarm_ctrl.h"

#if defined(CONFIG_PILL_BLE_SERVICE) && (CONFIG_PILL_BLE_SERVICE == 1)

/**
 * @brief Initialise the pill service after bt_enable().
 *
 * Locates the status characteristic attribute pointer needed for
 * notifications. Must be called once before pill_svc_notify_status().
 *
 * @return 0 on success.
 */
int pill_svc_init(const struct alarm_ctrl_api *api);

/**
 * @brief Send a status notification if the status has changed since last send.
 *
 * No-op if no central has subscribed.
 *
 * @param status  Pointer to the current alarm controller status snapshot.
 */
void pill_svc_notify_status(const struct alarm_ctrl_status *status);

#else /* !CONFIG_PILL_BLE_SERVICE */

static inline int  pill_svc_init(const struct alarm_ctrl_api *api)        { (void)api; return 0; }
static inline void pill_svc_notify_status(const struct alarm_ctrl_status *s) { (void)s; }

#endif /* CONFIG_PILL_BLE_SERVICE */

#endif /* BLE_PILL_SVC_H_ */
