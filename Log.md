# Development Log

## 2026-08-28

### Updated (Complete Tool Suite Modernization)
- **Unified Serial Dashboard & Waveform Plotter (`tools/serial_logger/serial_logger.py`)**:
  - Replaced legacy MAX9814 audio microphone logic with full support for the **ACS758 50A Inverter Current Sensor** (GPIO 33 & ADS1115 A3).
  - Modernized regex parsing to support the production 1Hz telemetry report (`[Generator AC]`, `[MPPT Battery]`, `[Inverter DC In]`, `[ACS758 Diag]`, `[Inverter AC Out]`, `[ZMCT Raw Diag]`, `[Inverter Eff]`, `[Control/Lights]`, `[Temperature]`), standalone hardware test reports, and raw stream (`RAW_PLOT:`).
  - Added real-time tracking and ANSI display for Battery SoC (%), remaining Battery Wh, Inverter DC Power, Inverter Efficiency (%), Generator AC RMS voltage, and Inverter AC RMS load metrics.
  - Enhanced Window 1 (Diagnostic Dashboard) and Window 2 (Live Waveform Plotter) with dynamic bar graphs and local TCP streaming (`127.0.0.1:8888`).
- **Browser-Based Web Serial Plotter (`tools/web_serial_plotter/index.html`)**:
  - Replaced MAX9814 microphone with ACS758 Inverter Current & Power telemetry (GPIO 33 / ADS A3).
  - Added dynamic metric cards for Generator AC, Inverter AC Output, AC Load Current, Inverter DC Input, MPPT Battery + SoC, and Rotor Speed.
  - Expanded canvas waveform channels to plot live Generator AC RMS, Inverter AC RMS, Inverter Load Current, ACS758 Current, Battery Power, Aux Control Power, Rotor RPM, and Battery SoC (%).
  - Synchronized updated Web Serial Plotter to `data/web_serial_plotter.html` and `docs/web_serial_plotter.html`.
- **1-Hour Battery & System Telemetry Logger (`tools/monitor_ina1_1hour.py`)**:
  - Upgraded parser to seamlessly capture production 1Hz telemetry frames, `RAW_PLOT:`, and legacy data formats.
  - Added recording for Inverter DC Current (A), Inverter DC Power (W), Inverter Efficiency (%), Generator AC Voltage (V), Inverter AC Voltage (V), Inverter AC Current (A), and Inverter AC Power (W) to CSV output.
  - Maintained 1-second console progress status with live battery SoC (%), energy Wh accumulation, and capacity mAh tracking.
- **Clean Flash & Firmware Upload Scripts (`tools/upload_clean.bat`, `tools/upload_clean.ps1`, `tools/erase_and_monitor.py`)**:
  - Added automatic web asset compilation step (`tools/generate_web_assets.py`) prior to compiling/uploading firmware to guarantee PROGMEM embedded flash headers (`src/network/web_assets.h`) are always in sync with `data/`.
  - Upgraded CLI argument forwarding and port selection for single/dual window logging modes.
- **Embedded Web Assets Header (`src/network/web_assets.h`)**:
  - Regenerated PROGMEM byte arrays with maximum GZIP compression (18.73 KB in flash).

## 2026-08-27

### Fixed & Improved
- **ACS758 DC Current Sensor Hardware Route to GPIO 33**:
  - Re-routed ACS758 current sampling directly to physical hardware pin `GPIO 33` (`PIN_ACS758_INPUT`) where the analog voltage shifts cleanly by $+160\text{ mV}$ under load.
  - Implemented 32x ADC oversampling on GPIO 33 with signed moving average filtering to suppress internal SAR ADC noise.
  - Set calibrated multiplier to $0.9825\text{f}$ for exact $3.93\text{ A}$ multimeter tracking and automatic $0.00\text{ A}$ idle zero-current detection.
- **INA226 Battery DC Voltage Fine-Tuning ($12.10\text{ V}$)**:
  - Adjusted `INA226_VOLTAGE_CAL` to $0.94551\text{f}$ to perfectly match $12.10\text{ V}$ multimeter measurement.
  - Set default ACS758 resting zero baseline `ACS758_ZERO_OFFSET` to $1695.0\text{ mV}$ in `config.h` and LittleFS `config.json`.
- **ZMPT2 Inverter AC Output Voltage $220.0\text{ V}$ Multimeter Calibration**:
  - Calibrated `zmpt2Cal` scale multiplier to $327.8\text{f}$ based on physical $220.0\text{ V}$ multimeter AC output measurement on ADS1115 A1.
  - Aligned inverter AC power calculations ($P_{\text{AC}} = 220\text{V} \times 0.134\text{A} \approx 29.5\text{ W}$) and inverter efficiency metric.
