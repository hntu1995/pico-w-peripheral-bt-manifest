# Pico W BLE Peripheral — Zephyr RTOS Project

A complete BLE GATT peripheral application for the **Raspberry Pi Pico W** (RP2040 + CYW43439) running **Zephyr RTOS 4.4.99**.

## Project Overview

This project builds a BLE peripheral device that advertises and hosts standard GATT services:
- **HRS** — Heart Rate Service
- **BAS** — Battery Service  
- **IAS** — Immediate Alert Service
- **CTS** — Current Time Service (with helper API)
- **DIS** — Device Information Service
- **SMP** — Bluetooth Security Manager with passkey pairing

The Pico W connects to the CYW43439 BLE/WiFi chip over a custom PIO-SPI interface, which requires a third-party driver module from beechwoods-software to expose BT-HCI functionality to Zephyr's Bluetooth stack.

## Current Purpose & Status

This repository now serves as a Smart Pill Alarm reference firmware built on the Raspberry Pi Pico W using Zephyr RTOS 4.4.99. The project began as a BLE peripheral demo and has been extended into a product scaffold. Notable completed features and current status:

- BLE: Heart Rate Service (HRS), Battery Service (BAS), Current Time Service (CTS), Immediate Alert Service (IAS), Device Information Service (DIS), and a custom `Pill` GATT service (alarm-table, command, status).
- Security: Bluetooth SMP with passkey pairing and bond storage (NVS backed).
- Hardware integrations: SSD1306 OLED presenter, weekday LED indicators, MPU‑6050 motion sensor integration, and buzzer control.
- Battery monitoring: ADC-based measurement on GPIO26 with voltage-divider compensation and Li‑ion voltage→percentage mapping.
- Robustness: strict BLE write validation for alarm-table writes, HCI-driver fix to drain queued CYW43 packets, and Pico HAL fixes applied.

Status notes:

- Build: Verified on Windows; `build/zephyr/zephyr.uf2` produced for `rpi_pico/rp2040/w`.
- Runtime: Advertises as `Pico W Pill Alarm`, supports pairing, GATT discovery, alarm scheduling, and simple UI via OLED/LEDs.
- Tests: No automated unit tests included; development relied on manual smoke testing. See `pico_w_peripheral_bt/journal.md` for full build and debug history.

Purpose:

- Provide a working reference for BLE-based embedded products on the Pico W using Zephyr and a third-party CYW43439 driver.
- Demonstrate integrating sensors, display, and persistent configuration over GATT.


## Architecture

```
pico-w-peripheral-bt-manifest/
├── west.yml                    # Manifest: Zephyr 4.4.99 + custom BT driver + HAL patches
├── requirements.txt            # Python dependencies (west + Zephyr dev tools)
├── pico_w_peripheral_bt/       # Application source and build scripts
│   ├── src/main.c              # BLE peripheral app (HRS, BAS, IAS, CTS, DIS, SMP)
│   ├── prj.conf                # Kconfig (CYW43 driver, BT stack, sizes, NVS)
│   ├── CMakeLists.txt          # App build config (dual-layout module detection)
│   ├── setup-env.ps1           # Setup script (workspace-root auto-detection)
│   ├── build.ps1               # Build script (workspace-root auto-detection)
│   ├── boards/                 # Board devicetree overlays
│   └── README.md               # Detailed build journal
└── .venv/                      # Python venv (created during setup)

West fetches (via manifest):
├── zephyr/                     # Zephyr RTOS 4.4.99
├── zephyr-cyw43-driver/        # beechwoods BT-over-SPI driver (custom branch)
├── modules/hal/rpi_pico/       # RP2040 HAL with cybt_shared_bus fixes
├── modules/hal/cmsis_6/        # ARM CMSIS-6
├── modules/hal/infineon/       # Infineon HAL
├── modules/crypto/mbedtls/     # mbed TLS
└── ...
```

## Workspace Layouts

This project supports two deployment patterns:

### Layout 1: Original Local Development (single directory)
For local development on the build machine:
```
c:\Users\<you>\zephyrproject\
├── .venv/
├── zephyr/
├── zephyr-cyw43-driver/
├── modules/
├── pico_w_peripheral_bt/
├── west.yml
└── ...
```

### Layout 2: Manifest Clone (nested via `west init -l .`)
For cloning on a new machine:
```
c:\Users\<you>\project\
├── pico-w-peripheral-bt-manifest/     # Manifest repo (cloned)
│   ├── .venv/
│   ├── west.yml
│   ├── requirements.txt
│   ├── pico_w_peripheral_bt/
│   └── README.md
├── zephyr/                            # Fetched by west update
├── zephyr-cyw43-driver/               # Fetched by west update
├── modules/                           # Fetched by west update
└── ...
```

**Important Difference:** In Layout 2, the workspace root is the parent of the manifest repo. The app CMakeLists now auto-detects the module path, and the build scripts auto-detect the workspace root via `west topdir`.

## Prerequisites

