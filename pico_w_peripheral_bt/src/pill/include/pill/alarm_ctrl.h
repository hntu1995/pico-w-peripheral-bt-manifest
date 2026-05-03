/* alarm_ctrl.h - Runtime alarm control state (domain layer, no BLE deps)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PILL_ALARM_CTRL_H_
#define PILL_ALARM_CTRL_H_

#include <stdbool.h>
#include <stdint.h>

#include "pill/alarm_model.h"

/**
 * @brief Runtime status snapshot updated on every alarm_ctrl_tick().
 *
 * This is the single source of truth for application state that BLE
 * services and the main loop read from.
 */
struct alarm_ctrl_status {
    uint8_t active_alarm;       /* 1 if an alarm is currently firing */
    uint8_t battery_percent;    /* last measured battery level */
    uint8_t connected;          /* 1 if a BLE central is connected */
    uint8_t low_battery;        /* 1 if battery_percent <= threshold */
    uint8_t active_alarm_index; /* index of firing alarm, 0xFF if none */
};

/*
 * Alarm controller API interface (dependency-inversion).
 *
 * Modules that need to interact with the alarm controller at runtime
 * may be passed a pointer to this struct by `main` so that the
 * implementation can be swapped or mocked in tests.
 */
struct alarm_ctrl_api {
    void (*set_active)(bool active, uint8_t alarm_index);
    bool (*is_active)(void);
    uint8_t (*get_active_idx)(void);
    struct pill_alarm_table *(*get_table)(void);
    struct pill_kind_table *(*get_kind_table)(void);
    void (*set_time_base)(int64_t epoch_s, int64_t uptime_ms);
    int64_t (*get_epoch_s)(void);
    void (*set_connected)(bool connected);
    const struct alarm_ctrl_status *(*get_status)(void);
    void (*tick)(void);
};

/**
 * @brief Return a pointer to the built-in alarm controller API.
 *
 * The returned pointer is owned by the alarm controller module and
 * remains valid for the lifetime of the application.
 */
const struct alarm_ctrl_api *alarm_ctrl_get_api(void);

/**
 * @brief Initialise alarm control state and load persisted settings.
 *
 * Must be called before any other alarm_ctrl_* function.
 *
 * @return 0 on success, negative errno on settings init failure.
 */
int alarm_ctrl_init(void);

/**
 * @brief Activate or deactivate an alarm, driving hardware output.
 *
 * @param active      true to fire alarm, false to silence it.
 * @param alarm_index Index into the alarm table (use 0xFF when clearing).
 */
void alarm_ctrl_set_active(bool active, uint8_t alarm_index);

/** @return true if an alarm is currently active (buzzer on). */
bool alarm_ctrl_is_active(void);

/** @return Index of the currently active alarm, or 0xFF if none. */
uint8_t alarm_ctrl_get_active_idx(void);

/** @return Pointer to the live alarm table (write through for BLE writes). */
struct pill_alarm_table *alarm_ctrl_get_table(void);

/**
 * @brief Sync the time base from a CTS time-write.
 *
 * Persists the epoch via settings if CONFIG_PILL_SETTINGS is enabled.
 *
 * @param epoch_s   Unix timestamp in seconds.
 * @param uptime_ms k_uptime_get() value at the moment the epoch was valid.
 */
void alarm_ctrl_set_time_base(int64_t epoch_s, int64_t uptime_ms);

/**
 * @return Current best-estimate Unix epoch in seconds, or 0 if no time base.
 */
int64_t alarm_ctrl_get_epoch_s(void);

/**
 * @brief Update the BLE connected flag in the status snapshot.
 *
 * Called by ble_mgr when a central connects or disconnects.
 */
void alarm_ctrl_set_connected(bool connected);

/** @return Pointer to the current read-only status snapshot. */
const struct alarm_ctrl_status *alarm_ctrl_get_status(void);

/**
 * @brief Per-second application tick.
 *
 * Checks motion-clear, detects due alarms, updates battery status.
 * Does NOT make any BLE API calls — callers handle those.
 * Call once per second from the main loop.
 */
void alarm_ctrl_tick(void);

#endif /* PILL_ALARM_CTRL_H_ */
