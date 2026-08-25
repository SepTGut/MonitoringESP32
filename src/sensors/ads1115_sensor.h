#pragma once
// =============================================================
//  ads1115_sensor.h — ADS1115 16-Bit I2C ADC Driver
//  Features:
//    - 16-Bit high precision sampling (860 SPS)
//    - Full ADDR support (0x48, 0x49, 0x4A, 0x4B) with auto-probing
//    - Hardware ALRT (ALERT/RDY) pin conversion synchronization
// =============================================================

#include <Arduino.h>
#include <Wire.h>
#include <ADS1X15.h>

class ADS1115Sensor {
public:
    // address: 0x48 (GND), 0x49 (VDD), 0x4A (SDA), 0x4B (SCL)
    // alertPin: GPIO pin connected to ALRT pin (e.g. GPIO 19), or -1 if unused
    ADS1115Sensor(uint8_t address = 0x48, int8_t alertPin = -1);

    bool begin(int8_t alertPin = -1);
    bool isEnabled() const { return _enabled; }

    // Read voltage on channel (0..3) in millivolts (mV)
    float readMilliVolts(uint8_t channel);

    // Read raw ADC counts on channel (0..3)
    int16_t readRaw(uint8_t channel);

    // Check if conversion is complete (via hardware ALRT pin or I2C status)
    bool isConversionReady();

    // Configure hardware ALERT/RDY pin for conversion ready output
    void enableAlertReady(int8_t alertPin);

    // Getters for diagnostics
    uint8_t getAddress() const { return _address; }
    int8_t  getAlertPin() const { return _alertPin; }
    bool    isAlertEnabled() const { return _alertEnabled; }

private:
    ADS1115  _ads;
    uint8_t  _address;
    int8_t   _alertPin;
    bool     _alertEnabled;
    bool     _enabled;
};
