#pragma once
// =============================================================
//  config.h — System Configuration & Calibration Values
//  ESP32 Wind Generator Monitoring System
// =============================================================

#include <Arduino.h>

// --- FIRMWARE VERSION ---
#define FW_VERSION          "v1.0.0"

// --- WiFi Configuration ---
#define WIFI_AP_SSID        "ESP32-WIND-MONITOR"
#define WIFI_AP_PASS        "12345678"

// --- Timing Configuration ---
#define SENSOR_POLL_MS      100     // Sensor read interval (10 Hz)
#define WEBSOCKET_PUSH_MS   500     // Dashboard update interval (2 Hz)
#define SERIAL_LOG_MS       1000    // Serial print interval (1 Hz)

// --- ADC Sampling ---
#define ADC_SAMPLES         500     // Samples per RMS calculation window
#define ADC_SAMPLE_WINDOW   25      // Sampling window in ms (≥1 full 50Hz cycle)

// --- ZMPT101B Calibration ---
//  Adjust these based on known reference voltage measurements.
//  Formula: Vrms = raw_rms × calibration_factor
#define ZMPT_CALIBRATION_1  150.0f  // ZMPT101B #1 (for power calculation)
#define ZMPT_CALIBRATION_2  150.0f  // ZMPT101B #2 (raw monitoring)

// --- ZMCT103C Calibration ---
//  Depends on burden resistor and CT turns ratio.
//  ZMCT103C typical: 1000:1 turns ratio
//  Formula: Irms = raw_rms × calibration_factor
#define ZMCT_CALIBRATION    5.0f

// --- AC Power Factor ---
//  Used for estimated real power: P = Vrms × Irms × PF
#define AC_POWER_FACTOR     0.85f

// --- INA226 Configuration ---
#define INA226_MAX_CURRENT  10.0f   // Maximum expected current (A)
#define INA226_SHUNT_OHM    0.01f   // Shunt resistor value (Ω)

// --- RPM Configuration ---
#define RPM_PULSES_PER_REV  1       // Pulses per revolution (IR sensor)
#define RPM_TIMEOUT_MS      1000    // Zero RPM if no pulse for this duration
#define RPM_MIN_INTERVAL_US 5000    // Debounce: minimum µs between pulses

// --- Filter Configuration ---
#define FILTER_WINDOW_SIZE  10      // Moving average window size

// --- Display Limits (for progress bars) ---
#define DEFAULT_MAX_V       80.0f   // Max voltage display scale
#define DEFAULT_MAX_A       30.0f   // Max current display scale
#define DEFAULT_MAX_RPM     3000    // Max RPM display scale
#define DEFAULT_MAX_TEMP    100     // Max temperature display scale

// --- Feature Flags ---
#define ENABLE_SERIAL_LOG   1       // Enable serial monitor output
// #define ENABLE_MQTT              // Uncomment to enable MQTT telemetry
