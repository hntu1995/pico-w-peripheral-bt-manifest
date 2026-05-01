# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# Pull in the beechwoods zephyr-cyw43-driver as an extra Zephyr module.
# This provides the infineon,cyw43 and infineon,cyw43-bt-hci drivers needed
# for BT-over-SPI on the Raspberry Pi Pico W (CYW43439 chip).
set(ZEPHYR_EXTRA_MODULES
  "${CMAKE_CURRENT_SOURCE_DIR}/../zephyr-cyw43-driver"
)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(pico_w_peripheral_bt)

target_sources(app PRIVATE src/main.c)
