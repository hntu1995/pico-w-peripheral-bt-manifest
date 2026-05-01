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
$westRoot  = Split-Path -Parent $scriptDir
$appDir    = $scriptDir

# Activate the Python venv that has west installed
$venvActivate = Join-Path $westRoot ".venv\Scripts\Activate.ps1"
if (Test-Path $venvActivate) {
    & $venvActivate
} else {
    Write-Warning "Venv not found at $venvActivate -- assuming west is already on PATH"
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
    "-DCONFIG_USB_DEVICE_INITIALIZE_AT_BOOT=y"
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