- **ACS758 ($3.93\text{ A}$) and ZMCT103C ($0.134\text{ A}$) Multimeter Calibration**:
  - Calibrated ACS758 DC current scaling multiplier to $7.860\text{f}$ based on $3.93\text{ A}$ multimeter measurement ($20.0\text{ mV}$ raw $\Delta$ on ADS1115 A3).
  - Calibrated ZMCT103C AC load current factor to $0.2207\text{f}$ based on $0.134\text{ A}$ multimeter measurement ($607.1\text{ mV}$ raw RMS signal on ADS1115 A2).
  - Verified INA226 battery DC voltage accuracy ($12.10\text{ V} - 12.12\text{ V}$ reading vs $12.11\text{ V}$ multimeter).
- **Dual ACS758 ADC Comparison Mode (ADS1115 A3 vs Direct ESP32 GPIO 33)**:
  - Added concurrent sampling of ESP32 internal ADC1 on GPIO 33 (`PIN_ACS758_INPUT`) alongside ADS1115 Channel A3.
  - Implemented real-time side-by-side diagnostic logging (`[ACS758 Compare]`) displaying millivolts, baseline offset delta ($\Delta$), and computed Amperes for both ADC paths.
- **ACS758 $\pm 0.5\text{A}$ Idle Drift Elimination & Thermal Auto-Tracking**:
  - Increased zero-current dead-band `ACS758_DEADBAND_A` to $0.70\text{ A}$ ($\approx 9\text{ mV}$ threshold) to cleanly eliminate ambient $\pm 0.5\text{A}$ thermal noise while preserving $1.11\text{ A}$ inverter idle and $3.65\text{ A}$ load measurements.
  - Implemented continuous **Thermal Zero-Drift Tracking** ($\alpha = 0.0005$) that dynamically adapts `_zeroOffset` during idle periods to cancel temperature-induced baseline drift over extended runtimes.
- **ACS758 Default Hardware Baseline Alignment ($1675.0\text{ mV}$)**:
  - Fixed $65.55\text{A}$ false current reading on boot/reset caused by mismatch between $2500\text{ mV}$ nominal 5V setting and actual $1675\text{ mV}$ ADS1115 hardware divider level.
  - Set `ACS758_ZERO_OFFSET` in `config.h` and default baseline in `config_manager.cpp` to $1675.0\text{ mV}$.
  - Aligned all default baseline offsets (`zmpt1=1665mV`, `zmpt2=935mV`, `zmct=1672mV`, `acs758=1675mV`) and packaged default `data/config.json`.
- **ACS758 DC Current Sensor Signed Filtering & Idle Jitter Elimination**:
  - Eliminated $0\text{A} - 1.5\text{A}$ ghost current fluctuations caused by premature absolute rectification (`fabsf`) before moving average filtering.
  - Implemented signed filtering so symmetric Gaussian noise cancels naturally to $0.00\text{ A}$.
  - Added 16x ADC oversampling with $50\mu\text{s}$ delay in `readRawMilliVolts()` to suppress high-frequency power ripple spikes.
  - Increased noise dead-band `ACS758_DEADBAND_A` to $0.40\text{ A}$ to cleanly clamp zero-load fluctuations to **`0.00 A`**.
- **Web Dashboard Sensor Source Hardware Badges**:
  - Added hardware sensor source badges to all 17 measurement cards and hero power cards in the Web UI dashboard (`data/index.html`, `data/style.css`, `docs/`, `test/web_preview/`).
  - Categorized and color-coded each sensor by hardware interface:
    - **I2C Bus (Cyan)**: `INA226 #1 · I2C 0x44`, `INA226 #2 · I2C 0x45`, `LCD · I2C 0x27`
    - **16-Bit ADS1115 / GPIO (Purple)**: `ZMPT101B · ADS1115 (A0)`, `ZMPT101B · ADS1115 (A1)`, `ZMCT103C · ADS1115 (A2)`, `ACS758 · ADS1115 (A3)`
    - **Direct GPIO (Green)**: `IR Optical Sensor · GPIO 16`, `ESP32 Internal TSENS`
    - **1-Wire Bus (Amber)**: `DS18B20 #1 · GPIO 4`, `DS18B20 #2 · GPIO 4`
  - Regenerated PROGMEM GZIP flash arrays in `src/network/web_assets.h`.
- **USB Serial Command Processor (`CAL`, `SCAN`, `REBOOT`)**:
  - Added real-time serial command parser in `networkTaskFunction()` over USB UART (`115200` baud).
  - Supported commands: `CAL` / `CALIBRATE` (executes 500-sample ADC zero calibration and saves to flash), `SCAN` (runs dynamic I2C bus device discovery), and `REBOOT` (restarts ESP32).
- **Persistent ADC Calibration & Boot Reset Prevention**:
  - Removed destructive boot-time auto-calibration routines from `ZMPT101B::begin()`, `ZMCT103C::begin()`, and `sensorTaskFunction()` that were wiping out saved offsets in flash on every reboot.
  - Implemented direct restoration of saved baseline offsets (`zmpt1OffsetMv`, `zmpt2OffsetMv`, `zmctOffsetMv`, `acs758OffsetMv`) from LittleFS `/config.json` upon startup.
  - Restricted zero-point calibration strictly to user-triggered requests (`/api/adc-calibrate`), ensuring calibration survives power-cycles and reboots.
