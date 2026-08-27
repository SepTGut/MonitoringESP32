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

    int calErr = _ina.setMaxCurrentShunt(INA226_MAX_CURRENT, INA226_SHUNT_OHM);
    if (calErr != INA226_ERR_NONE && calErr != 0) {
        Serial.printf("[INA226] Warning: setMaxCurrentShunt returned %d, using direct physical shunt calculation\n", calErr);
    }
    _enabled = true;

    Serial.printf("[INA226] Initialized at address 0x%02X (max %.2fA, %.3fΩ shunt)\n",
                  _address, INA226_MAX_CURRENT, INA226_SHUNT_OHM);
    return true;
}

float INA226Sensor::readVoltage() {
    if (!_enabled) return 0.0f;
    float v = _ina.getBusVoltage() * INA226_VOLTAGE_CAL;
    if (isnan(v) || isinf(v) || v < 0.0f) return 0.0f;
    return v;
}

float INA226Sensor::readCurrent() {
    if (!_enabled) return 0.0f;
    // Calculate current directly from physical shunt voltage drop (I = V_shunt / R_shunt)
    // getShuntVoltage_mV() returns mV -> convert to Volts, then divide by shunt resistance (Ohms)
    float shunt_mV = _ina.getShuntVoltage_mV();
    if (isnan(shunt_mV) || isinf(shunt_mV)) return 0.0f;
    float currentA = (shunt_mV / 1000.0f) / INA226_SHUNT_OHM;  // Returns Amperes
    if (isnan(currentA) || isinf(currentA)) return 0.0f;
    return currentA;
}

float INA226Sensor::readPower() {
    if (!_enabled) return 0.0f;
    float v = readVoltage();
    float a = readCurrent();
    float p = v * fabsf(a);    // Returns Watts
    if (isnan(p) || isinf(p)) return 0.0f;
    return p;
}
