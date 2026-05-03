#include "pill/app_settings.h"

#include <errno.h>
#include <string.h>

#include <zephyr/settings/settings.h>
#include <zephyr/sys/printk.h>

#define PILL_SETTINGS_ROOT "pill"
#define PILL_SETTINGS_ALARMS "pill/alarms"
#define PILL_SETTINGS_LAST_EPOCH "pill/last_epoch"
#define PILL_SETTINGS_KINDS "pill/kinds"

static struct pill_alarm_table *g_table;
static int64_t *g_last_epoch_s;
static struct pill_kind_table *g_kind_table;

#if IS_ENABLED(CONFIG_PILL_SETTINGS)
static int pill_settings_set(const char *name, size_t len_rd,
                  settings_read_cb read_cb,
                  void *cb_arg)
{
    if (strcmp(name, "alarms") == 0) {
        if (len_rd != sizeof(*g_table)) {
            return -EINVAL;
        }
        if (read_cb(cb_arg, g_table, sizeof(*g_table)) < 0) {
            return -EIO;
        }
        if (g_table->count > PILL_MAX_ALARMS) {
            g_table->count = PILL_MAX_ALARMS;
        }
        return 0;
    }

    if (strcmp(name, "kinds") == 0) {
        if (len_rd != sizeof(*g_kind_table)) {
            return -EINVAL;
        }
        if (read_cb(cb_arg, g_kind_table, sizeof(*g_kind_table)) < 0) {
            return -EIO;
        }
        if (g_kind_table->count > PILL_MAX_KINDS) {
            g_kind_table->count = PILL_MAX_KINDS;
        }
        return 0;
    }

    if (strcmp(name, "last_epoch") == 0) {
        if (len_rd != sizeof(*g_last_epoch_s)) {
            return -EINVAL;
        }
        if (read_cb(cb_arg, g_last_epoch_s, sizeof(*g_last_epoch_s)) < 0) {
            return -EIO;
        }
        return 0;
    }

    return -ENOENT;
}

SETTINGS_STATIC_HANDLER_DEFINE(pill_settings, PILL_SETTINGS_ROOT,
                   NULL, pill_settings_set, NULL, NULL);
#endif /* CONFIG_PILL_SETTINGS */

int pill_app_settings_init(struct pill_alarm_table *table,
                          struct pill_kind_table *kinds,
                          int64_t *last_epoch_s)
{
    g_table = table;
    g_last_epoch_s = last_epoch_s;
    g_kind_table = kinds;

    if (!IS_ENABLED(CONFIG_PILL_SETTINGS)) {
        return 0;
    }

    return settings_load_subtree(PILL_SETTINGS_ROOT);
}

int pill_app_settings_save_alarms(const struct pill_alarm_table *table)
{
    if (!IS_ENABLED(CONFIG_PILL_SETTINGS)) {
        return -ENOTSUP;
    }

    return settings_save_one(PILL_SETTINGS_ALARMS, table, sizeof(*table));
}

int pill_app_settings_save_kinds(const struct pill_kind_table *table)
{
    if (!IS_ENABLED(CONFIG_PILL_SETTINGS)) {
        return -ENOTSUP;
    }

    return settings_save_one(PILL_SETTINGS_KINDS, table, sizeof(*table));
}

int pill_app_settings_save_last_epoch_s(int64_t epoch_s)
{
    if (!IS_ENABLED(CONFIG_PILL_SETTINGS)) {
        return -ENOTSUP;
    }

    return settings_save_one(PILL_SETTINGS_LAST_EPOCH,
                 &epoch_s, sizeof(epoch_s));
}
