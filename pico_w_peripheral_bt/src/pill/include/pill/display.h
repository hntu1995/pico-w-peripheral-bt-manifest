/* display.h - Simple OLED presenter (Phase-2 scaffold)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PILL_DISPLAY_H_
#define PILL_DISPLAY_H_

#include <stdint.h>
#include "pill/alarm_ctrl.h"

#if defined(CONFIG_PILL_DISPLAY) && (CONFIG_PILL_DISPLAY == 1)
int pill_display_init(void);
void pill_display_show_status(const struct alarm_ctrl_status *status);
#else
static inline int pill_display_init(void) { return 0; }
static inline void pill_display_show_status(const struct alarm_ctrl_status *status) { (void)status; }
#endif

#endif /* PILL_DISPLAY_H_ */
