/* ias_svc.h - IAS (Immediate Alert Service) callbacks (public interface)
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * IAS callbacks self-register at link time via BT_IAS_CB_DEFINE.
 * No explicit init call is needed from main.c.
 */

#ifndef BLE_IAS_SVC_H_
#define BLE_IAS_SVC_H_

struct alarm_ctrl_api;

/* Bind the alarm controller API for callbacks that need to affect domain state.
 * Call from main() after alarm_ctrl_init() so callbacks can call into the
 * domain without referencing global functions.
 */
void ias_svc_bind_api(const struct alarm_ctrl_api *api);

#endif /* BLE_IAS_SVC_H_ */
