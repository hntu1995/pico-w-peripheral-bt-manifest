/* pill_svc.h - Custom pill-alarm BLE GATT service — service interface
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * This service implements the proprietary pill-alarm BLE GATT service
 * (alarm table, command write, status notify, kind table).
 * It registers with the BLE service framework via the g_pill_service
 * instance.
 *
 * Add/remove by editing the Kconfig flag CONFIG_PILL_BLE_SERVICE in
 * prj.conf and the registry array in ble_service.c.
 */

#ifndef BLE_SERVICES_PILL_SVC_H_
#define BLE_SERVICES_PILL_SVC_H_

#include "ble/ble_service.h"

/* Forward-declared in ble_service.c for the registry array. */
extern struct ble_service g_pill_service;

#endif /* BLE_SERVICES_PILL_SVC_H_ */
