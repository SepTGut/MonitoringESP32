#pragma once
// =============================================================
//  rpm_sensor.h — Interrupt-Based RPM Sensor Driver
//  Method: Pulse counting with hardware interrupt
//  Formula: RPM = (pulse_count / time_seconds) × 60
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
    uint32_t _lastCalcTime;
    uint32_t _lastPulseCount;

    // Shared volatile counters accessed from ISR
    static volatile uint32_t _pulseCount;
    static volatile uint32_t _lastPulseTime;
};
