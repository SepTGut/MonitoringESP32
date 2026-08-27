#pragma once
// =============================================================
//  config.h — System Configuration & Calibration Values
//  ESP32 Wind Generator Monitoring System
// =============================================================

#include <Arduino.h>

// --- FIRMWARE VERSION ---
#define FW_VERSION          "v1.1.0"

// --- WiFi Configuration ---
#define WIFI_AP_SSID        "ESP32-WIND-MONITOR"
#define WIFI_AP_PASS        "12345678"

// --- Timing Configuration ---
#define SENSOR_POLL_MS      100     // Sensor read interval (10 Hz)
#define WEBSOCKET_PUSH_MS   500     // Dashboard update interval (2 Hz)
#define SERIAL_LOG_MS       1000    // Serial print interval (1 Hz)
#define LCD_ROTATION_MS     3000    // LCD screen rotation rate (ms)


// --- ADC Sampling ---
#define ADC_SAMPLES         500     // Samples per RMS calculation window
#define ADC_SAMPLE_WINDOW   25      // Sampling window in ms (≥1 full 50Hz cycle)
// NOTE: ADC_SAMPLE_WINDOW must be ≥20ms to capture one complete 50Hz AC cycle.
// The sample-count cap (ADC_SAMPLES) must not terminate the window before
// 20ms has elapsed. At typical ESP32 ADC throughput (~100kHz in Arduino
// analogRead mode), 500 samples take ~5ms, so the window timer dominates.
// If ADC throughput increases (e.g. DMA), verify the window is still met.

// --- ZMPT101B Calibration ---
//  Adjust these based on known reference voltage measurements.
//  Formula: Vrms = raw_rms × calibration_factor
#define ZMPT_CALIBRATION_1  242.0f  // ZMPT101B #1 (Generator AC Voltage)
#define ZMPT_CALIBRATION_2  327.8f  // ZMPT101B #2 (Inverter 220V AC Output)

// --- ZMCT103C Calibration ---
//  Depends on burden resistor and CT turns ratio.
//  ZMCT103C typical: 1000:1 turns ratio
#define ZMCT_CALIBRATION    0.2207f // Calibrated AC current scale factor (0.134A multimeter / 607mV raw)

// --- AC Power Factor ---
//  Used for estimated real power: P = Vrms × Irms × PF
#define AC_POWER_FACTOR     0.85f

// --- INA226 Configuration ---
#define INA226_MAX_CURRENT  0.80f   // Maximum expected current (A) for R100 shunt (81.92mV limit)
#define INA226_SHUNT_OHM    0.10f   // Shunt resistor value (Ω) — R100 = 0.100Ω (100mΩ)
#define INA226_VOLTAGE_CAL  0.94551f // DC voltage calibration multiplier (12.10V multimeter / 12.12V raw)

// --- I2C Bus Configuration ---
#define I2C_CLOCK_SPEED     100000UL // Standard-mode I2C clock (100 kHz) for PCF8574 LCD compatibility

// --- ADS1115 Configuration ---
#define DEFAULT_ADS1115_ADDR 0x48   // Default ADS1115 I2C Address (ADDR -> GND)
#define ADS1115_USE_ALERT    true   // Enable hardware ALERT/RDY pin synchronization if wired
#define ADS1115_DATA_RATE    7      // 7 = 860 SPS (Fastest conversion rate)

// --- ACS758 50A Inverter Current Sensor Configuration ---
#define ACS758_SENSITIVITY      40.0f   // mV/A (40.0 mV/A for ACS758LCB-050B bidirectional, 60.0 for 050U)
#define ACS758_CAL_MULTIPLIER   0.9825f // Calibrated scale factor for direct GPIO 33 (3.93A multimeter / 160mV raw delta)
#define ACS758_ZERO_OFFSET      1675.0f // Resting zero-current baseline in mV on GPIO 33
#define ACS758_MAX_CURRENT      50.0f   // Maximum current (A) for 50A variant

// --- RPM Configuration ---
#define RPM_PULSES_PER_REV  1       // Pulses per revolution (IR sensor)
#define RPM_TIMEOUT_MS      1000    // Zero RPM if no pulse for this duration
#define RPM_MIN_INTERVAL_US 5000    // Debounce: minimum µs between pulses

// --- Filter Configuration ---
#define FILTER_WINDOW_SIZE  10      // Moving average window size

// --- Display Limits (for progress bars) ---
// DC limits (INA226 channels)
#define DEFAULT_MAX_V       80.0f   // Max DC voltage display scale
#define DEFAULT_MAX_A       30.0f   // Max DC current display scale
// AC limits (ZMPT101B / ZMCT103C channels)
#define DEFAULT_MAX_AC_V    250.0f  // Max AC voltage display scale
#define DEFAULT_MAX_AC_A    30.0f   // Max AC current display scale
// General
#define DEFAULT_MAX_RPM     3000    // Max RPM display scale
#define DEFAULT_MAX_TEMP    100     // Max temperature display scale

// --- Power Management & Deep Sleep Configuration ---
#define ENABLE_POWER_SWITCH   true    // Enable power switch deep sleep monitoring
#define POWER_SWITCH_TIMEOUT  10000   // Milliseconds LOW before entering Deep Sleep (default 10s)

// --- Feature Flags ---
#define ENABLE_SERIAL_LOG   1       // Enable serial monitor output
// #define ENABLE_MQTT              // Uncomment to enable MQTT telemetry