- **ZMPT101B Floating Input Noise Suppression**:
  - Raised `ADC_NOISE_FLOOR_MV` in `zmpt101b.cpp` to `160.0f` ($\approx 38.7\text{ V}$) and `AC_MIN_VOLTAGE_CUTOFF` to `12.0f`.
  - Clamped ambient $50\text{Hz}$ electromagnetic op-amp pickup on open/floating generator terminals to **`0.0 V`** instead of displaying false ghost voltages ($35\text{V} - 36\text{V}$).
- **ZMCT103C AC Current Calibration & ACS758 Diagnostic Telemetry**:
  - Calibrated `ZMCT_CALIBRATION` factor to `0.1493f` ($0.69 \times \frac{0.132\text{A}}{0.61\text{A}}$), accurately scaling 40W heating element AC load measurement from $0.61\text{ A}$ down to exact reference current **$0.132\text{ A}$**.
  - Added live raw ADC millivolt telemetry logs (`[ACS758 Raw Diag]` and `[ZMCT Raw Diag]`) in `freertos_tasks.cpp` displaying raw sensor mV, zero baseline, delta mV, and active sampling source (ADS1115 vs Internal ADC GPIO).
- **ACS758 Hall-Effect DC Current Calibration & Dynamic Multiplier**:
  - Resolved current under-reading ($0.35\text{ A}$ raw vs $1.11\text{ A}$ multimeter on battery/inverter discharge line) caused by input voltage divider and ADC impedance scaling.
  - Added calibrated scaling factor `ACS758_CAL_MULTIPLIER` ($K_{\text{cal}} = 3.1714$) to `ACS758Sensor` driver and `config.h`, bringing the measured current to exact parity with external reference meters ($0.35\text{A} \times 3.1714 = 1.11\text{A}$).
  - Extended `SystemConfig` and `ConfigManager` to persist `acs758Cal` and dynamic zero-point baseline `acs758OffsetMv`.
  - Added ACS758 calibration multiplier setting in web dashboard (`data/index.html`, `data/script.js`, `docs/`, `test/web_preview/`) and regenerated flash assets (`src/network/web_assets.h`).
- **I2C Auto-Recovery, Multi-Address Backlight Pulse & Diagnostics**:
  - Implemented automatic SDA/SCL pin-reversal detection in `sensorTaskFunction()`: if 0 devices are found on the primary mapping, the firmware automatically scans the reversed pin mapping (SDA <-> SCL) and adopts the working configuration.
  - Added direct hardware backlight pulses in `LcdDisplay::begin()` across PCF8574 candidate addresses (`0x27`, `0x3F`, `0x20`, `0x38`) to force-illuminate the backlight circuit regardless of backpack IC variant.
  - Added GPIO pin state diagnostic logging (`[I2C Diag]`) to monitor SDA/SCL logic levels and detect physical shorts or missing pull-ups.
- **LCD Backlight Activation & I2C 100 kHz Compatibility**:
  - Adjusted `I2C_CLOCK_SPEED` from 400 kHz to standard 100 kHz (`100000UL`) in `config.h` to ensure 100% timing compatibility with PCF8574 LCD backpacks.
  - Added a 100ms power-on stabilization delay before I2C address scanning and set a 50ms bus timeout.
  - Updated `LcdDisplay::begin()` and `sensorTaskFunction()` to fallback to default address `0x27` and always initialize the display and backlight (`_lcd->init()`, `_lcd->backlight()`) even if the initial bus scan didn't receive an early ACK.
- **I2C Pin Assignment Synchronization & Config Save Resolution**:
  - Updated I2C pin assignments across firmware and diagnostic test suites to match hardware wiring: `PIN_I2C_SDA` (GPIO 21) and `PIN_I2C_SCL` (GPIO 22).
  - Enhanced `ConfigManager::begin()` and `ConfigManager::save()` with auto-formatting fallback on LittleFS write failure, ensuring that settings modifications sent via `/api/config` from the web dashboard are saved to flash successfully.
- **Zero-Dependency Fail-Safe Web Dashboard (Flash Embedded PROGMEM GZIP Assets)**:
  - Created `tools/generate_web_assets.py` to compress `index.html`, `style.css`, `script.js`, and `favicon.ico` into 18.2 KB of pre-compressed GZIP byte arrays in `src/network/web_assets.h`.
  - Implemented hybrid static asset delivery in `web_server.cpp`: checks for physical LittleFS files first, and automatically falls back to streaming PROGMEM GZIP assets via `beginResponse_P` with `Content-Encoding: gzip`.
  - Disabled `formatOnFail` in `LittleFS.begin(false)` across `config_manager.cpp` and `web_server.cpp` to prevent destructive partition formatting and file erasures during boot.
  - Guaranteed 100% web dashboard uptime across all connection modes even if LittleFS is unformatted or empty.
