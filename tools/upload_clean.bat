@echo off
rem =============================================================
rem  upload_clean.bat — ESP32 Clean Upload Command Batch File
rem  1. Generates pre-compressed PROGMEM web assets
rem  2. Erases all flash (program and LittleFS data)
rem  3. Compiles and uploads firmware
rem  4. Builds and uploads LittleFS filesystem image
rem =============================================================

echo ====================================================
echo   ESP32 Wind Monitor — Clean Upload Utility
echo ====================================================

set PORT=%1

rem Find internal PlatformIO executable first to avoid broken Python launchers
set PIO_PATH="%USERPROFILE%\.platformio\penv\Scripts\pio.exe"
if not exist %PIO_PATH% (
    set PIO_PATH=pio
)

rem 0. Generate Web Assets
echo.
echo [0/3] Generating pre-compressed web assets header (web_assets.h)...
python "%~dp0generate_web_assets.py"

rem 1. Erase all flash
echo.
echo [1/3] Erasing ESP32 flash (clean all program and data)...
if "%PORT%"=="" (
    call %PIO_PATH% run -t erase
) else (
    call %PIO_PATH% run -t erase --upload-port %PORT%
)
if %ERRORLEVEL% neq 0 (
    echo.
    echo Error: Erase flash failed.
    exit /b %ERRORLEVEL%
)

rem 2. Upload firmware
echo.
echo [2/3] Compiling and uploading firmware...
if "%PORT%"=="" (
    call %PIO_PATH% run -t upload
) else (
    call %PIO_PATH% run -t upload --upload-port %PORT%
)
if %ERRORLEVEL% neq 0 (
    echo.
    echo Error: Firmware upload failed.
    exit /b %ERRORLEVEL%
)

rem 3. Upload LittleFS filesystem
echo.
echo [3/3] Building and uploading LittleFS filesystem data...
if "%PORT%"=="" (
    call %PIO_PATH% run -t uploadfs
) else (
    call %PIO_PATH% run -t uploadfs --upload-port %PORT%
)
if %ERRORLEVEL% neq 0 (
    echo.
    echo Error: Filesystem upload failed.
    exit /b %ERRORLEVEL%
)

echo.
echo ====================================================
echo   SUCCESS: ESP32 completely cleaned and programmed!
echo ====================================================
