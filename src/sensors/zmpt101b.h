#pragma once
// =============================================================
//  zmpt101b.h — ZMPT101B AC Voltage Sensor Driver
//  Method: High-speed ADC sampling → RMS calculation
//  Two instances: #1 for power calc, #2 for raw monitoring
// =============================================================

#include <Arduino.h>
#include "../utils/filters.h"

class ADS1115Sensor;

class ZMPT101B {
public:
    ZMPT101B(uint8_t pin, float calibration = 1.0f);

    void begin();

    // Read RMS voltage (processed with calibration + filter)
    // Optional ads pointer + adsChannel allows reading from ADS1115 (16-bit I2C ADC)
    float readRMSVoltage(ADS1115Sensor* ads = nullptr, int8_t adsChannel = -1);

    // Read raw ADC midpoint value (for diagnostics)
    float readRawADC();

    // Set calibration multiplier at runtime
    void setCalibration(float calibration) { _calibration = calibration; }

    // Set and get zero-point DC midpoint offset in mV
    void setOffset(float offsetMv) { if (offsetMv >= 500.0f && offsetMv <= 3000.0f) _offset = offsetMv; }
    float getOffset() const { return _offset; }

    // Auto-calibrate zero-point baseline DC midpoint offset (samples 1000 readings at 0 VAC)
    float calibrateZeroOffset(ADS1115Sensor* ads = nullptr, int8_t adsChannel = -1);

private:
    uint8_t       _pin;
    float         _calibration;
    float         _offset;          // DC midpoint in mV (auto-calibrated)
    float         _lastRawAdc;
    MovingAverage _filter;

    // Calculate true RMS from ADC samples
    float calculateRMS(ADS1115Sensor* ads, int8_t adsChannel);
};
