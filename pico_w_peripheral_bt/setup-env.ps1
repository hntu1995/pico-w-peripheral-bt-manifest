# Environment setup script for Pico W BLE peripheral project
# Usage: .\setup-env.ps1

Set-ExecutionPolicy -Scope Process -ExecutionPolicy RemoteSigned -Force

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$westRoot  = Split-Path -Parent $scriptDir
$venvActivate = Join-Path $westRoot ".venv\Scripts\Activate.ps1"
$zephyrBase = Join-Path $westRoot "zephyr"

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
