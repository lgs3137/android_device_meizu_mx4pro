/*
 * Copyright (C) 2012 The Android Open Source Project
 * Copyright (C) 2014 The CyanogenMod Project
 * Copyright (C) 2014-2015 Andreas Schneider <asn@cryptomilk.org>
 * Copyright (C) 2014-2015 Christopher N. Hesse <raymanfx@gmail.com>
 * Copyright (C) 2016 Tatsuyuki Ishi <ishitatsuyuki@gmail.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#define LOG_TAG "PowerHAL"
/* #define LOG_NDEBUG 0 */
#include <utils/Log.h>
#include <hardware/hardware.h>
#include <hardware/power.h>
#define WAKE_GESTURE_PATH "/sys/devices/mx_tsp/gesture_hex"
#define BOOSTPULSE_PATH "/sys/devices/system/cpu/cpu0/cpufreq/interactive/boostpulse"
#define IO_IS_BUSY_PATH "/sys/devices/system/cpu/cpu0/cpufreq/interactive/io_is_busy"
static void sysfs_write(char *path, char *s)
{
    char buf[80];
    int len;
    int fd = open(path, O_WRONLY);
    if(fd < 0)
    {
        strerror_r(errno, buf, sizeof(buf));
        ALOGE("Error opening %s: %s", path, buf);
        return;
    }
    len = write(fd, s, strlen(s));
    if(len < 0)
    {
        strerror_r(errno, buf, sizeof(buf));
        ALOGE("Error writing to %s: %s", path, buf);
    }
    close(fd);
}
static void power_init(__attribute__((unused)) struct power_module *module)
{
    ALOGV("%s", __func__);
}
static void power_set_interactive(__attribute__((unused)) struct power_module *module, int on)
{
    ALOGV("%s %s", __func__, (on ? "ON" : "OFF"));
    sysfs_write(IO_IS_BUSY_PATH, on ? "1" : "0");
}
static void power_hint(struct power_module *module, power_hint_t hint, void *data)
{
    switch(hint)
    {
        case POWER_HINT_INTERACTION:
        {
            ALOGV("%s: POWER_HINT_INTERACTION", __func__);
            sysfs_write(BOOSTPULSE_PATH, "1");
            break;
        }
        case POWER_HINT_VSYNC:
        {
            ALOGV("%s: POWER_HINT_VSYNC", __func__);
            break;
        }
        case POWER_HINT_LOW_POWER:
        {
            ALOGV("%s: POWER_HINT_LOW_POWER", __func__);
            break;
        }
        default:
            break;
    }
}
static void set_feature(struct power_module *module, feature_t feature, int state)
{
    switch(feature)
    {
        case POWER_FEATURE_DOUBLE_TAP_TO_WAKE:
            /* This stands for disable_all switch */
            sysfs_write(WAKE_GESTURE_PATH, state ? "00010001" : "00010000");
            /* This is for double tap */
            sysfs_write(WAKE_GESTURE_PATH, state ? "00020001" : "00020000");
            /* Disable other gesture */
            sysfs_write(WAKE_GESTURE_PATH, "00030000");
            sysfs_write(WAKE_GESTURE_PATH, "00040000");
            break;
        default:
            ALOGW("Error setting feature %d, it doesn't exist", feature);
            break;
    }
}
static struct hw_module_methods_t power_module_methods = {
    .open = NULL,
};
struct power_module HAL_MODULE_INFO_SYM = {
    .common =
        {
            .tag = HARDWARE_MODULE_TAG,
            .module_api_version = POWER_MODULE_API_VERSION_0_3,
            .hal_api_version = HARDWARE_HAL_API_VERSION,
            .id = POWER_HARDWARE_MODULE_ID,
            .name = "m76 Power HAL",
            .author = "Tatsuyuki Ishi",
            .methods = &power_module_methods,
        },
    .init = power_init,
    .setInteractive = power_set_interactive,
    .powerHint = power_hint,
    .setFeature = set_feature,
};

