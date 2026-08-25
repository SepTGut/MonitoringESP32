#pragma once
// =============================================================
//  pin_config.h — GPIO Pin Assignments
//  ESP32-WROOM-32D / DevKit V4
//
//  IMPORTANT: All analog sensors MUST use ADC1 pins.
//  ADC2 is disabled when WiFi is active.
//
//  ADC1 channels: GPIO 32, 33, 34, 35, 36, 39
// =============================================================

#include <Arduino.h>

// --- ADS1115 16-Bit I2C ADC (Channels A0..A3) ---
// All analog sensors are sampled via ADS1115 (16-bit high-precision I2C ADC)
#define ADS1115_CH_ZMPT1      0 // Channel A0 — ZMPT101B #1 AC Voltage
#define ADS1115_CH_ZMPT2      1 // Channel A1 — ZMPT101B #2 AC Voltage
#define ADS1115_CH_ZMCT       2 // Channel A2 — ZMCT103C AC Current
#define ADS1115_CH_ACS758     3 // Channel A3 — ACS758 50A Inverter DC Current

// --- Fallback Internal ADC1 Pins (Used only if ADS1115 is absent) ---
#define PIN_ZMPT101B_1        34 // ADC1_CH6 — AC Voltage #1 (fallback)
#define PIN_ZMPT101B_2        35 // ADC1_CH7 — AC Voltage #2 (fallback)
#define PIN_ZMCT103C          32 // ADC1_CH4 — AC Current (fallback)
#define PIN_ACS758_INPUT      33 // ADC1_CH5 — ACS758 Inverter Current (fallback)

// --- RPM Sensor (interrupt-capable) ---
#define PIN_RPM_INPUT         27 // IR / Proximity sensor input

// --- I2C Bus (shared by ADS1115, INA226 x2, LCD) ---
#define PIN_I2C_SDA           21
#define PIN_I2C_SCL           22

// --- ADS1115 Hardware ALERT/RDY Pin (Optional hardware interrupt/ready input) ---
// Connecting ADS1115 ALRT pin to GPIO 19 allows zero-wait hardware conversion synchronization
#define PIN_ADS1115_ALERT     19 // ADS1115 ALERT/RDY pin (Active-LOW, pullup enabled)

// --- I2C Device Addresses ---
// ADS1115 ADDR Pin Hardware Options:
//   ADDR -> GND = 0x48 (Default)
//   ADDR -> VDD = 0x49
//   ADDR -> SDA = 0x4A
//   ADDR -> SCL = 0x4B
#define ADS1115_I2C_ADDR      0x48 // ADS1115 ADC (Default: ADDR -> GND)
#define ADS1115_ADDR_GND      0x48 // ADDR pin -> GND
#define ADS1115_ADDR_VDD      0x49 // ADDR pin -> VDD
#define ADS1115_ADDR_SDA      0x4A // ADDR pin -> SDA
#define ADS1115_ADDR_SCL      0x4B // ADDR pin -> SCL

#define INA226_ADDR_1         0x44 // INA226 #1: Battery / MPPT Charging (A0=GND, A1=GND)
#define INA226_ADDR_2         0x45 // INA226 #2: ESP32, Control & 12V Lighting Aux Power (A0=VS, A1=GND)
#define LCD_I2C_ADDR          0x27 // PCF8574 I2C LCD Display

// --- DS18B20 OneWire Bus ---
#define PIN_DS18B20           4  // Both temperature sensors on same bus
