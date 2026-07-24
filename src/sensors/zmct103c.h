#pragma once
// =============================================================
//  zmct103c.h — ZMCT103C AC Current Transformer Driver
//  Method: ADC sampling → RMS calculation
//  Combined with ZMPT101B #1 for AC power estimation
// =============================================================

#include <Arduino.h>
#include "../utils/filters.h"

class ADS1115Sensor;

class ZMCT103C {
public:
    ZMCT103C(uint8_t pin, float calibration = 1.0f);

    void begin();

    // Read RMS current (processed with calibration + filter)
    // Optional ads pointer + adsChannel allows reading from ADS1115 (16-bit I2C ADC)
    float readRMSCurrent(ADS1115Sensor* ads = nullptr, int8_t adsChannel = -1);

    // Read raw ADC value (for diagnostics)
    float readRawADC();

    // Set calibration multiplier at runtime
    void setCalibration(float calibration) { _calibration = calibration; }

    // Set and get zero-point DC midpoint offset in mV
    void setOffset(float offsetMv) { if (offsetMv >= 500.0f && offsetMv <= 3000.0f) _offset = offsetMv; }
    float getOffset() const { return _offset; }

    // Auto-calibrate zero-point baseline DC midpoint offset (samples 1000 readings at 0 AAC)
    float calibrateZeroOffset(ADS1115Sensor* ads = nullptr, int8_t adsChannel = -1);

private:
    uint8_t       _pin;
    float         _calibration;
    float         _offset;          // DC midpoint in mV (auto-calibrated)
    float         _lastRawAdc;
    MovingAverage _filter;

    float calculateRMS(ADS1115Sensor* ads, int8_t adsChannel);
};
