# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# Pull in the zephyr-cyw43-driver as an extra Zephyr module.
# This project is used in two workspace layouts:
# 1. Original local workspace: <topdir>/pico_w_peripheral_bt and <topdir>/zephyr-cyw43-driver
# 2. Manifest clone via `west init -l .`: <topdir>/pico-w-peripheral-bt-manifest/pico_w_peripheral_bt
#    and <topdir>/zephyr-cyw43-driver
set(cyw43_module_candidates
  "${CMAKE_CURRENT_SOURCE_DIR}/../zephyr-cyw43-driver"
  "${CMAKE_CURRENT_SOURCE_DIR}/../../zephyr-cyw43-driver"
)

foreach(module_dir IN LISTS cyw43_module_candidates)
  if(EXISTS "${module_dir}/zephyr/module.yml")
    set(ZEPHYR_EXTRA_MODULES "${module_dir}")
    break()
  endif()
endforeach()

if(NOT ZEPHYR_EXTRA_MODULES)
  message(FATAL_ERROR
    "zephyr-cyw43-driver module not found. Expected one of: ${cyw43_module_candidates}"
  )
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(pico_w_peripheral_bt)

target_sources(app PRIVATE
  src/main.c
  src/app_api.c
  src/pill/alarm/alarm_model.c
  src/pill/alarm/alarm_scheduler.c
  src/pill/alarm/alarm_ctrl.c
  src/pill/services/app_settings.c
  src/pill/hw/pill_hw.c
  src/pill/hw/display.c
  src/pill/hw/leds.c
  src/pill/hw/battery_adc.c
  src/pill/services/ble_validation.c
  src/ble/ble_core.c
  src/ble/ble_service.c
  src/ble/services/cts/cts_svc.c
  src/ble/services/ias/ias_svc.c
  src/ble/services/pill/pill_svc.c
)

# Allow source tree and pill include dir to resolve "pill/..." headers
target_include_directories(app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pill/include
)
