# Development Log

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


