// =============================================================
//  rpm_sensor.cpp — Interrupt-Based RPM Sensor Driver
//
//  Uses period-based measurement: RPM is calculated from the
//  time interval between the two most recent pulses. This gives
//  smooth, accurate readings across the full RPM range —
//  especially at low speeds where pulse-counting over fixed
//  windows produces erratic jumps (0 → 600 → 0 RPM).
//
//  Adaptive timeout: if the time since the last pulse exceeds
//  the last measured period, the RPM is recalculated downward
//  in real-time, providing smooth deceleration instead of a
//  sudden drop to zero.
// =============================================================

#include "rpm_sensor.h"
#include "../config/config.h"

// Static volatile variables shared with ISR
volatile uint32_t RPMSensor::_pulseCount = 0;
volatile uint32_t RPMSensor::_lastPulseTime = 0;
volatile uint32_t RPMSensor::_prevPulseTime = 0;
static portMUX_TYPE rpmMux = portMUX_INITIALIZER_UNLOCKED;

// Minimum refractory period (µs) to reject optical/switch noise spikes (1500 µs = max 40,000 RPM)
static const uint32_t RPM_DEBOUNCE_US = 1500;

void IRAM_ATTR RPMSensor::handleInterrupt() {
    uint32_t now = micros();
    portENTER_CRITICAL_ISR(&rpmMux);
    if (_lastPulseTime == 0 || (now - _lastPulseTime) >= RPM_DEBOUNCE_US) {
        _prevPulseTime = _lastPulseTime;
        _lastPulseTime = now;
        _pulseCount = _pulseCount + 1;
    }
    portEXIT_CRITICAL_ISR(&rpmMux);
}

RPMSensor::RPMSensor(uint8_t pin)
    : _pin(pin) {
}

void RPMSensor::begin() {
    if (_pin >= 40) {
        Serial.println("[RPM] Disabled — invalid pin");
        return;
    }

    pinMode(_pin, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(_pin), handleInterrupt, FALLING);

    _pulseCount = 0;
    _lastPulseTime = 0;
    _prevPulseTime = 0;

    Serial.printf("[RPM] Initialized on GPIO %d (IR sensor, %d pulse/rev)\n",
                  _pin, RPM_PULSES_PER_REV);
}

float RPMSensor::getRPM() {
    if (_pin >= 40) return 0.0f;

    uint32_t now = micros();
    uint32_t lastPulse, prevPulse;

    portENTER_CRITICAL(&rpmMux);
    lastPulse = _lastPulseTime;
    prevPulse = _prevPulseTime;
    portEXIT_CRITICAL(&rpmMux);

    // No pulses yet — still stopped
    if (lastPulse == 0 || prevPulse == 0) {
        return 0.0f;
    }

    // Period between the last two pulses (µs)
    uint32_t lastPeriod = lastPulse - prevPulse;
    if (lastPeriod == 0) return 0.0f;

    // Time elapsed since the most recent pulse (µs)
    uint32_t elapsed = now - lastPulse;

    // Hard timeout — fully stopped
    if (elapsed / 1000 > RPM_TIMEOUT_MS) {
        return 0.0f;
    }

    // Adaptive deceleration: if more time has passed since the last
    // pulse than the last inter-pulse period, use elapsed time as the
    // effective period. This smoothly reduces RPM during deceleration
    // instead of holding a stale value until timeout.
    uint32_t effectivePeriod = (elapsed > lastPeriod) ? elapsed : lastPeriod;

    // RPM = 60,000,000 µs/min ÷ period_µs ÷ pulses_per_rev
    float rpm = 60000000.0f / (float)effectivePeriod / (float)RPM_PULSES_PER_REV;

    return rpm;
}
