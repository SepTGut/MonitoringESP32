# Development Log

## 2026-07-19

### Fixed (Code Review)
- **ConfigManager Mutex Deadlock on Startup**: Removed `_mutex` lock/unlock calls from `loadDefaults()`. Since `loadDefaults()` is called in the global static constructor before the FreeRTOS scheduler starts, taking the mutex failed and returned early, leaving settings initialized with garbage memory (which resulted in corrupted WiFi SSID names like `,␝?␁`).
- **ArduinoJson Type-Strictness Deserialization Bug**: Replaced `is<T>()` with robust `containsKey()` and `.as<T>()` in `updateFromJson()`. Previously, numeric conversions (like integer `150` to `float`, or integer `68` to `uint8_t`) failed type strictness checks, causing saved web configurations to ignore fields or remain unapplied.
- **Self-Healing Configuration Loader**: Added an automatic configuration validation check inside `load()`. If the loaded AP SSID is empty or contains non-printable garbage characters (due to past memory corruption or save errors), it automatically resets the configuration to defaults and saves a clean copy to LittleFS, self-healing the device on next boot.
- **WiFi AP ignores ConfigManager**: `wifi_manager.cpp` was using hardcoded `WIFI_AP_SSID`/`WIFI_AP_PASS` instead of `configManager.getConfig().apSSID`/`apPass`. Changing AP name or password from the web settings now takes effect after reboot.
- **ConfigManager Thread Safety & Validation**: Thread-protected `SystemConfig` read/write access via `_mutex` locks in `getConfig()` and `updateFromJson()`, preventing data races. Added check boundaries for timing fields (`sensorPollMs`, `wsPushMs`, `serialLogMs`) and constrained `pf` to `[0.0, 1.0]` to prevent invalid HTTP payloads.
- **MovingAverage Division-by-Zero Guard**: Added a safeguard in `MovingAverage` constructor to fall back to a window size of 1 if `windowSize` is initialized to 0.
- **INA226 default address mismatch**: `config_manager.cpp` defaulted INA226 addresses to `0x40`/`0x41` while `pin_config.h` defined them as `0x44`/`0x45`. Now uses `pin_config.h` defines as the single source of truth for defaults.
- **DS18B20 OneWire traffic in dummy mode**: `tempBus.requestTemperature()` was called unconditionally even in simulated dummy mode. Now guarded by `!currentCfg.dummyMode`.
- **LCD format specifier**: `snprintf` used `%5d` (signed) for `uint32_t rpm`. Changed to `%5lu` with `(unsigned long)` cast.
- **SensorTask stack size**: Increased from 4096 to 8192 bytes to prevent stack overflow during I2C scanning, Serial.printf, and LCD operations.

### Added
- **ADC Anti-Drift (Continuous Offset Tracking)**: Added exponential moving average (EMA) DC offset tracking to both ZMPT101B and ZMCT103C sensor drivers. The offset now adapts every measurement cycle to follow ESP32 ADC thermal drift, preventing upward-creeping readings over time. Also added a noise floor dead-band that clamps near-zero RMS values to exactly 0.0A/0.0V.
- **Serial Logger Update**: Updated `serial_reader.py` to parse all 3 temperatures (External 1, External 2, Internal CPU), display CPU temperature with color-coded warning thresholds (green/amber/red), and use absolute log directory paths.
- **LCD Redesign & Animations (4-Screen Layout)**:
  - Redesigned 16x2 character LCD layout to a 4-screen rotation sequence (AC Overview, DC Overview, Rotor & Temperatures, and WiFi Status).
  - Registered custom glyph icons for lightning bolts (AC Power), batteries (DC Power), thermometers (Temperature), and a 4-frame propeller.
  - Implemented real-time propeller rotation math inside the 10Hz SensorTask polling loop, with spin rate proportional to wind generator RPM.
  - Separated high-frequency rotor rendering (10Hz / 100ms) on Screen 2 from low-frequency text updates (500ms) to ensure smooth animations with zero screen flicker.
  - Programmed Screen 1 to show detailed voltages and currents for both DC Channels simultaneously.
  - Programmed Screen 2 to show the static combination of the rotor speed, internal CPU temperature, and external temperatures.
- **Internal ESP32 CPU Temperature**:
  - Declared C-style extern `temprature_sens_read()` to read built-in CPU temperature sensor in `freertos_tasks.cpp`.
  - Added Celsius conversion and update calls inside the main 10Hz SensorTask measurement loop.
  - Formatted Screen 2 on physical LCD to output both speed and CPU temperature: `[RPM]RPM CPU:[temp_esp]C` on Line 0, and external temps as `Ext:[t1]/[t2]C` on Line 1.
  - Updated serial monitor formatting to output CPU temp alongside existing variables.
  - Added `tEsp` key to Web JSON data endpoints (`/api/data` and WebSocket broadcasts).
  - Integrated dynamic card and animated progress bar for ESP32 CPU Temperature into Dashboard UI layouts.
  - Synced client files to preview dashboard (`test/web_preview/`) and GitHub Pages (`docs/`).

## 2026-07-17

### Added
- **I2C LCD 16x2 Display Controller**:
  - Implemented auto-rotating screens (AC Overview, DC Channels, Speed/Temps, WiFi Status).
  - Added flicker-free drawing throttling (500ms refresh rate limit).
  - Dynamically displays STA connection SSID and local DHCP IP address or softAP credentials.
