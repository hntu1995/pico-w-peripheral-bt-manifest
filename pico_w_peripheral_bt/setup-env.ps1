# Environment setup script for Pico W BLE peripheral project
# Usage: .\setup-env.ps1

Set-ExecutionPolicy -Scope Process -ExecutionPolicy RemoteSigned -Force

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectRoot = Split-Path -Parent $scriptDir
$westRoot = $null
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

$venvActivate = Join-Path $westRoot ".venv\Scripts\Activate.ps1"
$zephyrBase = Join-Path $westRoot "zephyr"

if (-not (Test-Path $venvActivate)) {
    $venvActivate = Join-Path $projectRoot ".venv\Scripts\Activate.ps1"
}

if (Test-Path $venvActivate) {
    & $venvActivate
    Write-Host "Activated venv: $venvActivate"
} else {
    Write-Warning "Venv activation script not found at $venvActivate"
}

if (Test-Path $zephyrBase) {
    $env:ZEPHYR_BASE = $zephyrBase
    Write-Host "ZEPHYR_BASE=$env:ZEPHYR_BASE"
} else {
    Write-Warning "Zephyr folder not found at $zephyrBase"
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
foreach ($candidate in $sdkCandidates) {
    if (Test-Path (Join-Path $candidate "cmake\zephyr\gnu\generic.cmake")) {
        $sdkRoot = $candidate
        break
    }
}

if ($sdkRoot) {
    $env:ZEPHYR_TOOLCHAIN_VARIANT = "zephyr"
    $env:ZEPHYR_SDK_INSTALL_DIR = $sdkRoot
    Write-Host "ZEPHYR_TOOLCHAIN_VARIANT=$env:ZEPHYR_TOOLCHAIN_VARIANT"
    Write-Host "ZEPHYR_SDK_INSTALL_DIR=$env:ZEPHYR_SDK_INSTALL_DIR"
} else {
    Write-Warning "Zephyr SDK not found. Install SDK 1.0.1 (or 1.0.0) and set ZEPHYR_SDK_INSTALL_DIR."
}

# Ensure driver/module dependency checkout is complete for clean machines.
$pythonCmd = Get-Command python -ErrorAction SilentlyContinue

$cyw43Module = Join-Path $westRoot "zephyr-cyw43-driver"
$cyw43Source = Join-Path $cyw43Module "drivers\wifi\zephyr_cyw43\src\cyw43-driver\src\cyw43_ll.c"
if ((Test-Path $cyw43Module) -and -not (Test-Path $cyw43Source)) {
    Write-Host "Initializing zephyr-cyw43-driver submodule..."
    Push-Location $cyw43Module
    try {
        git submodule update --init --recursive
    } catch {
        Write-Warning "Failed to initialize zephyr-cyw43-driver submodule: $_"
    } finally {
        Pop-Location
    }
}

$mbedtlsModule = Join-Path $westRoot "modules\crypto\mbedtls"
$tfPsaDir = Join-Path $mbedtlsModule "tf-psa-crypto"
$frameworkDir = Join-Path $mbedtlsModule "framework"
$tfPsaCore = Join-Path $tfPsaDir "core"
$tfWrapper = Join-Path $tfPsaCore "psa_crypto_driver_wrappers_no_static.c"
$tfConfigCheck = Join-Path $tfPsaCore "tf_psa_crypto_config_check_before.h"

if (Test-Path $mbedtlsModule) {
    if (-not (Test-Path (Join-Path $tfPsaDir ".git"))) {
        Write-Host "Cloning TF-PSA-Crypto dependency for Mbed TLS..."
        Push-Location $mbedtlsModule
        try {
            git clone https://github.com/Mbed-TLS/TF-PSA-Crypto.git tf-psa-crypto
            Push-Location $tfPsaDir
            try {
                git checkout 29160dd877d29658279fd683b2ae57b320ddcf09
                git submodule update --init --recursive
            } finally {
                Pop-Location
            }
        } catch {
            Write-Warning "Failed to clone/setup TF-PSA-Crypto: $_"
        } finally {
            Pop-Location
        }
    }

    if (-not (Test-Path (Join-Path $frameworkDir ".git"))) {
        Write-Host "Cloning mbedtls-framework dependency for Mbed TLS..."
        Push-Location $mbedtlsModule
        try {
            git clone https://github.com/Mbed-TLS/mbedtls-framework.git framework
            Push-Location $frameworkDir
            try {
                git checkout dff9da04438d712f7647fd995bc90fadd0c0e2ce
            } finally {
                Pop-Location
            }
        } catch {
            Write-Warning "Failed to clone/setup mbedtls-framework: $_"
        } finally {
            Pop-Location
        }
    }

    if ($pythonCmd -and (Test-Path $tfPsaDir)) {
        if (-not (Test-Path $tfWrapper)) {
            Write-Host "Generating TF-PSA driver wrappers..."
            Push-Location $tfPsaDir
            try {
                python .\scripts\generate_driver_wrappers.py .\core
            } catch {
                Write-Warning "Failed to generate TF-PSA driver wrappers: $_"
            } finally {
                Pop-Location
            }
        }

        if (-not (Test-Path $tfConfigCheck)) {
            Write-Host "Generating TF-PSA config-check headers..."
            Push-Location $tfPsaDir
            try {
                python .\scripts\generate_config_checks.py
            } catch {
                Write-Warning "Failed to generate TF-PSA config-check headers: $_"
            } finally {
                Pop-Location
            }
        }
    } elseif (-not $pythonCmd) {
        Write-Warning "Python executable not found on PATH; skipping TF-PSA file generation."
    }
}

Push-Location $westRoot
Write-Host "Workspace root: $westRoot"
Write-Host "Run build with: .\pico_w_peripheral_bt\build.ps1 [-Flash]"