- **Bulletproof Web Server & Captive Portal Routing**:
  - Implemented `server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html")` alongside explicit static routes (`/`, `/index.html`, `/style.css`, `/script.js`, `/favicon.ico`, `/web_serial_plotter.html`) to ensure all paths and assets resolve reliably without redirect loops.
  - Added dedicated captive portal probe endpoints for Android (`/generate_204`, `/gen_204`), Apple (`/hotspot-detect.html`, `/canonical.html`, `/success.txt`), and Windows (`/connecttest.txt`, `/ncsi.txt`) with automatic redirect to AP IP `192.168.4.1`.
  - Added global CORS headers (`Access-Control-Allow-Origin: *`, `OPTIONS 200 OK`) to allow seamless cross-origin and local browser preview access.
  - Improved `onNotFound` fallback to prevent redirect loops when accessing via router IP in STA mode or via mDNS (`windmonitor.local`).
- **Web Dashboard Error Elimination & 4-Channel Asset Synchronization**:
  - Resolved DOM element ID caching mismatches in `script.js` left over from the pre-4-channel power architecture migration (`val-dcpwr2`, `power-ring-dc2`, `val-dcvolt2`, etc. removed; DOM cache now 100% matched to `index.html`).
  - Added embedded SVG turbine favicon to `index.html` and registered `/favicon.ico` (204 No Content) route handler in `web_server.cpp` to eliminate browser console 404 errors.
  - Wrapped settings inputs in a semantic `<form id="settings-form" onsubmit="event.preventDefault()">` to satisfy browser password management and eliminate accessibility/autofill warnings.
  - Modernized `startDemoSimulation()` in `script.js` to simulate realistic 4-channel power topology: Inverter 220V AC Output, MPPT Battery Charge, ACS758 50A Inverter DC Discharge, and INA226 #2 Control & Lighting Aux, along with Lakoni 65Ah / 780Wh battery SoC.
  - Enhanced preview / localhost fallback in `script.js` so browser previews automatically activate demo simulation cleanly without throwing connection exceptions or unhandled 404 fetch alerts.
  - Synchronized all web assets (`index.html`, `script.js`, `style.css`) across `data/`, `docs/`, and `test/web_preview/`.
- **Project Folder Relocation Verification & Path Synchronization**:
  - Verified project integrity following directory relocation to `d:\MyCode\New folder\MonitoringESP32`.
  - Tested and confirmed full firmware compilation (`pio run` -> `[SUCCESS]`, RAM: 12.5%, Flash: 55.3%) and LittleFS image generation (`pio run -t buildfs` -> `[SUCCESS]`).
  - Regenerated compilation database `compile_commands.json` via PlatformIO (`pio run -t compiledb`).
  - Updated include and fallback search paths in `.clangd` and `.vscode/settings.json` to reference `d:\MyCode\New folder\MonitoringESP32`.
  - Updated relative documentation links in `test/README.md`.
- **System Robustness & Sensor Noise Resilience**:
  - **RPM ISR Hardware Debouncing**: Added a $1500\,\mu\text{s}$ refractory period check (`RPM_DEBOUNCE_US`) to `RPMSensor::handleInterrupt()` to filter out optical bounce and electromagnetic spikes (supporting up to 40,000 RPM cleanly).
  - **INA226 NaN / Inf Float Protection**: Added `isnan()` and `isinf()` validation guards in `INA226Sensor::readVoltage()`, `readCurrent()`, and `readPower()` to prevent corrupt float propagation during intermittent I2C disconnects.
  - **LCD Memory Safety**: Added explicit pointer deallocation guard (`if (_lcd != nullptr) delete _lcd;`) in `LcdDisplay::begin(addr)` to prevent memory leaks during on-demand bus rescans.
  - **DataManager & ConfigManager Mutex Self-Healing**: Added lazy-creation fallbacks for FreeRTOS mutex handles to defend against early-boot static initialization races.
  - **Hardware Diagnostic Test Suite Synchronization**: Updated `test/hardware_test/src/main.cpp` pin definitions to match `pin_config.h` (`PIN_I2C_SDA = 22`, `PIN_I2C_SCL = 21`, `PIN_RPM = 16`, `PIN_ACS758_IN = 33`), applied debounced RPM ISR, and added ACS758 50A diagnostic reporting.
- **IDE Language Server (Clangd) Diagnostic False-Positives**:
  - Created `.clangd` configuration file to strip GCC/Xtensa-specific flags (`-mlongcalls`, `-fstrict-volatile-bitfields`, `-fno-tree-switch-conversion`, `-fno-jump-tables`, etc.) that triggered false diagnostics in Clang-based IDE language servers.
  - Added comprehensive include search paths for Arduino ESP32 core (`Arduino.h`), Xtensa GCC 15.1.0 standard headers, FreeRTOS, ESP-IDF components, and third-party library dependencies.
  - Created `.vscode/settings.json` configured with `--compile-commands-dir`, `--query-driver` pointing to the Xtensa toolchain compiler, and fallback flags.
  - Rebuilt compilation database (`pio run -t compiledb`) and verified 100% clean builds for both main firmware and hardware test suite (`[SUCCESS]`).

