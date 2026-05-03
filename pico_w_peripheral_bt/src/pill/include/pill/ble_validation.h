/* ble_validation.h - BLE write validators for pill alarm table
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PILL_BLE_VALIDATION_H_
#define PILL_BLE_VALIDATION_H_

#include <stdint.h>

/* Validate wire-format alarm table buffer received over BLE.
 * Returns 0 on success or negative errno on failure.
 */
int pill_ble_validate_alarm_table(const uint8_t *buf, uint16_t len);

int pill_ble_validate_kind_table(const uint8_t *buf, uint16_t len);

#endif /* PILL_BLE_VALIDATION_H_ */
