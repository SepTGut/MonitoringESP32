#pragma once
// =============================================================
//  ds18b20_sensor.h — DS18B20 Temperature Sensor Driver
//  Protocol: OneWire (multiple sensors on single bus)
// =============================================================

#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>

class DS18B20Sensor {
public:
    DS18B20Sensor(uint8_t pin);

    void begin();

    // Request async temperature conversion (non-blocking)
    void requestTemperature();

    // Read temperature by bus index (call after conversion completes)
    float readTemperature(uint8_t index = 0);

    // Number of sensors detected on the bus
    uint8_t getDeviceCount();

private:
    uint8_t           _pin;
    OneWire           _oneWire;
    DallasTemperature _sensors;
};
