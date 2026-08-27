#pragma once
// =============================================================
//  acs758_sensor.h — ACS758 50A Hall-Effect DC Current Sensor
//  Position: After Accu, before Inverter (Inverter discharge line)
// =============================================================

#include <Arduino.h>
#include "../utils/filters.h"

class ADS1115Sensor;

class ACS758Sensor {
public:
    // sensitivityMvPerA: 40.0 mV/A for 50B (bidirectional 5V), 60.0 mV/A for 50U (unidirectional 5V)
    ACS758Sensor(uint8_t fallbackPin = 33, float sensitivityMvPerA = 40.0f, float calMultiplier = 3.1714f);

    void begin();

    // Read DC current in Amperes via ADS1115 (channel 3) or fallback analog pin
    float readCurrent(ADS1115Sensor* ads = nullptr, int8_t adsChannel = 3);

    // Read raw millivolts measured at sensor pin
    float readRawMilliVolts(ADS1115Sensor* ads = nullptr, int8_t adsChannel = 3);

    // Auto-calibrate zero-current baseline offset at idle (samples 500 times)
    float calibrateZeroOffset(ADS1115Sensor* ads = nullptr, int8_t adsChannel = 3);

    void setSensitivity(float sensitivityMvPerA) { _sensitivity = sensitivityMvPerA; }
    float getSensitivity() const { return _sensitivity; }

    void setZeroOffset(float zeroMv) { _zeroOffset = zeroMv; }
    float getZeroOffset() const { return _zeroOffset; }

    void setCalMultiplier(float cal) { _calMultiplier = cal; }
    float getCalMultiplier() const { return _calMultiplier; }

private:
    uint8_t       _pin;
    float         _sensitivity;     // mV per Ampere (e.g. 40.0 mV/A for 50B)
    float         _calMultiplier;   // Scaling calibration factor (e.g. 3.1714)
    float         _zeroOffset;      // Resting zero-current output in mV (~2500mV for 5V 50B, ~1650mV for 3.3V)
    MovingAverage _filter;
};