## 2026-08-26

### Added & Changed
- **Lakoni 12V 65Ah Battery Profile Recalibration (100% SoH / 65Ah / 780 Wh)**:
  - Updated battery energy calculation model in `freertos_tasks.cpp` to nominal 100% SoH ($12\text{V} \times 65.0\text{Ah} = 780.0\text{ Wh}$).
  - Updated Web Dashboard metric card in `data/index.html` and preview `test/web_preview/index.html` to **`Lakoni 65Ah (100% SoH / 65Ah)`**.
  - Updated client-side fallback energy scaling in `data/script.js` to $780.0\text{ Wh}$.
  - Updated Python diagnostic logger `tools/monitor_ina1_1hour.py` with 100% SoH nominal energy metrics ($780.0\text{ Wh}$).
- **I2C Bus Pin Reconfiguration**:
  - `PIN_I2C_SDA = 22` (I2C Data on GPIO 22).
  - `PIN_I2C_SCL = 21` (I2C Clock on GPIO 21).
- **GPIO 27 Power Switch Deep Sleep & Wake-Up Feature**:
  - Assigned **Power Switch input** to **GPIO 27** (`RTC_GPIO17`) with active `INPUT_PULLDOWN` (`PIN_POWER_SWITCH = 27`).
  - Added configurable timeout `POWER_SWITCH_TIMEOUT = 10000` (10s) and feature flag `ENABLE_POWER_SWITCH = true` in `config.h` and `SystemConfig` (`pwrSwEn`, `pwrSwTimeout`).
  - Implemented automatic Deep Sleep trigger in Core 1 sensor task when switch remains `LOW` continuously for the configured timeout: gracefully turns off LCD backlight, shuts down WiFi, flushes serial, sets RTC pull-down, and enters Deep Sleep with `EXT0` wake-up configured on switch `HIGH` (level 1).
  - Added `shutdown()` method to `LcdDisplay` and deep sleep wake-up reason detection in `main.cpp`.
- **GPIO Pin Configuration Cleanup**:
  - `PIN_RPM_INPUT = 16` (RPM sensor pulse input on GPIO 16).
  - `PIN_DS18B20 = 4` (DS18B20 OneWire temperature bus on GPIO 4).
  - `PIN_POWER_SWITCH = 27` (Power switch on RTC GPIO 27).

## 2026-08-23

### Fixed & Changed (Hardware Integration)
- **INA226 R100 Shunt Resistor Support & Current Measurement Fix**:
  - Updated `config.h` defaults: `INA226_SHUNT_OHM` updated from `0.01Ω` to `0.10Ω` (`R100` = 100mΩ) and `INA226_MAX_CURRENT` updated from `10.0A` to `0.80A` (adhering to INA226 81.92mV maximum differential shunt voltage input range).
  - Updated `ina226_sensor.cpp` driver to compute DC current directly from physical ADC shunt voltage ($I = \frac{V_{shunt}}{R_{shunt}}$) and power via $P = V_{bus} \times |I|$. This resolves calibration register overflow errors and ensures high-precision 16-bit measurement.
  - Updated standalone `test/hardware_test/src/main.cpp` diagnostic test to initialize and measure with `R100` ($0.10\Omega$) parameters.
- **Pin Reassignment (RPM -> GPIO 16, DS18B20 -> GPIO 17)**:
  - Reassigned **RPM Sensor Pulse Input** from GPIO 27 to **GPIO 16** (`PIN_RPM_INPUT = 16`).
  - Reassigned **DS18B20 OneWire Temperature Bus** from GPIO 4 to **GPIO 17** (`PIN_DS18B20 = 17`).
  - Verified firmware compilation with PlatformIO (`[SUCCESS]`).
- **ADS1115 Hardware ALRT (ALERT/RDY) Pin, Multi-ADDR Support & 400kHz Fast I2C**:
  - Implemented hardware `ALRT` (ALERT/RDY) pin conversion synchronization on `GPIO 19` (`PIN_ADS1115_ALERT`) with active-LOW pulse detection and `INPUT_PULLUP`.
  - Added full multi-address support for all 4 hardware `ADDR` pin configurations (`ADDR->GND: 0x48`, `ADDR->VDD: 0x49`, `ADDR->SDA: 0x4A`, `ADDR->SCL: 0x4B`) with automated multi-probe auto-discovery across `0x48..0x4B`.
  - Configured I2C bus clock to **400 kHz Fast-Mode** (`I2C_CLOCK_SPEED = 400000UL`), accelerating 16-bit ADC throughput and INA226 bus transactions.
  - Exposed ADS1115 ALRT pin status, active address, and I2C 400kHz speed in `/api/sysinfo`.
  - Rebuilt LittleFS binary image and verified firmware compilation with `[SUCCESS]`.
