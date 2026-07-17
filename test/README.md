# ESP32 Wind Generator Monitor — Test & Preview Utilities

This directory contains standalone utilities and local files to test, preview, and simulate the wind generator system without requiring active hardware.

## 1. Web Dashboard Preview
The `web_preview/` folder contains a copy of the glassmorphic web dashboard configured to run automatically in **Demo Mode** when opened in a web browser.

### How to Open:
1. **Direct File Open**:
   Simply double-click the [index.html](file:///d:/MyCode/MonitoringESP32/test/web_preview/index.html) file inside `test/web_preview/` to open it in your default browser. It will load with simulated metrics automatically.
   
2. **Local HTTP Server (Recommended)**:
   To simulate a real web server environment, open a terminal in this directory and launch Python's built-in HTTP server:
   ```bash
   python -m http.server 8000
   ```
   Then open your browser and navigate to:
   [http://localhost:8000/web_preview/](http://localhost:8000/web_preview/)

---

## 2. Test Features
* **Settings & Calibration Form**: Test modifying settings values, toggling network checkboxes, or changing sensor I2C assignment values. Saving settings will persist configuration parameters locally to mock storage.
* **WebSocket Simulation**: Simulates dynamic AC/DC voltages, current readings, temperatures, and rotor RPM telemetry in real-time.
