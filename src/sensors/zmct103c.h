#pragma once
// =============================================================
//  zmct103c.h — ZMCT103C AC Current Transformer Driver
//  Method: ADC sampling → RMS calculation
//  Combined with ZMPT101B #1 for AC power estimation
// =============================================================

#include <Arduino.h>
#include "../utils/filters.h"

class ZMCT103C {
public:
    ZMCT103C(uint8_t pin, float calibration = 1.0f);

    void begin();

    // Read RMS current (processed with calibration + filter)
    float readRMSCurrent();

    // Read raw ADC value (for diagnostics)
    float readRawADC();

    // Set calibration multiplier at runtime
    void setCalibration(float calibration) { _calibration = calibration; }

private:
    uint8_t       _pin;
    float         _calibration;
    float         _offset;          // DC midpoint (auto-calibrated)
    float         _lastRawAdc;
    MovingAverage _filter;

    float calculateRMS();
};
