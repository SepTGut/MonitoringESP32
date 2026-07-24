# ESP32 Wind Monitor — Standalone Hardware Sensor Test

This directory contains a simple, standalone diagnostic firmware created specifically to test all physical hardware sensors and output real-time measurements directly to the Serial Monitor at **115200 baud**.

---

## Folder Structure

```
test/
└── hardware_test/
    ├── platformio.ini    # Standalone PlatformIO environment configuration
    ├── README.md         # Hardware testing guide
    └── src/
        └── main.cpp      # Standalone diagnostic test sketch
```

---

## Tested Components & Pin Assignments

| Component / Sensor | Interface / Pin | Description / Expected Address |
|---|---|---|
| **I2C Bus Scanner** | SDA: GPIO 21, SCL: GPIO 22 | Auto-scans all 127 I2C addresses |
| **ZMPT101B #1** | ADC1_CH6 (GPIO 34) | Internal eFuse calibrated ADC (mV) |
| **ZMPT101B #2** | ADC1_CH7 (GPIO 35) | Internal eFuse calibrated ADC (mV) |
| **ZMCT103C** | ADC1_CH4 (GPIO 32) | Internal eFuse calibrated ADC (mV) |
| **ADS1115** | I2C (0x48) | 16-Bit I2C ADC (Channels A0, A1, A2, A3) |
| **INA226 #1** | I2C (0x44 / 0x40) | DC Channel 1 (Voltage, Current, Power) |
| **INA226 #2** | I2C (0x45 / 0x41) | DC Channel 2 (Voltage, Current, Power) |
| **DS18B20** | OneWire (GPIO 4) | Dual external temperature probes |
| **ESP32 CPU** | Internal Sensor | ESP32 CPU die temperature (°C) |
| **RPM Sensor** | GPIO 27 (IR Pulse) | Hardware interrupt pulse counter |
| **I2C LCD 16x2** | I2C (0x27 / 0x3F) | Status LCD display output |

---

## How to Build & Upload

### Option 1: Via PlatformIO CLI

From the project root directory, run:

```bash
# Upload hardware test firmware to ESP32
pio run -d test/hardware_test -t upload

# Open Serial Monitor at 115200 baud
pio device monitor -b 115200
```

### Option 2: Via VS Code / PlatformIO IDE

1. Open `test/hardware_test/platformio.ini` in VS Code.
2. Click **Build** / **Upload** on the PlatformIO status bar.
3. Open Serial Monitor (115200 baud).

---

## Example Serial Diagnostic Output

```text
============================================================
         ESP32 WIND MONITOR — HARDWARE DIAGNOSTIC TEST
============================================================
┌───────────────────────────────────────────────────────────┐
│              HARDWARE SENSOR DIAGNOSTIC REPORT            │
├───────────────────────────────────────────────────────────┤
│ I2C Bus Scan: 0x27 0x44 0x45 0x48                        │
├─ [1] Internal ESP32 ADC (eFuse Vref Calibrated) ─────────┤
│  ZMPT101B #1 (GPIO 34): 1652.3 mV                         │
│  ZMPT101B #2 (GPIO 35): 1648.1 mV                         │
│  ZMCT103C    (GPIO 32): 1650.5 mV                         │
├─ [2] External ADS1115 16-Bit ADC (I2C) ──────────────────┤
│  Channel A0 (ZMPT1): 1651.125 mV                          │
│  Channel A1 (ZMPT2): 1647.875 mV                          │
│  Channel A2 (ZMCT) : 1650.250 mV                          │
│  Channel A3 (Aux)  :  120.000 mV                          │
├─ [3] INA226 DC Power Sensors ─────────────────────────────┤
│  INA226 #1 (0x44): 12.45V |  2.10A |  26.14W [OK]         │
│  INA226 #2 (0x45): 24.10V |  0.85A |  20.48W [OK]         │
├─ [4] Temperature Sensors ─────────────────────────────────┤
│  DS18B20 #1 :  31.5 °C [OK]                               │
│  DS18B20 #2 :  28.2 °C [OK]                               │
│  ESP32 CPU  :  42.8 °C                                    │
├─ [5] RPM Rotor Pulse Counter (GPIO 27) ───────────────────┤
│  Total Pulses: 142      | Speed:  1200 RPM                │
└───────────────────────────────────────────────────────────┘
```
