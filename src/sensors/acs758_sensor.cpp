// =============================================================
//  acs758_sensor.cpp — ACS758 50A Hall-Effect DC Current Sensor
// =============================================================

#include "acs758_sensor.h"
#include "ads1115_sensor.h"
#include "../config/config.h"

// Cutoff threshold for zero current dead-band (Amperes)
// Clamps idle residual noise/drift below 0.35A to exactly 0.00A
static const float ACS758_DEADBAND_A = 0.35f;

ACS758Sensor::ACS758Sensor(uint8_t fallbackPin, float sensitivityMvPerA, float calMultiplier)
    : _pin(fallbackPin),
      _sensitivity(sensitivityMvPerA),
      _calMultiplier(calMultiplier),
      _zeroOffset(1675.0f),       // Calibrated baseline (~1.675V)
      _filter(FILTER_WINDOW_SIZE) {
}

void ACS758Sensor::begin() {
    if (_pin < 40) {
        pinMode(_pin, INPUT);
        analogSetAttenuation(ADC_11db);
        analogReadResolution(12);
    }
    Serial.printf("[ACS758] Driver initialized (sensitivity: %.1f mV/A, calMultiplier: %.4f, default zero: %.1f mV)\n",
                  _sensitivity, _calMultiplier, _zeroOffset);
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
    // 32x oversampling to filter out internal ADC noise
    float sum = 0.0f;
    const int samples = 32;
    for (int i = 0; i < samples; i++) {
        sum += useAds ? ads->readMilliVolts((uint8_t)adsChannel)
                      : (float)analogReadMilliVolts(_pin);
        delayMicroseconds(50);
    }
    return sum / (float)samples;
}

float ACS758Sensor::readCurrent(ADS1115Sensor* ads, int8_t adsChannel) {
    float rawMv = readRawMilliVolts(ads, adsChannel);
    float deltaMv = rawMv - _zeroOffset;
    
    // Apply calibration multiplier to scale raw delta to signed Amperes
    float signedCurrentA = (deltaMv / _sensitivity) * _calMultiplier;

    // Thermal zero-drift tracking: when in idle noise floor, slowly track baseline
    if (fabsf(signedCurrentA) < ACS758_DEADBAND_A) {
        _zeroOffset += 0.0005f * (rawMv - _zeroOffset);
    }

    // Filter the SIGNED signal first so symmetric positive/negative noise cancels to 0.0A
    float filteredCurrentA = _filter.update(signedCurrentA);

    // Apply dead-band cutoff on the filtered signal to eliminate residual idle noise
    if (fabsf(filteredCurrentA) < ACS758_DEADBAND_A) {
        return 0.0f;
    }

    // Return positive magnitude for inverter discharge current
    return fabsf(filteredCurrentA);
}
