/* cts_svc.h - CTS (Current Time Service) — service interface
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * This service implements the BLE Current Time Service (CTS).
 * It registers itself with the BLE service framework via the
 * g_cts_service instance.
 *
 * Add/remove by editing the Kconfig flag CONFIG_BT_CTS in prj.conf
 * and the registry array in ble_service.c.
 */

#ifndef BLE_SERVICES_CTS_SVC_H_
#define BLE_SERVICES_CTS_SVC_H_

#include <stdint.h>

#include "ble/ble_service.h"

/* Forward-declared in ble_service.c for the registry array. */
extern struct ble_service g_cts_service;

#endif /* BLE_SERVICES_CTS_SVC_H_ */
