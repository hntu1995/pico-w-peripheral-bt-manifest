/* ias_svc.h - IAS (Immediate Alert Service) — service interface
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * IAS callbacks self-register at link time via BT_IAS_CB_DEFINE.
 * This module binds to the app API via the ble_service framework.
 *
 * Add/remove by editing the Kconfig flag CONFIG_BT_IAS in prj.conf
 * and the registry array in ble_service.c.
 */

#ifndef BLE_SERVICES_IAS_SVC_H_
#define BLE_SERVICES_IAS_SVC_H_

#include "ble/ble_service.h"

/* Forward-declared in ble_service.c for the registry array. */
extern struct ble_service g_ias_service;

#endif /* BLE_SERVICES_IAS_SVC_H_ */
