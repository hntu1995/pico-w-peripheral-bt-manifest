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

Push-Location $westRoot
Write-Host "Workspace root: $westRoot"
Write-Host "Run build with: .\pico_w_peripheral_bt\build.ps1 [-Flash]"
