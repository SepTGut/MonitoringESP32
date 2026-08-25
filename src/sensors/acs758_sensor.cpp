// =============================================================
//  acs758_sensor.cpp — ACS758 50A Hall-Effect DC Current Sensor
// =============================================================

#include "acs758_sensor.h"
#include "ads1115_sensor.h"
#include "../config/config.h"

// Cutoff threshold for zero current dead-band (Amperes)
static const float ACS758_DEADBAND_A = 0.08f;

ACS758Sensor::ACS758Sensor(uint8_t fallbackPin, float sensitivityMvPerA)
    : _pin(fallbackPin),
      _sensitivity(sensitivityMvPerA),
      _zeroOffset(2500.0f),       // Typical 5V bidirectional midpoint (~2.5V)
      _filter(FILTER_WINDOW_SIZE) {
}

void ACS758Sensor::begin() {
    if (_pin < 40) {
        pinMode(_pin, INPUT);
        analogSetAttenuation(ADC_11db);
        analogReadResolution(12);
    }
    Serial.printf("[ACS758] Driver initialized (sensitivity: %.1f mV/A, default zero: %.1f mV)\n",
                  _sensitivity, _zeroOffset);
}

float ACS758Sensor::calibrateZeroOffset(ADS1115Sensor* ads, int8_t adsChannel) {
    const bool useAds = (ads != nullptr && ads->isEnabled() && adsChannel >= 0 && adsChannel <= 3);
    float sum = 0.0f;
    const int count = 500;
    for (int i = 0; i < count; i++) {
        sum += useAds ? ads->readMilliVolts((uint8_t)adsChannel)
                      : (float)analogReadMilliVolts(_pin);
        delayMicroseconds(200);
    }
    _zeroOffset = sum / (float)count;
    Serial.printf("[ACS758] Calibrated zero-current baseline: %.2f mV (%s)\n",
                  _zeroOffset, useAds ? "ADS1115" : "Internal ADC");
    return _zeroOffset;
}

float ACS758Sensor::readRawMilliVolts(ADS1115Sensor* ads, int8_t adsChannel) {
    const bool useAds = (ads != nullptr && ads->isEnabled() && adsChannel >= 0 && adsChannel <= 3);
    return useAds ? ads->readMilliVolts((uint8_t)adsChannel)
                  : (float)analogReadMilliVolts(_pin);
}

float ACS758Sensor::readCurrent(ADS1115Sensor* ads, int8_t adsChannel) {
    float rawMv = readRawMilliVolts(ads, adsChannel);
    float deltaMv = rawMv - _zeroOffset;
    float currentA = deltaMv / _sensitivity;

    // Apply dead-band cutoff for noise around 0A
    if (fabsf(currentA) < ACS758_DEADBAND_A) {
        currentA = 0.0f;
    }

    return _filter.update(currentA);
}
