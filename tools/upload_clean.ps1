# =============================================================
#  upload_clean.ps1 - ESP32 Clean Upload Script
#  1. Generates pre-compressed PROGMEM web assets
#  2. Erases all flash (program and LittleFS data)
#  3. Compiles and uploads firmware
#  4. Builds and uploads LittleFS filesystem image
# =============================================================

$port = ""
if ($args.Count -gt 0) {
    $port = $args[0]
}

$pioPath = "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe"
if (!(Test-Path $pioPath)) {
    $pioPath = "pio"
}

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$genScript = Join-Path $scriptDir "generate_web_assets.py"

Write-Host "====================================================" -ForegroundColor Cyan
Write-Host "  ESP32 Wind Monitor - Clean Upload Utility" -ForegroundColor Cyan
Write-Host "  Using PlatformIO: $pioPath" -ForegroundColor Gray
Write-Host "====================================================" -ForegroundColor Cyan

# 0. Generate Web Assets
if (Test-Path $genScript) {
    Write-Host "`n[0/3] Generating pre-compressed web assets (web_assets.h)..." -ForegroundColor Yellow
    python $genScript
}

# 1. Erase all flash
Write-Host "`n[1/3] Erasing ESP32 flash (clean all program and data)..." -ForegroundColor Yellow
if ($port) {
    & $pioPath run -t erase --upload-port $port
} else {
    & $pioPath run -t erase
}

if ($LASTEXITCODE -ne 0) {
    Write-Host "`nError: Erase flash failed." -ForegroundColor Red
    exit 1
}

# 2. Upload firmware
Write-Host "`n[2/3] Compiling and uploading firmware..." -ForegroundColor Yellow
if ($port) {
    & $pioPath run -t upload --upload-port $port
} else {
    & $pioPath run -t upload
}

if ($LASTEXITCODE -ne 0) {
    Write-Host "`nError: Firmware upload failed." -ForegroundColor Red
    exit 1
}

# 3. Upload LittleFS filesystem
Write-Host "`n[3/3] Building and uploading LittleFS filesystem data..." -ForegroundColor Yellow
if ($port) {
    & $pioPath run -t uploadfs --upload-port $port
} else {
    & $pioPath run -t uploadfs
}

if ($LASTEXITCODE -ne 0) {
    Write-Host "`nError: Filesystem upload failed." -ForegroundColor Red
    exit 1
}

Write-Host "`n====================================================" -ForegroundColor Cyan
Write-Host "  SUCCESS: ESP32 completely cleaned and programmed!" -ForegroundColor Green
Write-Host "====================================================" -ForegroundColor Cyan