- **3x Code Optimization & Warning Elimination Cycles**:
  - **Iteration 1 (`b92ba62`)**: Eliminated 58 compiler warnings (57 deprecated `Bxxxxx` macros in `lcd_display.cpp` replaced with `0bxxxxx` binary literals, 1 `volatile++` deprecation in `rpm_sensor.cpp`). Removed 4 legacy alias float fields in `SensorData` (`ac_voltage`, `ac_voltage2`, `ac_current`, `ac_power`), saving 16 bytes per frame.
  - **Iteration 2 (`f1f1e04`)**: Corrected dummy simulation topology in `freertos_tasks.cpp` so ZMPT2 simulates 220V Inverter AC output, ZMCT simulates inverter load current, and acPower matches actual hardware calculations. Saved 56 bytes flash.
  - **Iteration 3 (`iter3`)**: Streamlined JSON serialization in `web_server.cpp` and `data/script.js` with compact, zero-duplicate payload keys. Optimized static file route handler lambdas, saving 516 bytes of flash (down to 718,223 bytes / 54.8%).
- **Inverter AC Output (ZMPT2 + ZMCT) & Generator AC (ZMPT1) Topology**:
  - Configured **ZMPT101B #1 (ADS1115 A0)** on the raw Generator AC output (before rectifier/MPPT).
  - Configured **ZMPT101B #2 (ADS1115 A1)** & **ZMCT103C (ADS1115 A2)** on the Inverter 220V AC Output side for real AC load power measurement ($P_{\text{inv\_ac}} = V_{\text{zmpt2}} \times I_{\text{zmct}} \times PF$).
  - Added real-time Inverter conversion efficiency calculation ($\eta = \frac{P_{\text{inv\_ac}}}{P_{\text{inv\_dc}}} \times 100\%$) comparing ACS758 50A DC input power to Inverter AC output power.
  - Rebuilt LittleFS binary image and recompiled firmware with [SUCCESS].
- **4-Channel ADS1115 Analog Migration, ACS758 50A Inverter Monitor & INA226 #2 Control/Light Reassignment**:
  - Migrated all 4 analog channels to 16-bit ADS1115 I2C ADC:
    - `Channel A0`: **ZMPT101B #1** (AC Voltage #1)
    - `Channel A1`: **ZMPT101B #2** (AC Voltage #2)
    - `Channel A2`: **ZMCT103C** (AC Current)
    - `Channel A3`: **ACS758 50A** (High-Current Inverter DC Discharge)
  - Created new **`ACS758Sensor`** driver (`src/sensors/acs758_sensor.h`, `src/sensors/acs758_sensor.cpp`) with configurable sensitivity ($40.0\text{ mV/A}$ for 50B / $60.0\text{ mV/A}$ for 50U), auto-zero offset calibration, and moving average filtering.
  - Reassigned **INA226 #2 (`0x45`)** to monitor ESP32, controller, sensors, and 12V lighting auxiliary power consumption before the DC-DC buck converter.
  - Updated `SensorData` struct with `inverter_current`, `inverter_power`, `acs758_adc`, and `HEALTH_ACS758` / `HEALTH_ADS1115` health bits.
  - Updated Web Dashboard (`data/index.html`, `data/script.js`, `data/style.css`, `test/web_preview/index.html`) with 4 distinct Hero Power Rings (AC Turbine Power, MPPT Battery Charging, Inverter Discharge Load, and Control & Lighting Aux Power) and matching metric cards.
  - Rebuilt LittleFS binary filesystem image.
- **INA226 DC Voltage Calibration (0.94707x Multiplier)**:
  - Applied high-precision voltage multiplier factor `INA226_VOLTAGE_CAL` ($0.94707\text{f} = \frac{11.81\text{V}}{12.47\text{V}}$) in `ina226_sensor.cpp` and `config.h`.
  - Verified live on hardware: INA226 #1 now reads **$11.81\text{V}$** (matches physical multimeter exactly) and reports **$0.0\%$ SoC** (matches digital battery tester $\text{SOC}=0\%$).
