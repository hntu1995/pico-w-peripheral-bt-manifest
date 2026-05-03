# Build script for Pico W BLE peripheral application
# Usage: .\build.ps1 [-Flash] [-NoPristine]
#
#   -Flash        Copy UF2 to the RPI-RP2 drive after a successful build.
#                 Uses PowerShell Copy-Item (avoids the Python shutil.copyfile
#                 bug on Windows where west flash --runner uf2 fails with
#                 OSError 22 on the special USB mass-storage volume).
#   -NoPristine   Skip the -p always pristine build flag (incremental build).
param(
    [switch]$Flash,
    [switch]$NoPristine
)

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectRoot = Split-Path -Parent $scriptDir
$westRoot = $null
$appDir    = $scriptDir

$westCmd = Get-Command west -ErrorAction SilentlyContinue
if ($westCmd) {
    try {
        $westRoot = (& $westCmd.Source topdir 2>$null | Select-Object -First 1).Trim()
    } catch {
        $westRoot = $null
    }
}

if (-not $westRoot) {
    $westRoot = $projectRoot
}

# Activate the Python venv that has west installed
$venvActivate = Join-Path $projectRoot ".venv\Scripts\Activate.ps1"
if (Test-Path $venvActivate) {
    & $venvActivate
} else {
    Write-Warning "Venv not found at $venvActivate -- assuming west is already on PATH"
}

# Ensure Zephyr toolchain environment is valid.
# Some setups accidentally set ZEPHYR_TOOLCHAIN_VARIANT to a path, which breaks CMake.
$sdkCandidates = @(
    (Join-Path $env:USERPROFILE "zephyr-sdk-1.0.1\zephyr-sdk-1.0.1"),
    (Join-Path $env:USERPROFILE "zephyr-sdk-1.0.1"),
    (Join-Path $env:USERPROFILE "zephyr-sdk-1.0.0\zephyr-sdk-1.0.0"),
    (Join-Path $env:USERPROFILE "zephyr-sdk-1.0.0"),
    "C:\zephyr-sdk-1.0.1\zephyr-sdk-1.0.1",
    "C:\zephyr-sdk-1.0.1",
    "C:\zephyr-sdk-1.0.0\zephyr-sdk-1.0.0",
    "C:\zephyr-sdk-1.0.0"
)

$sdkRoot = $null
# Prefer an already-set ZEPHYR_SDK_INSTALL_DIR if it points to a valid SDK install.
# If the provided path is a nested install (e.g. ...\zephyr-sdk-1.0.1\zephyr-sdk-1.0.1),
# try its parent directory as a fallback so duplicated paths are handled.
function Test-SdkRoot([string]$p) {
    # Valid SDK root must have the zephyr generic.cmake AND at least one toolchain dir under gnu
    $generic = Join-Path $p 'cmake\zephyr\gnu\generic.cmake'
    if (-not (Test-Path $generic)) { return $false }
    $gnuDir = Join-Path $p 'gnu'
    if (-not (Test-Path $gnuDir)) { return $false }
    $archs = Get-ChildItem -Directory $gnuDir -ErrorAction SilentlyContinue | Where-Object { $_.Name -match '-zephyr-' }
    if ($archs -and $archs.Count -gt 0) { return $true }
    return $false
}

if ($env:ZEPHYR_SDK_INSTALL_DIR) {
    $providedSdk = $env:ZEPHYR_SDK_INSTALL_DIR

    # Try the provided path first
    if (Test-SdkRoot $providedSdk) {
        $sdkRoot = $providedSdk
    } else {
        # If the provided path doesn't look like the SDK root, try its parent
        try {
            $item = Get-Item -LiteralPath $providedSdk -ErrorAction Stop
            if ($item -and $item.PSIsContainer) {
                $parent = $item.Parent
                if ($parent -and (Test-SdkRoot $parent.FullName)) {
                    $sdkRoot = $parent.FullName
                    Write-Host "Normalized ZEPHYR_SDK_INSTALL_DIR from '$providedSdk' to '$sdkRoot'"
                }
            }
        } catch {
            # Ignore and fall back to candidate list below
        }
    }
}

if (-not $sdkRoot) {
    foreach ($candidate in $sdkCandidates) {
        if (Test-Path (Join-Path $candidate "cmake\zephyr\gnu\generic.cmake")) {
            $sdkRoot = $candidate
            break
        }
    }
}

if ($sdkRoot) {
    $env:ZEPHYR_TOOLCHAIN_VARIANT = "zephyr"
    $env:ZEPHYR_SDK_INSTALL_DIR = $sdkRoot
    Write-Host "Using Zephyr SDK: $env:ZEPHYR_SDK_INSTALL_DIR"
} else {
    Write-Warning "Zephyr SDK not found. Build may fail unless toolchain is already configured."
}

# Build arguments
# west build [-p always] -S cdc-acm-console -b rpi_pico/rp2040/w <app> -- -DCONFIG_USB_DEVICE_INITIALIZE_AT_BOOT=y
$buildArgs = @()
if (-not $NoPristine) {
    $buildArgs += "-p", "always"
}
$buildArgs += @(
    "-S", "cdc-acm-console",
    "-b", "rpi_pico/rp2040/w",
    $appDir,
    "--",
    "-DCONFIG_USB_DEVICE_INITIALIZE_AT_BOOT=y",
    "-DZEPHYR_SDK_INSTALL_DIR=$env:ZEPHYR_SDK_INSTALL_DIR"
)

Push-Location $westRoot
try {
    west build @buildArgs
    $exitCode = $LASTEXITCODE
} finally {
    Pop-Location
}

$uf2 = Join-Path $westRoot "build\zephyr\zephyr.uf2"

if ($exitCode -eq 0) {
    if (Test-Path $uf2) {
        Write-Host ""
        Write-Host "Build succeeded. UF2 image: $uf2"
        Write-Host "Build includes snippet: cdc-acm-console"
        Write-Host "Build includes CMake option: CONFIG_USB_DEVICE_INITIALIZE_AT_BOOT=y"
        Write-Host "To flash: hold BOOTSEL on Pico W, connect USB, then copy $uf2 to the RPI-RP2 drive."
    }

    if ($Flash) {
        Write-Host ""
        Write-Host "Looking for RPI-RP2 drive..."

        # Find the RPI-RP2 volume (works even if drive letter varies)
        $picoDrive = Get-WmiObject Win32_Volume |
            Where-Object { $_.Label -eq "RPI-RP2" } |
            Select-Object -First 1 -ExpandProperty DriveLetter

        if (-not $picoDrive) {
            Write-Error "RPI-RP2 drive not found. Hold BOOTSEL on the Pico W and reconnect USB, then retry."
            exit 1
        }

        $dest = Join-Path $picoDrive "zephyr.uf2"
        Write-Host "Copying $uf2 -> $dest"
        try {
            # Use Copy-Item (not Python shutil) to avoid OSError 22 on Windows
            Copy-Item -Path $uf2 -Destination $dest -Force
            Write-Host "Flash complete. The Pico W will reboot automatically."
        } catch {
            Write-Error "Flash failed: $_"
            exit 1
        }
    }
}

exit $exitCode