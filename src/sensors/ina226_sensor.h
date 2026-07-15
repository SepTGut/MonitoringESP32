#pragma once
// =============================================================
//  ina226_sensor.h — INA226 DC Power Sensor Driver
//  Communication: I2C
//  Measures: Bus Voltage, Current, Power
// =============================================================

#include <Arduino.h>
#include <Wire.h>
#include <INA226.h>

class INA226Sensor {
public:
    INA226Sensor(uint8_t address, uint8_t sda, uint8_t scl);

    bool begin();

    float readVoltage();    // Returns V
    float readCurrent();    // Returns A
    float readPower();      // Returns W

    bool isEnabled() const { return _enabled; }

private:
    INA226   _ina;
    uint8_t  _address;
    uint8_t  _sda;
    uint8_t  _scl;
    bool     _enabled;
};
