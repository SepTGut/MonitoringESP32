// =============================================================
//  ina226_sensor.cpp — INA226 DC Power Sensor Driver
// =============================================================

#include "ina226_sensor.h"
#include "../config/config.h"

INA226Sensor::INA226Sensor(uint8_t address, uint8_t sda, uint8_t scl)
    : _ina(address), _address(address), _sda(sda), _scl(scl), _enabled(false) {
}

bool INA226Sensor::begin() {
    // Pin guard: skip init if address or pins are invalid
    if (_address == 0 || _address == 255 || _sda >= 40 || _scl >= 40) {
        Serial.println("[INA226] Disabled — invalid address or pins");
        _enabled = false;
        return false;
    }

    // Note: Wire.begin() should be called ONCE before this in the task
    if (!_ina.begin()) {
        Serial.printf("[INA226] Failed to connect at address 0x%02X\n", _address);
        _enabled = false;
        return false;
    }

    _ina.setMaxCurrentShunt(INA226_MAX_CURRENT, INA226_SHUNT_OHM);
    _enabled = true;

    Serial.printf("[INA226] Initialized at address 0x%02X (max %.1fA, %.3fΩ shunt)\n",
                  _address, INA226_MAX_CURRENT, INA226_SHUNT_OHM);
    return true;
}

float INA226Sensor::readVoltage() {
    if (!_enabled) return 0.0f;
    return _ina.getBusVoltage();
}

float INA226Sensor::readCurrent() {
    if (!_enabled) return 0.0f;
    return _ina.getCurrent_mA() / 1000.0f;  // mA → A
}

float INA226Sensor::readPower() {
    if (!_enabled) return 0.0f;
    return _ina.getPower_mW() / 1000.0f;    // mW → W
}
