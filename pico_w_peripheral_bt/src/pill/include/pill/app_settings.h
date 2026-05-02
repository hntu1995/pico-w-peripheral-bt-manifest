/* app_settings.h - Persistent settings API for pill alarms
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PILL_APP_SETTINGS_H_
#define PILL_APP_SETTINGS_H_

#include <stdint.h>

#include "pill/alarm_model.h"

int pill_app_settings_init(struct pill_alarm_table *table, int64_t *last_epoch_s);
int pill_app_settings_save_alarms(const struct pill_alarm_table *table);
int pill_app_settings_save_last_epoch_s(int64_t epoch_s);

#endif