- **Lakoni Blue Wolf 12V 65Ah (75D23L) Precision SoC & Energy Calibration**:
  - Calibrated exact battery profile to **Lakoni Blue Wolf 12V 65Ah (75D23L, 550 CCA)** with measured **57% SoH / 395 CCA ($R=7.57\text{ m}\Omega$)**.
  - Calibrated real usable battery capacity to **$37.05\text{ Ah}$ / $444.60\text{ Wh}$** ($12\text{V} \times 65\text{Ah} \times 57\%$).
  - Adjusted piecewise linear SoC table cutoff so that resting voltage $\le 11.85\text{V}$ maps precisely to **$0\%$ SoC** (matching user's digital battery tester), $12.25\text{V} \rightarrow 50\%$, and $\ge 12.75\text{V} \rightarrow 100\%$.
  - Updated Web Dashboard metric card to **`Lakoni 65Ah (57% SoH / 37Ah)`**, live serial telemetry, WebSocket JSON stream, and Python logger tools.
- **1-Hour INA226 #1 Live Monitor & CSV Data Logger**: Created `tools/monitor_ina1_1hour.py` to continuously capture real-time telemetry from INA226 #1 (Voltage, Current, Power), calculate live statistical metrics (Min/Max/Avg, accumulated energy in Wh and capacity in mAh), render an in-place console status dashboard, and stream CSV logs directly to `logs/`.
- **LittleFS Filesystem Deployment Fix**: Identified and resolved `UnicodeEncodeError` in Windows PowerShell during `uploadfs` flashing, and fixed LittleFS partition mounting compatibility.

## 2026-07-29

### Fixed (Web Configuration)
- **Configuration Save HTTP 400 Error**: Resolved validation failure on `/api/config` when saving settings with an empty Station Mode (STA) SSID field. Updated `config_manager.cpp` to permit empty/unconfigured `staSsid` strings (as long as `staEnabled` is false), and updated `script.js` (and `docs/script.js`) to conditionally send `staSsid` only when populated.

## 2026-07-26

### Fixed (Rule-of-Five & WiFi Value Copy)
- **MovingAverage Rule-of-Five**: Added `= delete` for copy constructor, copy assignment, move constructor, and move assignment in `filters.h`. The class owns a heap-allocated `float[]` buffer — without these guards, an accidental copy would cause a double-free crash.
- **WiFi Manager const-ref to temporary**: Changed `const SystemConfig&` to `const SystemConfig` (value copy) in both `beginAP()` and `beginAPSTA()` in `wifi_manager.cpp`. `getConfig()` returns by value, so binding a `const&` to the temporary was technically safe but misleading and fragile against future refactors.

### Changed (Sensor Calibration Defaults)
- **Default ADC Mode → External ADS1115**: Changed `useAds1115` default from `false` to `true` in `config_manager.cpp`. New installs now use the external ADS1115 16-bit I2C ADC by default instead of the internal ESP32 12-bit ADC. Existing devices with a saved `/config.json` are unaffected.
- **AC Sensor Calibration (Multimeter Reference)**: Recalculated calibration defaults in `config.h` from multimeter readings. ZMPT101B voltage: `150.0` → `242.0` (was reading ~137V, actual 222V). ZMCT103C current: `5.0` → `0.31` → `0.69` (second pass: was reading ~0.32A, actual 0.71A).
- **RPM Sensor — Period-Based Measurement**: Rewrote `rpm_sensor.cpp`/`.h` from pulse-counting over fixed 100ms windows to period-based measurement (time between the last two pulses). Fixes erratic RPM jumps (0 → 600 → 0) at speeds under 1000 RPM. Added adaptive deceleration: if time since the last pulse exceeds the last inter-pulse period, RPM smoothly decreases in real-time instead of holding a stale value until timeout.

## 2026-07-21

### Added (Hardening Release v1.1.0)
- **CORS Protection**: Removed wildcard CORS headers in web server to restrict API access to the device's origin.
- **Password Audit Logging**: Updated configuration save logs to redact passwords and request bodies, logging only status and flags.
- **Task Stack & Heap Diagnostics**: Added `minHeap`, `sensorStackFree`, and `networkStackFree` high-water mark metrics to `/api/sysinfo` for field diagnostics.
- **Setup-Required Guard**: Blocked `/api/restart` with HTTP 403 when initial setup is incomplete. Added prominent `setupRequired` warning banner on the web dashboard.
- **AC Display Limits**: Added configurable `maxAcV` (default 250V) and `maxAcA` (default 30A) fields to `SystemConfig` and web settings, decoupling AC display ranges from DC limits.
- **LCD Memory Optimization**: Replaced `String` concatenation in `LcdDisplay` with fixed 17-byte `char` buffers to eliminate heap fragmentation in the 10Hz measurement task.
- **Sensor Health Indicators**: Integrated health bitmask badge indicators into dashboard metric cards, rendering warning badges on sensor failure instead of misleading numeric zeros.
- **Inline Settings Validation**: Added client-side form validation matching firmware constraints before settings submission.
- **Estimated Power Labeling**: Added "(Est.)" indicator label to AC Power hero card to reflect calculation via RMS voltage, current, and configurable power factor.
- **Code Quality & Bug Fixes**:
  - Fixed variable shadowing bug in `ConfigManager::load()` where `DeserializationError` and `String` both used `error`.
  - Fixed format string null byte bug in `lcd_display.cpp` rotor speed line.
  - Converted health bitmask flags in `/api/data` JSON output to explicit booleans.
- **ESP32 Internal ADC Fix (eFuse Vref Calibration)**: Replaced raw `analogRead()` in `ZMPT101B` and `ZMCT103C` with `analogReadMilliVolts()`, utilizing ESP32 factory eFuse calibration curves to eliminate internal ADC non-linearity and Vref voltage variations.
- **External ADS1115 16-Bit I2C ADC Support**: Added `robtillaart/ADS1X15 @ ^0.4.2` library dependency, new `ADS1115Sensor` driver wrapper, and configurable `useAds1115` & `adsAddr` options to `SystemConfig`. AC sensors automatically route through ADS1115 channels A0 (AC Voltage 1), A1 (AC Voltage 2), and A2 (AC Current) with 16-bit precision when enabled.
- **ADC Configuration Settings UI**: Integrated "ADC Mode & Configuration" settings card into the web portal with toggle switch, I2C address selector (`0x48`-`0x4B`), and dynamic ADC mode display in System Information.
- **Standalone Hardware Sensor Test Firmware**: Added `test/hardware_test/` standalone PlatformIO diagnostic project featuring real-time I2C bus scanner, internal eFuse ADC (mV), 16-bit ADS1115 ADC (mV), INA226 voltage/current/power, DS18B20 temperatures, CPU die temp, RPM pulse counter, and 16x2 LCD status output with 1Hz serial report generation.
- **Dedicated Hardware Test Serial Dashboard Reader**: Created `tools/serial_logger/hardware_test_reader.py` with automatic port selection, session log file archiving to `tools/serial_logger/logs/`, and an in-place redrawn live console dashboard UI (matching `serial_reader.py`) for I2C discovery, internal eFuse ADC (mV), 16-bit ADS1115 (mV), INA226 DC power, DS18B20 temps, and RPM rotor metrics.
- **Floating Pin Noise Floor Clamping**: Increased `ADC_NOISE_FLOOR_MV` to 45.0mV and added minimum cutoff guards (`3.0V` AC and `0.05A` AC) in `ZMPT101B` and `ZMCT103C`. This prevents unconnected/floating ESP32 analog input pins (which float at ~1.65V with ambient EM noise) from producing fluctuating false AC voltage and current readings.
- **ESP32 Silicon eFuse Linearity & Auto Zero-Point Calibration System**: Integrated factory eFuse Vref/linearity lookup tables into `ZMPT101B` and `ZMCT103C`, added 1000-sample `calibrateZeroOffset()` routine to measure hardware baseline DC midpoint voltages, exposed `POST /api/adc-calibrate` REST endpoint, and integrated a "Calibrate ADC Zero-Point" button on the web portal to save zero-point offsets directly to LittleFS (`/config.json`).

## 2026-07-24

### Added (Diagnostic Tools & Web Serial)
- **IR RPM Sensor Signal Edge & Debounce Update**: Changed RPM hardware interrupt trigger edge from `RISING` to `FALLING` and removed software debounce filtering in both main firmware (`src/sensors/rpm_sensor.cpp`) and standalone hardware diagnostic test (`test/hardware_test/src/main.cpp`).
- **Dual-Window Serial Dashboard & Waveform Plotter (`serial_logger`)**: Updated `serial_logger.py` to automatically open **2 separate console windows** upon launch (Window 1: Serial Dashboard Table, Window 2: Live Raw Sensor Waveform Plotter). Utilizes a background local TCP socket server (`127.0.0.1:8888`) to broadcast serial data lines from the master serial connection to the secondary plotter window without COM port lock errors. Integrated with `tools/test_hardware.py` and `tools/erase_and_monitor.py`.
- **MAX9814 Microphone Sound Level Sensor Support (GPIO 33)**: Added MAX9814 electret microphone sampling (bias mV + 25ms Vpp sound amplitude level measurement) on GPIO 33 in `test/hardware_test/src/main.cpp`, with display in both Dashboard and Plotter windows in `serial_logger.py`.
- **Browser-Native Web Serial Logger & Plotter Web App**: Created `tools/web_serial_plotter/index.html` (reference `serialplotter.io`) leveraging browser-native Web Serial API (`navigator.serial`). Features real-time 60fps HTML5 Canvas waveform plotting, live sensor metrics cards, terminal console, CSV data export, zero-point calibration triggers, and launcher script `tools/open_web_plotter.py`. Also deployed to `data/web_serial_plotter.html` for ESP32 LittleFS web serving.

## 2026-07-19

### Fixed (Early-Boot & Serialization)
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

### Added (Sensor & Animation Subsystems)
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

### Added (LCD & Frontend Capabilities)
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

### Architecture Notes (2026-07-17)
- Settings updates are stored in LittleFS config file `/config.json` and applied automatically on reboot.
- Consolidated all I2C accesses on Core 1 to avoid multi-threaded bus conflicts.

## 2026-07-15

### Added (Initial System Architecture)
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

### Baseline Architecture Notes (2026-07-15)
- AC power calculated as: P = Vrms × Irms × PF (configurable)
- ZMPT101B #1 used for power calculation, #2 for raw monitoring
- All analog sensors on ADC1 pins (ADC2 unavailable with WiFi)
- DS18B20 uses async conversion (non-blocking)
- Added pyserial-based port selector script for easy desktop diagnostics
- Added PowerShell and Command Prompt scripts to erase flash, build/upload firmware, and uploadfs sequentially
