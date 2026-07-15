@echo off
rem =============================================================
rem  upload_clean.bat — ESP32 Clean Upload Command Batch File
rem  1. Erases all flash (program and LittleFS data)
rem  2. Compiles and uploads firmware
rem  3. Builds and uploads LittleFS filesystem image
rem =============================================================

echo ====================================================
echo   ESP32 Wind Monitor — Clean Upload Utility
echo ====================================================

set PORT=%1

rem 1. Erase all flash
echo.
echo [1/3] Erasing ESP32 flash (clean all program and data)...
if "%PORT%"=="" (
    call pio run -t erase
) else (
    call pio run -t erase --upload-port %PORT%
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
    call pio run -t upload
) else (
    call pio run -t upload --upload-port %PORT%
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
    call pio run -t uploadfs
) else (
    call pio run -t uploadfs --upload-port %PORT%
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
