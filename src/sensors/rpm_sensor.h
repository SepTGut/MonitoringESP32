#pragma once
// =============================================================
//  rpm_sensor.h — Interrupt-Based RPM Sensor Driver
//  Method: Period-based measurement (time between pulses)
//  Accurate across full RPM range, especially at low speeds.
// =============================================================

#include <Arduino.h>

class RPMSensor {
public:
    RPMSensor(uint8_t pin);

    void begin();
    float getRPM();

    // ISR handler — must be static for attachInterrupt
    static void IRAM_ATTR handleInterrupt();

private:
    uint8_t  _pin;

    // Shared volatile state accessed from ISR
    static volatile uint32_t _pulseCount;
    static volatile uint32_t _lastPulseTime;
    static volatile uint32_t _prevPulseTime;
};
