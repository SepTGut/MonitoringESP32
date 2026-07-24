// =============================================================
//  ads1115_sensor.cpp — ADS1115 16-Bit I2C ADC Driver
// =============================================================

#include "ads1115_sensor.h"

ADS1115Sensor::ADS1115Sensor(uint8_t address)
    : _ads(address), _address(address), _enabled(false) {
}

bool ADS1115Sensor::begin() {
    if (_address < 0x48 || _address > 0x4B) {
        Serial.println("[ADS1115] Disabled — invalid address");
        _enabled = false;
        return false;
    }

    if (!_ads.begin()) {
        Serial.printf("[ADS1115] Failed to connect at address 0x%02X\n", _address);
        _enabled = false;
        return false;
    }

    // Set gain to 1 (+/- 4.096V range, 1 LSB = 0.125mV = 0.000125V)
    _ads.setGain(1);
    // Set data rate to 860 SPS for fast sampling
    _ads.setDataRate(7);

    _enabled = true;
    Serial.printf("[ADS1115] Initialized at address 0x%02X (16-bit 860SPS +/-4.096V)\n", _address);
    return true;
}

float ADS1115Sensor::readMilliVolts(uint8_t channel) {
    if (!_enabled || channel > 3) return 0.0f;
    int16_t raw = _ads.readADC(channel);
    // 1 LSB = 0.125 mV (gain 1)
    return raw * 0.125f;
}

int16_t ADS1115Sensor::readRaw(uint8_t channel) {
    if (!_enabled || channel > 3) return 0;
    return _ads.readADC(channel);
}
