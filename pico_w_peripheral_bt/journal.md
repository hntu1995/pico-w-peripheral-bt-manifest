# Pico W BLE Peripheral — Complete Build Journal

This document records **every step, decision, mistake, and fix** made while building a BLE GATT peripheral application for the Raspberry Pi Pico W from scratch using Zephyr RTOS. The goal was to go from zero to a working `zephyr.uf2` binary ready to flash.

---

## Table of Contents

1. [Project Goal](#1-project-goal)
2. [Environment](#2-environment)
3. [Why a Third-Party Driver Is Needed](#3-why-a-third-party-driver-is-needed)
4. [Setting Up the beechwoods Driver](#4-setting-up-the-beechwoods-driver)
5. [Project Files Created](#5-project-files-created)
6. [Build Attempts and Every Error Fixed](#6-build-attempts-and-every-error-fixed)
7. [Final Build Result](#7-final-build-result)
8. [Flashing](#8-flashing)
9. [BLE Application Behaviour](#9-ble-application-behaviour)
10. [Build Scripts](#10-build-scripts)
11. [Runtime Debugging — GATT Service Discovery Fix](#11-runtime-debugging--gatt-service-discovery-fix)
12. [Summary of Every File Changed](#12-summary-of-every-file-changed)

---

## 1. Project Goal

Build a complete BLE GATT **peripheral** application on the **Raspberry Pi Pico W** (RP2040 + CYW43439) under Zephyr RTOS 4.4.99, based loosely on the upstream `zephyr/samples/bluetooth/peripheral` example.

The application advertises and hosts several standard GATT services:

- **HRS** — Heart Rate Service
- **BAS** — Battery Service
- **IAS** — Immediate Alert Service
- **CTS** — Current Time Service
- **DIS** — Device Information Service
- **SMP** — Security Manager with passkey pairing

Target board string: `rpi_pico/rp2040/w`

---

## 2. Environment

| Component | Version / Path |
|-----------|---------------|
| Zephyr RTOS | 4.4.99 |
| west | 1.5.0 |
| Zephyr SDK | 1.0.1 at `C:/Users/admin/zephyr-sdk-1.0.1` |
| Python venv | `c:\Users\admin\zephyrproject\.venv` |
| West workspace root | `c:\Users\admin\zephyrproject` |
| CMake | 4.3.2 |
| Ninja | winget package |
| dtc | winget package (`oss-winget.dtc`) |
| Host OS | Windows 11 |

The Python virtual environment must be activated before running `west`:

```powershell
cd c:\Users\admin\zephyrproject
.\.venv\Scripts\Activate.ps1
```

---

## 3. Why a Third-Party Driver Is Needed

The Raspberry Pi Pico W connects the RP2040 to the CYW43439 chip using a **PIO-SPI** interface (GPIO 24 = data/IRQ, GPIO 25 = CS, GPIO 29 = CLK, GPIO 23 = WL_ON). This is a fully custom, half-duplex SPI bus driven by the RP2040's PIO block.

**Zephyr mainline (4.4) has two CYW43439 drivers:**

1. **`infineon,airoc-wifi`** — the official Infineon AIROC WiFi driver (`CONFIG_WIFI_AIROC`). This supports CYW43439 WiFi over PIO-SPI. It does **not** expose BT; there is no BT-HCI implementation.
2. **`CONFIG_CYW43439`** — in `modules/hal/rpi_pico`, provides BT via **UART** (`CONFIG_BT_H4`). There is no UART connection between the RP2040 and CYW43439 on the Pico W, so this is completely unusable.

**Conclusion:** Neither mainline driver can provide BLE on the Pico W. The **beechwoods-software/zephyr-cyw43-driver** implements exactly what is needed: it wraps the Pico SDK's `pico_cyw43_driver` and `cybt_shared_bus` layer and exposes an `infineon,cyw43-bt-hci` node to Zephyr's BT stack over PIO-SPI.

---

## 4. Setting Up the beechwoods Driver

Clone the driver **with `--recursive`** to pull in the Pico SDK submodules it depends on:

```powershell
cd c:\Users\admin\zephyrproject
git clone --recursive https://github.com/beechwoods-software/zephyr-cyw43-driver.git
```

This creates `c:\Users\admin\zephyrproject\zephyr-cyw43-driver\`.

The driver is integrated by pointing `ZEPHYR_EXTRA_MODULES` at it in `CMakeLists.txt`. West and Zephyr's build system then scan it for CMake/Kconfig/binding files automatically.

---

## 5. Project Files Created

All project files live in `c:\Users\admin\zephyrproject\pico_w_peripheral_bt\`.

### 5.1 CMakeLists.txt

```cmake
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# Pull in the beechwoods zephyr-cyw43-driver as an extra Zephyr module.
# This must be set BEFORE find_package(Zephyr ...).
set(ZEPHYR_EXTRA_MODULES
  "${CMAKE_CURRENT_SOURCE_DIR}/../zephyr-cyw43-driver"
)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(pico_w_peripheral_bt)

target_sources(app PRIVATE src/main.c)
```

The critical line is `ZEPHYR_EXTRA_MODULES`. Zephyr's CMake machinery discovers all binding YAML files, Kconfig files, and CMakeLists in the extra module path automatically.

### 5.2 prj.conf

```kconfig
# Stack / heap sizes (the CYW43 driver is stack-hungry)
CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE=4096
CONFIG_MAIN_STACK_SIZE=4096
CONFIG_HEAP_MEM_POOL_SIZE=16384

# CYW43 WiFi/BT driver (beechwoods – BT-over-SPI support)
CONFIG_WIFI=y
CONFIG_WIFI_ZEPHYR_CYW43=y

# Disable the Infineon AIROC WiFi and pico-sdk UART-HCI BT drivers
CONFIG_WIFI_AIROC=n
CONFIG_CYW43439=n

# Entropy / RNG required by the CYW43 driver
CONFIG_ENTROPY_GENERATOR=y
CONFIG_TEST_RANDOM_GENERATOR=y

# Minimal networking stack (selected internally by WIFI_ZEPHYR_CYW43)
CONFIG_NETWORKING=y
CONFIG_NET_L2_ETHERNET=y
CONFIG_NET_IPV4=y
CONFIG_NET_IPV6=n
CONFIG_NET_UDP=y
CONFIG_NET_TCP=y
CONFIG_NET_MGMT=y
CONFIG_NET_MGMT_EVENT=y
CONFIG_NET_CONFIG_SETTINGS=y
CONFIG_NET_CONFIG_AUTO_INIT=n
CONFIG_NET_CONFIG_INIT_TIMEOUT=0

# Bluetooth: peripheral role
CONFIG_BT=y
CONFIG_LOG=y
CONFIG_BT_SMP=y
CONFIG_BT_PERIPHERAL=y

# GATT services
CONFIG_BT_DIS=y
CONFIG_BT_BAS=y
CONFIG_BT_HRS=y
CONFIG_BT_IAS=y
CONFIG_BT_CTS=y
CONFIG_BT_CTS_HELPER_API=y

# BT tuning
CONFIG_BT_ATT_PREPARE_COUNT=5
CONFIG_BT_PRIVACY=y
CONFIG_BT_DEVICE_NAME="Pico W Peripheral"
CONFIG_BT_DEVICE_APPEARANCE=833
CONFIG_BT_DEVICE_NAME_DYNAMIC=y
CONFIG_BT_DEVICE_NAME_MAX=65
CONFIG_BT_MAX_CONN=4
CONFIG_BT_ID_MAX=2

# Persistent BT settings (bond storage over NVS)
CONFIG_BT_SETTINGS=y
CONFIG_FLASH=y
CONFIG_FLASH_MAP=y
CONFIG_NVS=y
CONFIG_SETTINGS=y
```

Key design choices:

- `CONFIG_WIFI_AIROC=n` and `CONFIG_CYW43439=n` explicitly disable the two conflicting mainline drivers.
- `CONFIG_WIFI_ZEPHYR_CYW43=y` is the beechwoods Kconfig symbol.
- `CONFIG_BT_SETTINGS=y` requires a `storage_partition` in flash (see Error 6 below).
- `CONFIG_NET_CONFIG_AUTO_INIT=n` and `CONFIG_NET_CONFIG_INIT_TIMEOUT=0` prevent the networking layer from blocking at boot waiting for a network interface that we never intend to bring up.

### 5.3 Kconfig

```kconfig
mainmenu "Pico W BLE Peripheral"
source "Kconfig.zephyr"
```

Minimal application Kconfig — required by the build system.

### 5.4 boards/rpi_pico_rp2040_w.overlay

This file went through many revisions as errors were fixed. The final version is documented below in Section 6 and listed in full in Section 11.

### 5.5 src/main.c

A BLE GATT peripheral application that:
- Starts the BT subsystem with `bt_enable()`
- Registers all GATT services (HRS, BAS, IAS, CTS, DIS)
- Sets up SMP with passkey authentication
- Starts advertising
- Simulates heart rate and battery level data in a periodic work handler

---

## 6. Build Attempts and Every Error Fixed

The build command used throughout:

```powershell
cd c:\Users\admin\zephyrproject
.\.venv\Scripts\Activate.ps1
west build -p always -b rpi_pico/rp2040/w pico_w_peripheral_bt
```

The `-p always` flag forces a pristine (clean) build each time.

---

### Error 1 — Duplicate DTS binding file

**Stage:** CMake configuration (DTS processing)

**Error message:**
```
devicetree error: both
  c:/Users/admin/zephyrproject/zephyr-cyw43-driver/dts/bindings/gpio/infineon,cyw43-gpio.yaml
and
  c:/Users/admin/zephyrproject/zephyr/dts/bindings/gpio/infineon,cyw43-gpio.yaml
have 'compatible: infineon,cyw43-gpio'
```

**Root cause:**
Zephyr 4.4 already includes the `infineon,cyw43-gpio.yaml` binding in the mainline tree. The beechwoods driver also ships the same file in its own `dts/bindings/gpio/` directory (it was written targeting an older Zephyr version). Zephyr's DTS processing refuses to have two files claiming the same `compatible` string.

**Fix:**
Delete the duplicate from the beechwoods driver repository:

```powershell
Remove-Item "c:\Users\admin\zephyrproject\zephyr-cyw43-driver\dts\bindings\gpio\infineon,cyw43-gpio.yaml"
```

---

### Error 2 — AIROC-specific DTS properties rejected

**Stage:** CMake configuration (DTS processing)

**Error message:**
```
devicetree error: 'spi-half-duplex' appears in
  .../pico_w_peripheral_bt/boards/rpi_pico_rp2040_w.overlay
but is not defined by 'infineon,cyw43'
```

**Root cause:**
The base board DTS (`rpi_pico_rp2040_w.dts`) defines an `airoc-wifi@0` node with `compatible = "infineon,airoc-wifi"` and AIROC-specific properties:

```dts
airoc-wifi@0 {
    compatible = "infineon,airoc-wifi";
    wifi-reg-on-gpios = <&gpio0 23 GPIO_ACTIVE_HIGH>;
    bus-select-gpios  = <&gpio0 24 GPIO_ACTIVE_HIGH>;
    wifi-host-wake-gpios = <&gpio0 24 GPIO_ACTIVE_HIGH>;
    spi-max-frequency = <10000000>;
    spi-half-duplex;
    spi-data-irq-shared;
    ...
};
```

The initial overlay approach was to **re-bind** this node by overriding `compatible` to `"infineon,cyw43"` and deleting AIROC-specific properties with `/delete-property/`. This caused DTS validation failures because the binding validator sees the AIROC properties while still on the node.

**Fix (final approach):**
Disable the old node entirely and create a completely new node:

```dts
/* Disable the stock AIROC node */
&pio0_spi0 {
    airoc-wifi@0 {
        status = "disabled";
    };
};

/* Create a fresh infineon,cyw43 node nested inside &pio0 */
&pio0 {
    pio0_spi0: pio0_spi0 {
        infineon_cyw43_module: infineon_cyw43_module@0 {
            compatible = "infineon,cyw43";
            reg = <0x0>;
            ...
        };
    };
};
```

The second `&pio0 { pio0_spi0: pio0_spi0 { ... } }` pattern augments the existing `pio0_spi0` node and adds the new child to it.

---

### Error 3 — Missing `infineon_cyw43_module` DT label

**Stage:** Compilation, step ~171/392

**Error message (abbreviated):**
```
'DT_N_NODELABEL_infineon_cyw43_module_P_host_wake_gpios_IDX_0_PH_ORD' undeclared
```

**Root cause:**
The beechwoods driver file `cyw43_bus_pio_spi.c`, line 89:

```c
.host_wake_gpio = GPIO_DT_SPEC_GET(DT_NODELABEL(infineon_cyw43_module), host_wake_gpios),
```

This is a compile-time lookup by **node label**. The label `infineon_cyw43_module` must exist in the device tree. The overlay at this stage named the new node but did not assign it a label.

**Fix:**
Add the `infineon_cyw43_module:` label to the new node in the overlay:

```dts
infineon_cyw43_module: infineon_cyw43_module@0 {   /* label added */
    compatible = "infineon,cyw43";
    ...
};
```

---

### Error 4 — Missing `cyw43_led` child DT node

**Stage:** Compilation, step ~172/392

**Error message:**
```
'DT_N_INST_0_infineon_cyw43_led_FULL_NAME' undeclared here;
did you mean 'DT_N_INST_0_infineon_cyw43_bt_hci'?
```

**Root cause:**
The beechwoods driver's `zephyr_cyw43_drv.c` uses `DT_INST(0, infineon_cyw43_led)` to find the LED device node. The CYW43439 has a GPIO pin (GPIO 0) that drives the on-board LED on the Pico W. The driver manages it as an `infineon,cyw43-led` compatible child node. Without this child, the macro expands to an undeclared symbol.

The reference overlay in the beechwoods repo (`app/boards/rpi_pico_w.overlay`) always includes:
```dts
cyw43_led: cyw43_led {
    compatible = "infineon,cyw43-led";
};
```

**Fix:**
Add the child node inside `infineon_cyw43_module@0`:

```dts
cyw43_led: cyw43_led {
    compatible = "infineon,cyw43-led";
};
```

---

### Error 5 — `cyw43_gpio` label collision

**Stage:** CMake configuration (DTS processing)

**Error message:**
```
devicetree error: Label 'cyw43_gpio' defined in multiple places
```

**Root cause:**
The base board DTS already has `cyw43_gpio` as a label on a child node inside `airoc-wifi@0`:

```dts
airoc-wifi@0 {
    cyw43_gpio: gpio {   /* ← claims label 'cyw43_gpio' */
        compatible = "infineon,cyw43-gpio";
        gpio-controller;
        #gpio-cells = <2>;
        ngpios = <3>;
    };
};
```

Even though `airoc-wifi@0` is now `status = "disabled"`, the node and its children still exist in the DTS at compile time. The label `cyw43_gpio` is still a compile-time DTS symbol. The overlay's new `cyw43_gpio: cyw43_gpio { ... }` node inside `infineon_cyw43_module@0` then creates a second claim on the same label, which DTS compilation rejects.

**Fix:**
Use `/delete-node/` to remove the old GPIO node before creating the new one:

```dts
/delete-node/ &cyw43_gpio;

---

## 13. Phase‑2: Display, LEDs, Battery ADC

- Implemented a basic SSD1306 presenter (`src/pill/display.c`) that:
    - Uses the devicetree `oled_display` alias when present.
    - Maintains a page-oriented framebuffer and a tiny 5x7 font.
    - Writes the framebuffer via Zephyr's `display_write()` API.
    - Falls back to `LOG_INF()` if no display is present.

- Implemented weekday LED control (`src/pill/leds.c`) that:
    - Reads `weekday-led0..6` aliases from the overlay.
    - Configures GPIOs and drives the appropriate LED for a weekday.
    - Logs missing aliases instead of failing.

- Implemented ADC sampling and Li‑ion mapping (`src/pill/battery_adc.c`):
    - Uses Zephyr ADC API to sample channel 0 (GPIO26 by convention).
    - Converts raw ADC codes to millivolts, compensates for a 2:1 divider,
        and maps to percentage with a small piecewise-linear curve.
    - Provides conservative fallbacks and logs errors when ADC is
        unavailable.

Pitfalls and notes:
- The SSD1306 presenter uses ~1KB static framebuffer memory; ensure
    resource budgets are acceptable for your build configuration.
- The ADC channel, VREF and divider are configurable via macros in the
    source if your board wiring differs from the defaults.
```

This directive is placed in the overlay before the `&pio0 { ... }` block.

## 18. Build script SDK normalization, rebuild and flash — 2026-05-03

- **Date:** 2026-05-03
- **Files modified:** `pico_w_peripheral_bt/build.ps1`
- **Summary:** Normalized and hardened SDK detection in the build wrapper, passed the normalized SDK path into CMake, verified a successful build, and flashed the resulting UF2 to a Pico W.

- **What I changed:**
    - Added a `Test-SdkRoot()` helper in `build.ps1` that requires the SDK root to contain `cmake/zephyr/gnu/generic.cmake` and at least one toolchain directory under `gnu` (matching `-zephyr-`).
    - If `ZEPHYR_SDK_INSTALL_DIR` points to a nested path (for example `...\zephyr-sdk-1.0.1\zephyr-sdk-1.0.1`), the script now attempts to normalize to its parent directory and prints the normalized path.
    - The script now passes `-DZEPHYR_SDK_INSTALL_DIR=<normalized-path>` through the `west build` arguments so CMake uses the intended SDK root.

- **Verification performed:**
    - Tested with `ZEPHYR_SDK_INSTALL_DIR` set to `C:\Users\admin\zephyr-sdk-1.0.1\zephyr-sdk-1.0.1`; the script normalized it to `C:\Users\admin\zephyr-sdk-1.0.1` and proceeded to configure and build.
    - Incremental build (`-NoPristine`) produced `build/zephyr/zephyr.uf2`.
    - Copied `build/zephyr/zephyr.uf2` to the attached Pico W (RPI-RP2 volume, detected via WMI). The file was copied to `E:\zephyr.uf2` on this machine and the board rebooted.

- **Observations / notes:**
    - The build logs contained many warnings (mostly from the third-party `zephyr-cyw43-driver` and Pico HAL), but the link step succeeded and the UF2 was generated.
    - Passing the explicit `-DZEPHYR_SDK_INSTALL_DIR` flag avoids confusing CMake when nested or duplicated SDK folders exist on Windows.

- **Next recommended steps:**
    - Open the USB CDC console (115200) to confirm runtime logs, BLE advertising, and GATT discovery. I can open the serial console now if you want.
    - Optionally, add a short SDK sanity-check in CI or document `ZEPHYR_SDK_INSTALL_DIR` usage in the README to avoid the nested-install issue for other developers.


---

### Error 6 — Missing `storage_partition` DT label

**Stage:** Compilation, ~step 175/392

**Error message:**
```
'DT_N_NODELABEL_storage_partition_PARTITION_ID' undeclared
```

**Root cause:**
`CONFIG_BT_SETTINGS=y` causes the Bluetooth stack to persist bonding information to flash. It uses the Zephyr `SETTINGS` subsystem with the `NVS` backend. NVS locates its flash area via `DT_NODELABEL(storage_partition)`, which must resolve to a real flash partition node.

The Raspberry Pi Pico base DTS (`rpi_pico-common.dtsi`) only defines two partitions:

| Partition | Start | Size |
|-----------|-------|------|
| `second_stage_bootloader` | `0x000000` | 256 B |
| `code_partition` | `0x000100` | rest of 2 MB |

There is no `storage_partition`.

**Fix:**
Add a flash partition layout in the overlay. The Pico W has exactly 2 MB flash. The layout chosen:

| Partition | Start | Size | Notes |
|-----------|-------|------|-------|
| `second_stage_bootloader` | `0x000000` | 256 B | Unchanged |
| `code_partition` | `0x000100` | 1.75 MB (`0x1BFF00`) | Shrunk |
| `storage_partition` | `0x1C0000` | 256 KB | **New** |

```dts
/* Shrink code_partition to leave room for storage */
&code_partition {
    reg = <0x100 0x1BFF00>;
};

/* Add the NVS storage partition at end of flash */
&flash0 {
    partitions {
        storage_partition: partition@1c0000 {
            label = "storage";
            reg = <0x1c0000 DT_SIZE_K(256)>;
        };
    };
};
```

256 KB was chosen because NVS requires at least 2 sectors (4 KB each for RP2040 flash), and 256 KB provides comfortable headroom for bond storage.

---

### Error 7 — `panic()` undeclared in `cybt_shared_bus.c`

**Stage:** Compilation, step 172/392

**Error message:**
```
modules/hal/rpi_pico/src/rp2_common/pico_cyw43_driver/cybt_shared_bus/cybt_shared_bus.c:269:
error: implicit declaration of function 'panic'; did you mean 'k_panic'?
[-Werror=implicit-function-declaration]
```

**Root cause:**
`cybt_shared_bus.c` is part of the Pico SDK's BT integration layer in `hal/rpi_pico`. It calls `panic()` at line 269 — this is the Pico SDK's own panic function declared in `<pico/platform/panic.h>`. However, the file's includes are:

```c
#include "cyw43_config.h"
#include "cyw43.h"
#include "cyw43_ll.h"
/* pico/platform/panic.h is NOT included */
```

In Zephyr's build environment, the Pico SDK header chain is not automatically available, so `panic()` has no declaration.

**First attempted fix (WRONG — do not use):**

Added a macro to `cyw43_configport.h`:
```c
#define panic(fmt, ...) k_panic()
```

This caused a **new, worse error.** The Pico SDK's own `<pico/platform/panic.h>` declares:
```c
void __attribute__((noreturn)) panic(const char *fmt, ...);
```

When the compiler later encounters this declaration (included transitively via `hardware/gpio.h` → `pico.h` → `pico/platform/panic.h`), the preprocessor macro is already in scope. The preprocessor expands `panic` inside the declaration:

```c
/* After macro expansion: */
void __attribute__((noreturn)) k_panic()(const char *fmt, ...);
```

This is syntactically broken C and caused a cascade of parse errors. **Lesson:** a function-like `#define` replacing a name will also expand inside function declarations — this is a classic C preprocessor pitfall. The `#define` approach must be avoided when the symbol appears in any header's declaration.

The macro was removed from `cyw43_configport.h`.

**Correct fix:**
Include the header that declares `panic()` directly in the source file:

File: `modules/hal/rpi_pico/src/rp2_common/pico_cyw43_driver/cybt_shared_bus/cybt_shared_bus.c`

```diff
 #include "cyw43_ll.h"
+#include <pico/platform/panic.h>
```

---

### Error 8 — Same `panic()` error in `cybt_shared_bus_driver.c`

**Stage:** Compilation, step 173/392

**Error message:**
```
modules/hal/rpi_pico/src/rp2_common/pico_cyw43_driver/cybt_shared_bus/cybt_shared_bus_driver.c:398:
error: implicit declaration of function 'panic'; did you mean 'k_panic'?
```

**Root cause:**
The companion file `cybt_shared_bus_driver.c` (same directory) has the identical missing include. Both files are part of the same Pico SDK BT bus layer; both call `panic()` without the header.

**Fix:**
Same as Error 7:

File: `modules/hal/rpi_pico/src/rp2_common/pico_cyw43_driver/cybt_shared_bus/cybt_shared_bus_driver.c`

```diff
 #include "cyw43_ll.h"
+#include <pico/platform/panic.h>
```

---

## 7. Final Build Result

After all eight errors were resolved, the build succeeded:

```
[392/392] Linking ASM executable zephyr/zephyr.elf

Memory region         Used Size  Region Size  %age Used
           FLASH:      531724 B     2344960 B     22.68%
             RAM:       75664 B     262144 B     28.86%

BUILD_EXIT:0
west build: build successful
```

Output artifacts in `build/zephyr/`:
- `zephyr.uf2` — UF2 binary for drag-and-drop flashing (~22% of 2 MB flash)
- `zephyr.elf` — ELF with full debug symbols

---

## 8. Flashing

1. Hold the **BOOTSEL** button on the Pico W.
2. While holding BOOTSEL, connect the Pico W to USB.
3. Release BOOTSEL. A USB mass storage device named **RPI-RP2** appears in Windows Explorer.
4. Copy (drag-and-drop or `Copy-Item`) `build\zephyr\zephyr.uf2` onto the RPI-RP2 drive.
5. The drive disappears and the board reboots, running the new firmware.

```powershell
# Example (adjust the drive letter to whatever RPI-RP2 is assigned)
Copy-Item "c:\Users\admin\zephyrproject\build\zephyr\zephyr.uf2" "D:\"
```

---

## 9. BLE Application Behaviour

After flashing and powering the board:

- The device advertises as **"Pico W Peripheral"**.
- BLE scanning tools (nRF Connect for Mobile, LightBlue, etc.) will discover it.
- Connecting requires **passkey pairing** — SMP is enabled with passkey confirmation.
- GATT services available after pairing:

| Service | Description |
|---------|-------------|
| Heart Rate (HRS) | Simulated BPM cycling 60–100 |
| Battery (BAS) | Simulated battery level percentage |
| Current Time (CTS) | Readable and writable |
| Immediate Alert (IAS) | Writable alert level |
| Device Information (DIS) | Manufacturer/model/firmware strings |

Bonds are stored in NVS flash (`storage_partition`) and survive power cycles.

## 19. BLE re-advertising identity address stabilization — 2026-05-03

- **Date:** 2026-05-03
- **Files modified:** `pico_w_peripheral_bt/src/ble/ble_mgr.c`, `pico_w_peripheral_bt/src/ble/ble_mgr.h`
- **Summary:** Forced connectable advertising to use the BLE identity address so scanner-visible address stays stable across disconnect/re-advertise cycles, while preserving the existing delayed auto-restart flow.

- **What changed:**
    - Added `PILL_ADV_PARAM_IDENTITY_FAST_1` in `ble_mgr.c` using:
        - `BT_LE_ADV_OPT_CONN`
        - `BT_LE_ADV_OPT_USE_IDENTITY`
        - fast interval window 1 (`BT_GAP_ADV_FAST_INT_MIN_1..MAX_1`)
    - Replaced both `bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ...)` call sites with `bt_le_adv_start(PILL_ADV_PARAM_IDENTITY_FAST_1, ...)`:
        - initial advertising start (`ble_mgr_start_advertising()`)
        - poll-driven re-advertise after disconnect (`ble_mgr_poll()`)
    - Updated settings-load gate in `ble_mgr_start_advertising()` from `CONFIG_PILL_SETTINGS` to `CONFIG_SETTINGS` so identity/bond settings are loaded whenever Zephyr settings are enabled.
    - Updated the `ble_mgr.h` API comment to document identity-address advertising behavior.

- **Why:**
    - With `CONFIG_BT_PRIVACY=y`, centrals may observe rotating random addresses (RPA), which can look like a different peripheral after re-advertising.
    - Using `BT_LE_ADV_OPT_USE_IDENTITY` keeps the advertised address stable for this app's reconnect UX and troubleshooting.

- **Validation:**
        - Built successfully with `.\pico_w_peripheral_bt\build.ps1 -NoPristine`.
    - Output artifact: `build/zephyr/zephyr.uf2`.

- **Pitfalls / notes:**
    - This change intentionally reduces privacy at the advertising address level by preferring the identity address for connectable advertising.
    - Disconnect reason `0x13` remains central-initiated (`Remote User Terminated`) and is independent of this address stabilization change.

## 20. Pairing diagnostics and explicit security request — 2026-05-03

- **Date:** 2026-05-03
- **Files modified:** `pico_w_peripheral_bt/src/ble/ble_mgr.c`
- **Summary:** Added explicit LE security elevation on connect and detailed pairing/security callbacks to diagnose nRF Connect pairing failures.

- **What changed:**
    - Added `bt_conn_set_security(conn, BT_SECURITY_L2)` in `connected_cb()` and logged return codes.
    - Registered `bt_conn_auth_info_cb` once at startup path to log:
        - `pairing_complete(peer, bonded)`
        - `pairing_failed(peer, reason)`
    - Added `security_changed` connection callback and logs:
        - peer address
        - resulting security level
        - security error code/string
    - Explicitly set peripheral bondability via `bt_set_bondable(true)` before advertising.

- **Why:**
    - nRF Connect reported pairing failure with limited diagnostics.
    - Requesting security immediately after connection makes failure points deterministic and visible in logs.

- **Validation:**
    - Built successfully with `.\pico_w_peripheral_bt\build.ps1 -NoPristine`.
    - Output artifact: `build/zephyr/zephyr.uf2`.

- **Pitfalls / notes:**
    - Pairing is still client-driven in many flows; logs are required to distinguish UI cancellation from SMP/protocol failure.
    - If old/bad bonds exist on either side, delete bond on phone and clear peripheral bond keys before retesting.

## 21. Auto-confirm incoming pairing for Just Works flow — 2026-05-04

- **Date:** 2026-05-04
- **Files modified:** `pico_w_peripheral_bt/src/ble/ble_mgr.c`
- **Summary:** Registered `bt_conn_auth_cb` handlers so incoming pairing is explicitly accepted and logged, fixing the prior gap where security was requested but no application auth callback confirmed pairing.

- **What changed:**
    - Added `bt_conn_auth_cb_register()` with callbacks for:
        - `pairing_confirm` → calls `bt_conn_auth_pairing_confirm()`
        - `passkey_display` → logs passkey
        - `passkey_entry` → logs unsupported local keypad flow and cancels
        - `cancel` → logs pairing cancellation
    - Registered auth callbacks once during BLE startup alongside auth-info callbacks.

- **Why:**
    - Runtime logs showed `security changed ... level=1 err=4` and `pairing failed ... reason=4`.
    - In Zephyr, `4` maps to `BT_SECURITY_ERR_AUTH_REQUIREMENT`, which matches an unfulfilled pairing confirmation/authentication path.

- **Validation:**
    - Built successfully with `.\pico_w_peripheral_bt\build.ps1 -NoPristine`.
    - Output artifact: `build/zephyr/zephyr.uf2`.

- **Pitfalls / notes:**
    - Auto-confirming Just Works improves interoperability but is weaker than a real user-confirmed UI path.
    - If old bonds exist on the phone, delete them before retesting so stale state does not mask the new behavior.

## 22. Auto-confirm Numeric Comparison passkey — 2026-05-04

- **Date:** 2026-05-04
- **Files modified:** `pico_w_peripheral_bt/src/ble/ble_mgr.c`
- **Summary:** Added a `passkey_confirm` auth callback that auto-accepts numeric comparison passkeys to avoid local confirmation blocking and pairing failure loops.

- **What changed:**
    - Implemented `auth_passkey_confirm_cb(struct bt_conn *conn, unsigned int passkey)`.
    - Callback logs peer + passkey and calls `bt_conn_auth_passkey_confirm(conn)`.
    - Registered the callback in `auth_cb` via `.passkey_confirm = auth_passkey_confirm_cb`.

- **Why:**
    - User flow reported passkey confirmation prompts that should be automated.
    - Without handling this callback, pairing can remain at security level 1 and fail with `BT_SECURITY_ERR_AUTH_REQUIREMENT` in mixed IO-capability cases.

- **Validation:**
    - Built successfully with `.\pico_w_peripheral_bt\build.ps1 -NoPristine`.
    - Output artifact: `build/zephyr/zephyr.uf2`.

- **Pitfalls / notes:**
    - This intentionally trades user-verified passkey comparison for convenience; review security requirements before production release.

## 23. BAS update policy: connected-only, change-driven — 2026-05-04

- **Date:** 2026-05-04
- **Files modified:** `pico_w_peripheral_bt/src/main.c`
- **Summary:** Stopped periodic BAS battery updates while disconnected; battery is now published only when connected and when value/connection state changes.

- **What changed:**
    - Added connection/value tracking in main loop:
        - `was_connected`
        - `last_battery_percent`
    - `bt_bas_set_battery_level()` now runs only if:
        - current status is connected, and
        - either this is a fresh connection or battery percentage changed
    - Reused one `status` snapshot per tick for BAS/pill_svc/display calls.

- **Why:**
    - Requested behavior: do not report battery every second unless connected.
    - Reduces unnecessary traffic and avoids notification churn when no central is connected.

## 24. Alarm sync payload diagnostics before validation — 2026-05-04

- **Date:** 2026-05-04
- **Files modified:** `pico_w_peripheral_bt/src/ble/pill_svc.c`
- **Summary:** Added pre-validation logging for incoming alarm-table writes so every received entry is printed even when validation guards reject the payload.

- **What changed:**
    - Added `log_alarm_table_payload()` to decode and print each incoming wire entry.
    - `write_alarm_table()` now logs payload version/count/len and each `rx[i]` entry before validation.
    - Added explicit warning logs when:
        - version unsupported
        - validator rejects payload
        - decode fails

- **Why:**
    - Runtime logs previously showed only `write_alarm_table ... len=...`, making it unclear which guard blocked commit/print paths.
    - New logs reveal whether the payload shape/values or later decode path is failing.

- **Validation:**
    - Built successfully with `.\pico_w_peripheral_bt\build.ps1 -NoPristine`.
    - Output artifact: `build/zephyr/zephyr.uf2`.

## 25. BLE weekday-mask compatibility for alarm sync — 2026-05-04

- **Date:** 2026-05-04
- **Files modified:** `pico_w_peripheral_bt/src/pill/services/ble_validation.c`, `pico_w_peripheral_bt/src/ble/pill_svc.c`
- **Summary:** Normalized incoming `weekday_mask=0x00` from BLE clients to `PILL_WEEKDAY_ALL (0x7F)` so alarm-table sync payloads are accepted instead of rejected with `-EINVAL`.

- **What changed:**
    - In `ble_validation.c` (`pill_ble_validate_alarm_table`):
        - if decoded `weekday_mask == 0`, rewrite to `PILL_WEEKDAY_ALL` before `pill_alarm_validate()`.
    - In `pill_svc.c` (`decode_table`):
        - applied the same normalization before `pill_alarm_table_set()`.
        - added warning log identifying entry index that was normalized.

- **Why:**
    - Observed payload had valid wire shape but entries with `weekday_mask=0x00`.
    - Core model rejects zero weekday masks, causing guard failure (`write_alarm_table rejected by validator (-22)`).
    - Compatibility normalization keeps strict model semantics while accepting real client payloads.

- **Validation:**
    - Built successfully with `.\pico_w_peripheral_bt\build.ps1 -NoPristine`.
    - Output artifact: `build/zephyr/zephyr.uf2`.

## 26. Alarm log enhancement: resolve pill kind names — 2026-05-04

- **Date:** 2026-05-04
- **Files modified:** `pico_w_peripheral_bt/src/ble/pill_svc.c`
- **Summary:** Enhanced alarm-table logs to include human-readable pill kind names (from kind table) in addition to the raw kind bitmask.

- **What changed:**
    - Added `format_kind_mask()` helper to resolve kind bitmask bits to names.
    - `log_alarm_table()` now prints:
        - comma-separated kind names (fallback `kindN` when name missing)
        - raw hex mask for debugging

- **Why:**
    - Requested better observability of synced alarm entries without manually decoding bitmasks.

- **Validation:**
    - Built successfully with `.\pico_w_peripheral_bt\build.ps1 -NoPristine`.
    - Output artifact: `build/zephyr/zephyr.uf2`.

---

## 10. Build Scripts

Two PowerShell scripts are now provided:

### 10.1 setup-env.ps1

File: `pico_w_peripheral_bt/setup-env.ps1`

Purpose:
1. Sets execution policy for the current process (`RemoteSigned`).
2. Activates the workspace venv (`.venv\Scripts\Activate.ps1`).
3. Exports `ZEPHYR_BASE` to `c:\Users\admin\zephyrproject\zephyr`.
4. Moves to workspace root and prints the build command.

Usage:

```powershell
cd c:\Users\admin\zephyrproject
.\pico_w_peripheral_bt\setup-env.ps1
```

### 10.2 build.ps1

File: `pico_w_peripheral_bt/build.ps1`

The script wraps `west build` with sensible defaults and handles flashing via PowerShell-native file copy (bypassing the `west flash --runner uf2` path that fails on Windows — see [Section 11.3](#113-windows-uf2-flash-failure)).

```powershell
# Full pristine rebuild (default)
.\pico_w_peripheral_bt\build.ps1

# Incremental rebuild (skip -p always)
.\pico_w_peripheral_bt\build.ps1 -NoPristine

# Full rebuild + flash to Pico W in BOOTSEL mode
.\pico_w_peripheral_bt\build.ps1 -Flash

# Incremental rebuild + flash
.\pico_w_peripheral_bt\build.ps1 -NoPristine -Flash
```

Notes:
1. `cdc-acm-console` snippet is always applied (USB CDC ACM console routing).
2. `CONFIG_USB_DEVICE_INITIALIZE_AT_BOOT=y` is passed as a CMake cache option.
3. `-Flash` auto-detects the RPI-RP2 drive via WMI (`Get-WmiObject Win32_Volume`) and uses `Copy-Item` rather than Python's `shutil.copyfile`, which fails with `OSError 22` on Windows for USB mass-storage volumes. If the drive is not found, the script prints an error asking you to enter BOOTSEL mode first.
4. `-NoPristine` omits `-p always` from the west build command; Ninja performs an incremental build. Useful for rapid iteration when only source files changed.

---

## 11. Runtime Debugging — GATT Service Discovery Fix

After the firmware was flashed and confirmed to advertise and accept connections, BLE central clients (nRF Connect, LightBlue, etc.) reported **no GATT services discovered** — the service list appeared empty on every connection.

### 11.1 Symptom

- Device advertises correctly (UUID16 list: HRS/BAS/CTS + vendor 128-bit UUID visible in scan data).
- Central connects successfully.
- Central performs service discovery (ATT Read By Group Type request) — no services returned.
- Disconnect follows shortly after.

### 11.2 Root Cause — Single-packet-per-interrupt HCI driver

The CYW43439 communicates BT data via the **BTSDIO circular buffer** on the shared PIO-SPI bus. The Zephyr-side interrupt is driven by an **edge-sensitive GPIO** (the `WL_HOST_WAKE` line, GPIO 24). The firmware raises one GPIO edge per batch of BT data deposited into the circular buffer, regardless of how many packets are in that batch.

The poll thread in `zephyr_cyw43_drv.c` waits on a semaphore that is given by the GPIO ISR:

```c
// zephyr_cyw43_event_poll_thread:
while (1) {
    k_sem_take(&event_sem, K_MSEC(5000));  // woken by GPIO edge
    cyw43_poll();                           // calls cyw43_poll_func()
}
```

`cyw43_poll_func()` calls `cyw43_bluetooth_hci_process()` **once** if `cyw43_ll_bt_has_work()` is true. The original `cyw43_bluetooth_hci_process()` read **exactly one packet** per call and returned.

**The failure sequence on connection:**

1. Central connects → controller queues a `LE Connection Complete` HCI event in the BTSDIO circular buffer and fires one GPIO edge.
2. Poll thread wakes → reads the `LE Connection Complete` event → returns.
3. Central immediately sends an ATT `Read By Group Type` request → controller queues the corresponding HCI ACL data packet in the circular buffer.
4. **No new GPIO edge fires** (the controller only fires one edge per batch and may not fire again if no further BT traffic arrives to trigger it).
5. The ATT request sits in the circular buffer indefinitely. Zephyr's ATT layer never sees it, never responds, and the central times out → no services discovered.

The same problem would affect any multi-packet burst: connection parameter updates, encryption handshakes, MTU exchanges, and so on.

### 11.3 Windows UF2 Flash Failure

Before the GATT fix was found, a secondary issue was encountered:

```
west flash --runner uf2
→ OSError: [Errno 22] Invalid argument: 'E:\\zephyr.uf2'
```

`west flash --runner uf2` calls Python's `shutil.copyfile` to copy the UF2 to the RPI-RP2 drive. On Windows, the RP2040's USB mass-storage volume is not a standard NTFS/FAT filesystem — it is a special USB device that the Windows storage driver handles differently. Python's `shutil.copyfile` opens the file through the normal Win32 file API (`CreateFile`/`WriteFile`) which fails with `ERROR_INVALID_PARAMETER` (errno 22) on this device type.

**Fix:** The `-Flash` path in `build.ps1` bypasses `west flash` entirely and uses PowerShell's `Copy-Item`, which calls the Windows `CopyFile` shell API and handles mass-storage volumes correctly:

```powershell
$picoDrive = Get-WmiObject Win32_Volume |
    Where-Object { $_.Label -eq "RPI-RP2" } |
    Select-Object -First 1 -ExpandProperty DriveLetter

Copy-Item -Path $uf2 -Destination (Join-Path $picoDrive "zephyr.uf2") -Force
```

WMI volume label matching is used instead of a hard-coded drive letter so the script works regardless of which letter Windows assigns to the device.

### 11.4 Fix Applied — Drain the entire BTSDIO circular buffer per poll

**File:** `zephyr-cyw43-driver/drivers/wifi/zephyr_cyw43/src/zephyr_cyw43_bt_hci_drv.c`

`cyw43_bluetooth_hci_process()` was changed to loop until `cyw43_bluetooth_hci_read()` returns `len == 0` (empty buffer), rather than reading one packet and returning. Every packet queued since the last GPIO edge is now consumed in a single poll pass:

```c
// Before: reads exactly one packet per call
cyw43_bluetooth_hci_read(&cyw43_rxbuf[0], MAX_BT_MSG_SIZE, &cyw43_len);
// ... process one packet ...
return;

// After: drains the entire circular buffer
while (1) {
    cyw43_bluetooth_hci_read(&cyw43_rxbuf[0], MAX_BT_MSG_SIZE, &cyw43_len);
    if (cyw43_len == 0) {
        break;  // buffer empty, all packets processed
    }
    // ... process packet ...
}
```

`cyw43_bluetooth_hci_read()` calls `cyw43_btbus_read()` → `cybt_hci_read_packet()` which returns `*size = 0` when the BTSDIO circular buffer `bt2host_in_val == bt2host_out_val` (empty). The loop terminates cleanly on an empty buffer.

**Result:** After flashing the updated firmware, BLE central clients successfully discover all GATT services (HRS, BAS, IAS, CTS, DIS, and the custom vendor service) on the first connection attempt.

### 11.5 Build Script Improvements Made During This Session

| Change | Description |
|--------|-------------|
| `-NoPristine` switch | Added to `build.ps1`; omits `-p always` for incremental builds |
| `-Flash` rework | Replaced `west flash --runner uf2` with PowerShell `Copy-Item` via WMI drive detection |

---

## 12. Summary of Every File Changed

### Project files created from scratch

| File | Purpose |
|------|---------|
| `pico_w_peripheral_bt/CMakeLists.txt` | CMake entry point; sets `ZEPHYR_EXTRA_MODULES` |
| `pico_w_peripheral_bt/Kconfig` | App Kconfig (minimal) |
| `pico_w_peripheral_bt/prj.conf` | All Kconfig selections |
| `pico_w_peripheral_bt/boards/rpi_pico_rp2040_w.overlay` | DTS overlay |
| `pico_w_peripheral_bt/src/main.c` | BLE peripheral application |
| `pico_w_peripheral_bt/build.ps1` | Build script with `-NoPristine` and `-Flash` (PowerShell `Copy-Item` flash, not `west flash`) |
| `pico_w_peripheral_bt/setup-env.ps1` | Environment setup helper (execution policy, venv, `ZEPHYR_BASE`) |

### External driver cloned

```
zephyr-cyw43-driver/    ← git clone --recursive https://github.com/beechwoods-software/zephyr-cyw43-driver.git
```

### Files deleted from the cloned driver

| File | Reason |
|------|--------|
| `zephyr-cyw43-driver/dts/bindings/gpio/infineon,cyw43-gpio.yaml` | Duplicate of Zephyr 4.4 mainline file; caused DTS binding conflict |

### Files modified in hal/rpi_pico

**`modules/hal/rpi_pico/src/rp2_common/pico_cyw43_driver/cybt_shared_bus/cybt_shared_bus.c`**

```diff
 #include "cyw43_ll.h"
+#include <pico/platform/panic.h>
```

**`modules/hal/rpi_pico/src/rp2_common/pico_cyw43_driver/cybt_shared_bus/cybt_shared_bus_driver.c`**

```diff
 #include "cyw43_ll.h"
+#include <pico/platform/panic.h>
```

### Files modified in zephyr-cyw43-driver

**`zephyr-cyw43-driver/drivers/wifi/zephyr_cyw43/src/zephyr_cyw43_bt_hci_drv.c`**

`cyw43_bluetooth_hci_process()` changed to drain the full BTSDIO circular buffer per poll, fixing GATT service discovery failure (see [Section 11.2](#112-root-cause--single-packet-per-interrupt-hci-driver)):

```diff
-    /* Read and process a single HCI packet */
-    cyw43_bluetooth_hci_read(&cyw43_rxbuf[0], MAX_BT_MSG_SIZE, &cyw43_len);
-    if (cyw43_len < CYW43_PACKET_HEADER_SIZE) { return; }
-    /* ... process packet ... */
-    return;
+    /* Drain all queued packets in the circular buffer */
+    while (1) {
+        cyw43_bluetooth_hci_read(&cyw43_rxbuf[0], MAX_BT_MSG_SIZE, &cyw43_len);
+        if (cyw43_len == 0) { break; }  /* buffer empty */
+        if (cyw43_len < CYW43_PACKET_HEADER_SIZE) { break; }
+        /* ... process packet ... */
+    }
```

Additional defensive checks added in the same file:
- Short-packet guard (`cyw43_len < CYW43_PACKET_HEADER_SIZE` → break)
- TX overflow guard in `zephyr_cyw43_bt_hci_send()` (`buf->len > sizeof(cyw43_txbuf) - 3` → return `-EMSGSIZE`)
- Null `net_buf` allocation check with `LOG_ERR` instead of silent drop

### Final DTS overlay (`boards/rpi_pico_rp2040_w.overlay`)

This file was revised through five iterations as errors 2 through 6 were fixed. The final content:

```dts
/*
 * Copyright (c) 2024
 * SPDX-License-Identifier: Apache-2.0
 *
 * DTS overlay for Raspberry Pi Pico W - BLE peripheral application.
 * Uses the beechwoods zephyr-cyw43-driver for BT-over-SPI on the CYW43439.
 */

/ {
    chosen {
        zephyr,bt_hci = &cyw43_bt_hci;
    };
};

/* Disable the stock AIROC node so it does not conflict */
&pio0_spi0 {
    airoc-wifi@0 {
        status = "disabled";
    };
};

/* Delete the old cyw43_gpio label from the disabled node to avoid conflict */
/delete-node/ &cyw43_gpio;

/*
 * Repartition flash to carve out storage for BT settings (NVS backend).
 * Flash is 2MB. Layout:
 *   0x000000 - 0x0000FF : second_stage_bootloader (256 B)
 *   0x000100 - 0x1BFFFF : code_partition (~1.75 MB)
 *   0x1C0000 - 0x1FFFFF : storage_partition (256 KB)
 */
&code_partition {
    reg = <0x100 0x1BFF00>;
};

&flash0 {
    partitions {
        storage_partition: partition@1c0000 {
            label = "storage";
            reg = <0x1c0000 DT_SIZE_K(256)>;
        };
    };
};

/* Add the beechwoods CYW43 node with all required child nodes */
&pio0 {
    pio0_spi0: pio0_spi0 {
        infineon_cyw43_module: infineon_cyw43_module@0 {
            compatible = "infineon,cyw43";
            reg = <0x0>;
            spi-max-frequency = <10000000>;

            wl-on-gpios     = <&gpio0 23 GPIO_ACTIVE_HIGH>;
            bus-select-gpios = <&gpio0 24 GPIO_ACTIVE_HIGH>;
            host-wake-gpios  = <&gpio0 24 GPIO_ACTIVE_HIGH>;

            pinctrl-0 = <&airoc_wifi_default>;
            pinctrl-1 = <&airoc_wifi_host_wake>;
            pinctrl-names = "default", "host_wake";
            status = "okay";

            cyw43_led: cyw43_led {
                compatible = "infineon,cyw43-led";
            };

            cyw43_gpio: cyw43_gpio {
                compatible = "infineon,cyw43-gpio";
                gpio-controller;
                #gpio-cells = <2>;
                ngpios = <3>;
            };

            cyw43_bt_hci: cyw43_bt_hci {
                compatible = "infineon,cyw43-bt-hci";
                status = "okay";
            };
        };
    };
};
```

---

*Total number of build errors encountered: 8. Total pristine builds run: ~10. Final build time: ~4.5 minutes on a typical Windows machine (416 Ninja steps). Runtime issues fixed post-flash: 1 (GATT service discovery - HCI driver single-packet-per-interrupt limitation).*

---

## 13. Smart Pill Alarm - Coding Journal (Phase 1 Start)

Date: 2026-05-01

This section tracks the first implementation milestone for turning this BLE peripheral demo into a smart pill alarm product.

### 13.1 What was implemented

1. Application architecture moved from single-file demo flow into domain modules:
   - `src/pill/alarm_model.{h,c}`
   - `src/pill/alarm_scheduler.{h,c}`
   - `src/pill/app_settings.{h,c}`
   - `src/pill/pill_hw.{h,c}`
2. `src/main.c` was replaced with a product scaffold that wires:
   - BLE custom Medication/Pill service
   - alarm table encode/decode over BLE
   - scheduler tick loop and active alarm state
   - CTS time sync integration
   - BAS update + low-battery status flag
   - settings-backed persistence hooks
3. Build system updated to compile modular sources in `CMakeLists.txt`.
4. App Kconfig options added in `Kconfig`:
   - `CONFIG_PILL_MAX_ALARMS`
   - `CONFIG_PILL_LOW_BATTERY_THRESHOLD_PERCENT`
   - `CONFIG_PILL_MOTION_THRESHOLD_MILLI_G`
   - `CONFIG_PILL_MOTION_HITS_TO_CLEAR`
5. Board overlay extended for target hardware mapping:
   - SSD1306 on I2C0 (`0x3c`)
   - MPU-6050 on I2C0 (`0x68`)
   - alarm buzzer GPIO alias
   - 7 weekday LED GPIO aliases
6. `prj.conf` updated for product baseline and peripheral enablement.

### 13.2 BLE custom service payloads (initial schema)

- Alarm table characteristic (read/write, encrypted):
  - versioned compact payload
  - count + up to `CONFIG_PILL_MAX_ALARMS` entries
  - each entry: hour, minute, weekday_mask, pill_kind, enabled
- Command characteristic (write without response, encrypted):
  - `1`: acknowledge/stop active alarm
  - `2`: snooze 5 minutes (current implementation updates minute field)
- Status characteristic (read/notify):
  - alarm active flag
  - battery percent
  - connection flag
  - low battery flag
  - active alarm index

### 13.3 Persistence behavior (current)

- Storage keys under `pill/*`:
  - `pill/alarms`
  - `pill/last_epoch`
- Alarm table and last synced epoch are saved via `settings_save_one`.
- On boot, app loads from settings subtree and restores scheduler base if epoch exists.

### 13.4 Hardware behavior (current)

- Buzzer: GPIO on/off path implemented.
- Motion cancel: MPU-6050 sampling implemented with threshold + consecutive-hit filter.
- Battery: placeholder fixed value (95%) still used; ADC conversion not implemented yet.
- OLED and weekday LED rendering logic: not implemented yet (pins and aliases prepared).

### 13.5 Build verification

Build command run after integration:

```powershell
.\pico_w_peripheral_bt\build.ps1 -NoPristine
```

Result: build succeeded and generated `build/zephyr/zephyr.uf2`.

### 13.6 Next implementation steps (Phase 1 completion)

1. Replace battery placeholder with ADC divider measurement + percent mapping for Li-ion 1S.
2. Add OLED presenter module and display pages for active alarm and idle status.
3. Add weekday LED service module and map weekday bits to LED outputs.
4. Harden BLE write validation further (length/range/schema evolution support).
5. Add alarm snooze/day-rollover handling for edge cases (minute overflow and day transitions).
6. Add explicit bond-state policy checks for critical writes (beyond encrypted permissions).

## 14. Next Implementation Steps (continuation) — 2026-05-02

Goals:
- Complete remaining Phase‑1 features and prepare Phase‑2 integration.
- Replace placeholders with real hardware drivers and add tests.

Primary tasks:
1. Implement ADC battery mapping — replace fixed battery placeholder with sampled ADC value, apply divider compensation and piecewise percent mapping, add calibration constants and unit tests.
2. Add OLED presenter module — implement SSD1306 page framebuffer, pages for idle/active alarm/status, and a simple nav API.
3. Add weekday LED driver — map `weekday-led0..6` aliases, implement refresh task and brightness safe-guards.
4. Implement MPU‑6050 motion-cancel — driver wrapper for I2C sampling, configurable threshold and hit-count logic.
5. Harden BLE write validation — schema validation, size/range checks, and permission gate for critical writes.
6. Add unit and integration tests — common code units, mocked HALs, and a simple on-target smoke test for alarm firing.
7. Update journal, README, and change log — document changes, assumptions, and test results.

Acceptance criteria (per task):
- ADC: mapped battery percentage within ±3% of bench reference; tests cover nominal and edge voltages.
- OLED: displays alarm/idle pages correctly; no heap allocations at runtime.
- LEDs: correct weekday mapping; low-power idle when no updates.
- MPU‑6050: motion cancels alarm within configured hits; fails safely if sensor absent.
- BLE: invalid writes rejected with proper ATT error; schema versioning supported.
- Tests: CI-local build passes; target smoke test toggles buzzer and records event in settings.

Notes & implementation constraints:
- Follow Zephyr logging macros (`LOG_MODULE_DECLARE`, `LOG_INF`, `LOG_ERR`) in all new C files.
- Keep stack usage conservative; annotate thread stacks if raised.
- Add top-file header (author, date, purpose) to each new source file.
- Favor bounded APIs and explicit size checks; avoid dynamic allocations where possible.
- Update `pico_w_peripheral_bt/journal.md` with a short entry for each completed task.

Planned file targets:
- `src/pill/battery_adc.c` (ADC + mapping)
- `src/pill/display_oled.c/h` (SSD1306 presenter)
- `src/pill/weekday_leds.c/h`
- `src/pill/motion_sensor.c/h` (MPU‑6050 wrapper)
- `src/pill/ble_validation.c` (write validators)
- `tests/unit/*` and `tests/integration/*`

Next actions:
- I'll start with ADC implementation and unit tests, then iterate on OLED.

## 15. Battery ADC Implementation — 2026-05-02

Summary:
- Implemented `src/pill/battery_adc.c` ADC sampling and Li-ion voltage→percentage mapping.
- Enabled ADC feature by adding `CONFIG_PILL_BATTERY_ADC=1` to `prj.conf`.
- Integrated with hardware layer: `src/pill/pill_hw.c` uses `pill_battery_get_percent()` when enabled.

Files changed:
- `src/pill/battery_adc.c` — ADC sampling, divider compensation, piecewise mapping.
- `src/pill/battery_adc.h` — public API with safe inline fallback.
- `pico_w_peripheral_bt/prj.conf` — `CONFIG_PILL_BATTERY_ADC=1` added.

Notes:
- Default ADC channel: 0 (GPIO26). Default VREF: 3300 mV. Default divider: 2.
- Fallback returns 95% if ADC unavailable or read fails.
- Next: add unit tests and calibration constants; tune mapping per hardware.

## 16. BLE Write Validation — 2026-05-02

Summary:
- Added strict validator for alarm-table wire payloads: `src/pill/ble_validation.c`.
- Validator checks: version, length, count bounds, and per-entry field ranges using `pill_alarm_validate()`.
- Integrated validator into GATT path: `src/ble/pill_svc.c` now calls `pill_ble_validate_alarm_table()` before decode/commit.
Updated build inputs: `CMakeLists.txt` includes `src/pill/ble_validation.c`.

Result:
- Invalid or malformed writes are rejected early with appropriate ATT error codes.

Next: update README and journal with these changes, then proceed to polishing docs and remaining TODOs.

## 17. Pill directory organization — 2026-05-02

- Added `src/pill/README.md` documenting recommended module layout, mapping of existing files to suggested subdirectories (alarm/, hw/, include/pill/), and step-by-step refactor instructions.
- Added `src/pill/pill.h` aggregator header that includes common public headers (`pill/alarm_ctrl.h`, `pill/alarm_model.h`, `pill/alarm_scheduler.h`, `pill/app_settings.h`, `pill/battery_adc.h`, `pill/ble_validation.h`, `pill/display.h`, `pill/leds.h`, `pill/pill_hw.h`).
- No source code logic changed. Proposed follow-up: move files into `include/pill/` and `src/pill/{alarm,hw}` and update `CMakeLists.txt` to add `target_include_directories` and new source paths (I can perform this refactor on request).

Files added:
- `src/pill/README.md`
- `src/pill/pill.h`

Purpose: improve discoverability and prepare for a safe, incremental physical refactor of the pill module.
Next: update README and journal with these changes, then proceed to polishing docs and remaining TODOs.

---

## Change: Restart advertising on disconnect

- **Date:** 2026-05-02
- **Files:** src/ble/ble_mgr.c
- **Summary:** When a BLE central disconnects, advertising is now restarted after a 5 second delay instead of immediately. Implemented using a `k_delayed_work` handler that calls `bt_le_adv_start()`; any pending restart is cancelled when a connection is established. Replaced `printk()` logging with Zephyr `LOG_*` macros in `ble_mgr.c`.
- **Rationale / pitfalls:** Delay avoids tight reconnect loops and gives the central time to settle. The delayed-work handler runs in the system workqueue context — avoid blocking calls there. We cancel scheduled work on connect to prevent duplicate restarts.

---

## Overlay excerpt (copied)

```markdown
## 13. Phase‑2: Display, LEDs, Battery ADC

- Implemented a basic SSD1306 presenter (`src/pill/display.c`) that:
    - Uses the devicetree `oled_display` alias when present.
    - Maintains a page-oriented framebuffer and a tiny 5x7 font.
    - Writes the framebuffer via Zephyr's `display_write()` API.
    - Falls back to `LOG_INF()` if no display is present.

- Implemented weekday LED control (`src/pill/leds.c`) that:
    - Reads `weekday-led0..6` aliases from the overlay.
    - Configures GPIOs and drives the appropriate LED for a weekday.
    - Logs missing aliases instead of failing.

- Implemented ADC sampling and Li‑ion mapping (`src/pill/battery_adc.c`):
    - Uses Zephyr ADC API to sample channel 0 (GPIO26 by convention).
    - Converts raw ADC codes to millivolts, compensates for a 2:1 divider,
        and maps to percentage with a small piecewise-linear curve.
    - Provides conservative fallbacks and logs errors when ADC is
        unavailable.

Pitfalls and notes:
- The SSD1306 presenter uses ~1KB static framebuffer memory; ensure
    resource budgets are acceptable for your build configuration.
- The ADC channel, VREF and divider are configurable via macros in the
    source if your board wiring differs from the defaults.
```

This directive is placed in the overlay before the `&pio0 { ... }` block.





## 27. SDK root selection hardening (nested path guard) — 2026-05-04

- **Date:** 2026-05-04
- **Files modified:** `pico_w_peripheral_bt/build.ps1`, `pico_w_peripheral_bt/setup-env.ps1`
- **Summary:** Hardened SDK detection so scripts only accept a Zephyr SDK root that contains both `cmake/zephyr/gnu/generic.cmake` and populated GNU toolchain directories under `gnu/`.

- **What changed:**
    - Reused/added `Test-SdkRoot()` checks that validate both CMake and GNU toolchain presence.
    - Updated candidate fallback loops to use `Test-SdkRoot()` instead of checking only for `generic.cmake`.
    - Added normalization path handling for pre-set `ZEPHYR_SDK_INSTALL_DIR` in `setup-env.ps1` (if nested invalid path is provided, parent is tried and normalized).
    - Improved warnings to explicitly mention running SDK `setup.cmd` when GNU toolchains are missing.

- **Why:**
    - A duplicated/nested path like `...\zephyr-sdk-1.0.1\zephyr-sdk-1.0.1` can contain SDK metadata but no `gnu` directory, causing CMake failure:
      `Unable to find 'x86_64-zephyr-elf' ... in ...\gnu`.
    - Previous fallback logic selected that invalid path because it checked only for `generic.cmake`.

- **Validation:**
    - Confirmed valid SDK root on this machine is `C:\Users\admin\zephyr-sdk-1.0.1` with populated `gnu/*-zephyr-*` toolchains.
    - Build validation is run after this change via `build.ps1`.

- **Pitfalls / notes:**
    - Keep `ZEPHYR_EXTRA_MODULES` ordering intact in `CMakeLists.txt`; this change only affects environment/toolchain selection.

## 28. Chunked BLE table writes (alarm + kinds) — 2026-05-04

- **Date:** 2026-05-04
- **Files modified:** `pico_w_peripheral_bt/src/ble/pill_svc.c`
- **Summary:** Added a chunked write protocol in `pill_svc` so alarm table and pill-name table payloads can be split across multiple BLE writes and reassembled on the MCU before validation/commit.

- **Protocol (v2 frame):**
    - Header: `[ver:1][flags:1][transfer_id:1][total_len_le16:2][chunk_offset_le16:2][chunk_len_le16:2]`
    - Data: `[chunk_data:chunk_len]`
    - `ver=2` indicates chunked frame wrapper.
    - Flags: `START=0x01`, `END=0x02`, `ABORT=0x04`.
    - Reassembled payload is the existing table payload format (`WIRE_VERSION=1`) and is passed through existing validators/decoders.

- **Behavior:**
    - Backward compatible: single-frame legacy payload writes still work unchanged.
    - Partial chunk writes return success for that chunk and wait for remaining chunks.
    - On complete reassembly, service validates and commits once.
    - Strict checks enforce sequential offsets, stable transfer ID/total length, and bounded copy lengths.
    - `ABORT` resets transfer state safely.

- **Why:**
    - Prevent failures when table payload size exceeds practical ATT packet size (or client write size limits) by supporting app-layer fragmentation and MCU-side reassembly.

- **Pitfalls / notes:**
    - Transfer state is kept per characteristic (`alarm` and `kinds`) and assumes serialized writes in BLE context.
    - Out-of-order chunks are rejected (current implementation expects monotonic offsets).
    - Existing CYW43 local fixes remain unchanged and still apply.