### System Requirements
- **Windows 10/11** (or Linux/macOS with path adjustments)
- **Python 3.12+** (for Zephyr SDK)
- **Git**
- **CMake 4.3+**
- **Ninja**
- **dtc** (Device Tree Compiler)
- **Zephyr SDK 1.0.1+** ([download](https://github.com/zephyrproject-rtos/sdk-ng/releases))

### Hardware
- **Raspberry Pi Pico W**
- **USB cable** (for programming and USB serial console)

## Quick Start — New Machine (Manifest Clone)

Clone and build on a fresh machine:

```powershell
# Clone the manifest repository
git clone https://github.com/hntu1995/pico-w-peripheral-bt-manifest.git
Set-Location .\pico-w-peripheral-bt-manifest

# Create and activate Python venv
python -m venv .venv
.\.venv\Scripts\Activate.ps1

# Install west
python -m pip install -U pip
pip install west

# Initialize manifest workspace
west init -l .
west update

# Install Zephyr dependencies
pip install -r .\requirements.txt
west zephyr-export

# Build
.\pico_w_peripheral_bt\setup-env.ps1
.\pico_w_peripheral_bt\build.ps1

# Flash to Pico W (hold BOOTSEL, plug in USB, then run):
.\pico_w_peripheral_bt\build.ps1 -Flash
```

## Build Commands

```powershell
# Default: pristine build with USB CDC console
.\pico_w_peripheral_bt\build.ps1

# Incremental build (skip pristine clean)
.\pico_w_peripheral_bt\build.ps1 -NoPristine

# Build and flash (Pico W must be in BOOTSEL mode)
.\pico_w_peripheral_bt\build.ps1 -Flash
```

### Build Output
- **Pristine build:** Always recompiles from clean (-p always)
- **Snippet:** cdc-acm-console (USB serial at 115200 baud)
- **Board:** rpi_pico/rp2040/w
- **CMake option:** CONFIG_USB_DEVICE_INITIALIZE_AT_BOOT=y
- **Artifact:** build/zephyr/zephyr.uf2

## Flashing to Hardware

### Method 1: Automatic (via `-Flash` flag)
```powershell
# Hold BOOTSEL on Pico W, plug in USB, then run:
.\pico_w_peripheral_bt\build.ps1 -Flash
```
The script finds the RPI-RP2 drive and copies zephyr.uf2 automatically.

### Method 2: Manual Copy
```powershell
# Hold BOOTSEL, plug in USB
Copy-Item build/zephyr/zephyr.uf2 E:\zephyr.uf2  # E: = RPI-RP2 drive
```

## Serial Console

Connect via any serial terminal (115200 baud, 8N1) to the USB serial port:

```powershell
# Find COM port:
Get-WmiObject Win32_SerialPort

# Or use PuTTY, miniterm, or another terminal emulator
```

## Repository Files

| File/Folder | Purpose |
|---|---|
| `west.yml` | Manifest: pins Zephyr 4.4.99, driver branch, HAL patches |
| `requirements.txt` | Python dependencies (west, Zephyr build tools) |
| `pico_w_peripheral_bt/` | Application source, build scripts, board overlays |
| `pico_w_peripheral_bt/README.md` | Detailed build journal with all fixes documented |

## Recent Improvements (Portability Fixes)

### Workspace Path Auto-Detection (Latest)
- **CMakeLists.txt**: Searches for zephyr-cyw43-driver in both original and manifest layouts
- **setup-env.ps1 & build.ps1**: Auto-detect workspace root via `west topdir` (works in both layouts)
- **requirements.txt**: Root-level dependency file for fresh clone setup

### Previous Fixes (See pico_w_peripheral_bt/README.md)
- GATT service discovery: Fixed HCI packet draining in zephyr_cyw43_bt_hci_drv.c
- Cybt shared bus crashes: Added pico/platform/panic.h includes in hal_rpi_pico modules
- DTS binding conflicts: Removed duplicate infineon,cyw43-gpio.yaml from driver
- Kconfig mismatches: Aligned cdc-acm-console snippet with device_next stack symbols

## Troubleshooting

### Build Errors

**Error:** `is not a valid zephyr module`
- **Cause:** Workspace layout mismatch (fresh clone)
- **Fix:** Ensure `west update` completed. CMakeLists.txt now auto-detects the correct module path.

**Error:** Kconfig warnings about `USB_DEVICE_INITIALIZE_AT_BOOT`
- **Status:** Non-fatal; build succeeds
- **Fix:** Can be suppressed if only using device_next stack exclusively

**Error:** `RPI-RP2 drive not found` during `-Flash`
- **Fix:** Hold BOOTSEL on Pico W, disconnect/reconnect USB, retry the command

### Runtime Issues

- **GATT services not discovered:** See [pico_w_peripheral_bt/README.md](pico_w_peripheral_bt/README.md#11-runtime-debugging--gatt-service-discovery-fix) for HCI packet draining fix
- **No serial output:** Verify cdc-acm-console snippet is enabled (it is by default)

## Documentation

**Detailed Build Journal:** See [pico_w_peripheral_bt/README.md](pico_w_peripheral_bt/README.md) for:
- Every build error encountered and its solution
- Why a third-party driver is required for BLE on Pico W
- Full device tree and Kconfig design explanations
- GATT service discovery debugging notes
- Summary of all modified files across the workspace

## License

This project integrates:
- Zephyr RTOS (Apache 2.0)
- beechwoods-software/zephyr-cyw43-driver (Apache 2.0)
- Raspberry Pi HAL modules (per upstream licenses)

## Contributing

This is a reference project. To adapt it for your own use:
1. Fork the manifest repository
2. Update `west.yml` to point to your own driver and HAL forks
3. Commit and push your changes
4. Other machines can clone and build using the standard sequence above

## Further Reading

- [Zephyr Documentation](https://docs.zephyrproject.org/)
- [beechwoods-software/zephyr-cyw43-driver](https://github.com/beechwoods-software/zephyr-cyw43-driver)
- [Raspberry Pi Pico W Datasheet](https://datasheets.raspberrypi.com/picow/pico-w-datasheet.pdf)
