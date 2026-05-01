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

target_sources(app PRIVATE src/main.c)
