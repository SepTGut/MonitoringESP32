// =============================================================
//  ads1115_sensor.cpp — ADS1115 16-Bit I2C ADC Driver
// =============================================================

#include "ads1115_sensor.h"

ADS1115Sensor::ADS1115Sensor(uint8_t address, int8_t alertPin)
    : _ads(address), _address(address), _alertPin(alertPin), _alertEnabled(false), _enabled(false) {
}

bool ADS1115Sensor::begin(int8_t alertPin) {
    if (alertPin >= 0) {
        _alertPin = alertPin;
    }

    if (_address < 0x48 || _address > 0x4B) {
        Serial.printf("[ADS1115] Invalid configured address 0x%02X, probing default 0x48...\n", _address);
        _address = 0x48;
        _ads.reset();
    }

    // Try initial configured address
    if (!_ads.begin()) {
        Serial.printf("[ADS1115] Not found at address 0x%02X. Auto-scanning ADDR variants (0x48..0x4B)...\n", _address);
        bool found = false;
        const uint8_t possibleAddresses[4] = { 0x48, 0x49, 0x4A, 0x4B };
        for (uint8_t a : possibleAddresses) {
            if (a == _address) continue;
            ADS1115 testAds(a);
            if (testAds.begin()) {
                _address = a;
                _ads = testAds;
                found = true;
                Serial.printf("[ADS1115] Auto-detected active ADS1115 at address 0x%02X!\n", _address);
                break;
            }
        }
        if (!found) {
            Serial.println("[ADS1115] Error: No ADS1115 found on any I2C address (0x48-0x4B). Sensor disabled.");
            _enabled = false;
            return false;
        }
    }

    // Set gain to 1 (+/- 4.096V range, 1 LSB = 0.125mV)
    _ads.setGain(1);
    // Set data rate to 860 SPS for maximum throughput
    _ads.setDataRate(7);

    // Configure hardware ALERT/RDY pin if specified
    if (_alertPin >= 0 && _alertPin < 40) {
        enableAlertReady(_alertPin);
    }

    _enabled = true;
    Serial.printf("[ADS1115] Initialized at address 0x%02X (16-bit 860SPS +/-4.096V, ALRT: %s)\n",
                  _address, _alertEnabled ? "Hardware Pin Active" : "I2C Polling");
    return true;
}

void ADS1115Sensor::enableAlertReady(int8_t alertPin) {
    if (alertPin >= 0 && alertPin < 40) {
        _alertPin = alertPin;
        pinMode(_alertPin, INPUT_PULLUP);

        // Configure ADS1115 threshold registers for Conversion Ready pulse
        // MSB of Hi_thresh = 1 (0x7FFF) and Lo_thresh = 0 (0x8000)
        _ads.setComparatorThresholdLow(0x8000);
        _ads.setComparatorThresholdHigh(0x7FFF);
        _ads.setComparatorQueConvert(0); // Assert alert after 1 conversion
        _ads.setComparatorPolarity(0);   // Active LOW pulse
        _ads.setComparatorLatch(0);      // Non-latching pulse

        _alertEnabled = true;
        Serial.printf("[ADS1115] Hardware ALRT/RDY enabled on GPIO %d (Active-LOW)\n", _alertPin);
    }
}

bool ADS1115Sensor::isConversionReady() {
    if (!_enabled) return false;
    if (_alertEnabled && _alertPin >= 0 && _alertPin < 40) {
        return digitalRead(_alertPin) == LOW;
    }
    return _ads.isReady();
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
