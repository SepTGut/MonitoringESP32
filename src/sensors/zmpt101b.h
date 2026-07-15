#pragma once
// =============================================================
//  zmpt101b.h — ZMPT101B AC Voltage Sensor Driver
//  Method: High-speed ADC sampling → RMS calculation
//  Two instances: #1 for power calc, #2 for raw monitoring
// =============================================================

#include <Arduino.h>
#include "../utils/filters.h"

class ZMPT101B {
public:
    ZMPT101B(uint8_t pin, float calibration = 1.0f);

    void begin();

    // Read RMS voltage (processed with calibration + filter)
    float readRMSVoltage();

    // Read raw ADC midpoint value (for diagnostics)
    float readRawADC();

    // Set calibration multiplier at runtime
    void setCalibration(float calibration) { _calibration = calibration; }

private:
    uint8_t       _pin;
    float         _calibration;
    float         _offset;          // DC midpoint (auto-calibrated)
    float         _lastRawAdc;
    MovingAverage _filter;

    // Calculate true RMS from ADC samples
    float calculateRMS();
};