- **Universal I2C Address Auto-Discovery Scanner**:
  - Scans all 127 I2C addresses at startup.
  - Automatically identifies LCD module addresses (`0x20-0x27`, `0x38-0x3F`) and INA226 power sensor addresses (`0x40-0x4F`).
  - Stores discovered addresses thread-safely in DataManager to display on the dashboard settings panel.
- **Software-Defined Sensor Assignment**:
  - Added dropdown selectors to the settings portal to dynamically map physical I2C addresses to DC Channel 1 (Main Load) and DC Channel 2 (Auxiliary).
  - Added full null-pointer safety (`nullptr` checks) for missing or unassigned I2C sensors.
- **GitHub Pages Demo Mode Sync**:
  - Updated mock interceptors in `script.js` to simulate dynamic I2C scans and handle the new settings parameters.
- **Workspace Agent Customizations**:
  - Created `.agents/AGENTS.md` containing workspace behavioral rules.
- **Dynamic Serial Logging Rate**:
  - Updated the sensor measurement task loop to read `serialLogMs` config values dynamically at runtime.
- **Frontend Sync**:
  - Copied latest `index.html`, `script.js`, and `style.css` to `docs/` to keep GitHub Pages demo up to date.
- **Standalone Test & Preview Directory**:
  - Created a dedicated `test/` folder containing local dashboard preview assets (`web_preview/`) and local preview guides.
  - Forced `isDemoMode = true` in `test/web_preview/script.js` to guarantee that the test preview dashboard always runs in simulated Demo Mode by default, regardless of protocol or host (fully resolving VS Code Live Server extension port 5500 display issues). Production files inside `data/` and `docs/` remain clean and detect live mode dynamically.
- **On-Demand I2C Scanner**:
  - Integrated thread-safe, cross-core volatile signal `i2cScanRequested` to safely trigger an I2C scan on Core 1 from Core 0 network requests, preventing physical I2C bus collision crashes.
  - Added REST endpoint `POST /api/i2c-scan` to trigger dynamic bus scans.
  - Added a "Scan I2C Bus" button to the settings assignment panel on both the production and test preview dashboards.
  - Resolved dynamic browser promise hanging: Added mock resolver for `/api/i2c-scan` in the javascript `apiFetch` simulator to prevent the page getting stuck displaying "Scanning..." in Demo Mode.
  - Added global CORS headers (`Access-Control-Allow-Origin: *`) and options preflight handlers (`HTTP_OPTIONS`) to the ESP32's `AsyncWebServer` configuration, allowing local testing web servers (such as VS Code Live Server) to successfully call these API endpoints.
- **Virtual LCD Live Preview**:
  - Added a retro green backlit 16x2 Virtual LCD display preview component directly into the dashboard (spans 2 columns, fully responsive, rotates status pages in-sync with hardware, simulated in JS).
  - Configured the LCD preview card to be hidden on the production dashboard by default, dynamically showing it only when running in Demo Mode to keep the production hardware view clean.
  - Synced changes to `test/web_preview/` and `docs/` directories.
- **LCD Character Optimization**:
  - Optimized the C++ formats and JavaScript preview outputs for AC Power and Temperatures to eliminate character truncation (ensuring label + data values fit exactly inside the 16-character horizontal boundary).
- **Embedded Dummy Sensors Mode**:
  - Added a dynamic "Dummy Sensors Mode" toggle to the configuration database (`SystemConfig`), saved/loaded via LittleFS `/config.json`.
  - Added a new Settings card section on the web dashboard to easily toggle simulated output.
  - Implemented firmware-level math equations in the sensor polling task loop on Core 1 that generate realistic telemetry values (sine waves) when dummy mode is enabled. This allows testing the LCD rotation, web dashboard live view, and serial logging without physical sensors wired to the ESP32.

### Notes
- Settings updates are stored in LittleFS config file `/config.json` and applied automatically on reboot.
- Consolidated all I2C accesses on Core 1 to avoid multi-threaded bus conflicts.

## 2026-07-15

### Added
- Complete project structure with modular architecture
- Configuration system: `config.h` (calibration/timing) + `pin_config.h` (GPIO)
- FreeRTOS dual-core tasks: Core 1 (sensors), Core 0 (network)
- Thread-safe DataManager with mutex protection
- Sensor drivers:
  - INA226 (I2C DC power) ×2
  - DS18B20 (OneWire temperature) ×2
  - RPM sensor (interrupt-based IR)
  - ZMPT101B (AC voltage RMS) ×2
  - ZMCT103C (AC current RMS) ×1
- Moving average filter utility
- WiFi manager (AP mode)
- ESPAsyncWebServer with WebSocket data push
- Premium glassmorphism web dashboard with:
  - 3 hero power ring cards (AC, DC#1, DC#2)
  - 10 metric cards with progress bars
  - Sidebar navigation (responsive)
  - Animated background orbs
  - Spinning turbine SVG
- Demo mode for GitHub Pages (auto-simulated data)
- Serial monitor formatted output (1 Hz)
- README.md with architecture, wiring, calibration guide
- docs/ folder synced from data/ for GitHub Pages
- Python serial monitor and logger tool in `tools/serial_logger/`
- Custom clean upload utility scripts (`tools/upload_clean.ps1`, `tools/upload_clean.bat`)

### Notes
- AC power calculated as: P = Vrms × Irms × PF (configurable)
- ZMPT101B #1 used for power calculation, #2 for raw monitoring
- All analog sensors on ADC1 pins (ADC2 unavailable with WiFi)
- DS18B20 uses async conversion (non-blocking)
- Added pyserial-based port selector script for easy desktop diagnostics
- Added PowerShell and Command Prompt scripts to erase flash, build/upload firmware, and uploadfs sequentially


