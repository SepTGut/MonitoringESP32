// =============================================================
//  rpm_sensor.cpp — Interrupt-Based RPM Sensor Driver
// =============================================================

#include "rpm_sensor.h"
#include "../config/config.h"

// Static volatile variables shared with ISR
volatile uint32_t RPMSensor::_pulseCount = 0;
volatile uint32_t RPMSensor::_lastPulseTime = 0;

void IRAM_ATTR RPMSensor::handleInterrupt() {
    // Software debounce: ignore pulses too close together
    uint32_t now = micros();
    if (now - _lastPulseTime >= RPM_MIN_INTERVAL_US) {
        _pulseCount++;
        _lastPulseTime = now;
    }
}

RPMSensor::RPMSensor(uint8_t pin)
    : _pin(pin), _lastCalcTime(0), _lastPulseCount(0) {
}

void RPMSensor::begin() {
    if (_pin >= 40) {
        Serial.println("[RPM] Disabled — invalid pin");
        return;
    }

    pinMode(_pin, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(_pin), handleInterrupt, RISING);

    _pulseCount = 0;
    _lastPulseCount = 0;
    _lastCalcTime = millis();
    _lastPulseTime = 0;

    Serial.printf("[RPM] Initialized on GPIO %d (IR sensor, %d pulse/rev)\n",
                  _pin, RPM_PULSES_PER_REV);
}

float RPMSensor::getRPM() {
    if (_pin >= 40) return 0.0f;

    uint32_t currentMillis = millis();
    uint32_t currentPulses = _pulseCount;

    // Protection: avoid calculation if interval is too short
    uint32_t timeDelta = currentMillis - _lastCalcTime;
    if (timeDelta < 10) return 0.0f;

    uint32_t pulses = currentPulses - _lastPulseCount;

    // RPM = (pulses / time_seconds) × 60 / pulses_per_rev
    float rpm = ((float)pulses / (float)timeDelta) * 60000.0f / (float)RPM_PULSES_PER_REV;

    // Store for next calculation
    _lastCalcTime = currentMillis;
    _lastPulseCount = currentPulses;

    // Zero out RPM if no pulses received for timeout period
    uint32_t timeSinceLastPulse = (micros() - _lastPulseTime) / 1000;
    if (timeSinceLastPulse > RPM_TIMEOUT_MS) {
        rpm = 0.0f;
    }

    return rpm;
}
