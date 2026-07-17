# Development Log

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


