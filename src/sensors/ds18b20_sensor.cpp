// =============================================================
//  ds18b20_sensor.cpp — DS18B20 Temperature Sensor Driver
// =============================================================

#include "ds18b20_sensor.h"

DS18B20Sensor::DS18B20Sensor(uint8_t pin)
    : _pin(pin), _oneWire(pin), _sensors(&_oneWire) {
}

void DS18B20Sensor::begin() {
    if (_pin >= 40) {
        Serial.println("[DS18B20] Disabled — invalid pin");
        return;
    }

    _sensors.begin();
    // Non-blocking mode: requestTemperatures() returns immediately
    _sensors.setWaitForConversion(false);

    uint8_t count = _sensors.getDeviceCount();
    Serial.printf("[DS18B20] Found %d sensor(s) on pin %d\n", count, _pin);
}

void DS18B20Sensor::requestTemperature() {
    if (_pin >= 40) return;
    _sensors.requestTemperatures();
}

float DS18B20Sensor::readTemperature(uint8_t index) {
    if (_pin >= 40) return 0.0f;

    float temp = _sensors.getTempCByIndex(index);

    // DallasTemperature returns -127°C if sensor is disconnected
    if (temp == DEVICE_DISCONNECTED_C) {
        return 0.0f;
    }
    return temp;
}

uint8_t DS18B20Sensor::getDeviceCount() {
    if (_pin >= 40) return 0;
    return _sensors.getDeviceCount();
}
