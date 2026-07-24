#pragma once
// =============================================================
//  ads1115_sensor.h — ADS1115 16-Bit I2C ADC Driver
// =============================================================

#include <Arduino.h>
#include <Wire.h>
#include <ADS1X15.h>

class ADS1115Sensor {
public:
    ADS1115Sensor(uint8_t address = 0x48);

    bool begin();
    bool isEnabled() const { return _enabled; }

    // Read voltage on channel (0..3) in millivolts (mV)
    float readMilliVolts(uint8_t channel);

    // Read raw ADC counts on channel (0..3)
    int16_t readRaw(uint8_t channel);

private:
    ADS1115  _ads;
    uint8_t  _address;
    bool     _enabled;
};
