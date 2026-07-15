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

// --- ZMPT101B AC Voltage Sensors (ADC1 only) ---
#define PIN_ZMPT101B_1      34      // ADC1_CH6 — AC Voltage (for power calc)
#define PIN_ZMPT101B_2      35      // ADC1_CH7 — AC Voltage (raw monitoring only)

// --- ZMCT103C AC Current Sensor ---
#define PIN_ZMCT103C        32      // ADC1_CH4 — AC Current

// --- RPM Sensor (interrupt-capable) ---
#define PIN_RPM_INPUT       27      // IR sensor input

// --- I2C Bus (shared by INA226 x2) ---
#define PIN_I2C_SDA         21
#define PIN_I2C_SCL         22

// --- INA226 I2C Addresses ---
#define INA226_ADDR_1       0x40    // INA226 #1 (A0=GND, A1=GND)
#define INA226_ADDR_2       0x41    // INA226 #2 (A0=VS,  A1=GND)

// --- DS18B20 OneWire Bus ---
#define PIN_DS18B20         4       // Both sensors on same bus
