// =============================================================
//  zmct103c.cpp — ZMCT103C AC Current Transformer Driver
//
//  Same RMS sampling approach as ZMPT101B:
//  1. Collect N samples over one AC cycle
//  2. Remove DC offset (continuously tracked)
//  3. Irms = sqrt( sum(sample²) / N )
//  4. Apply calibration factor (accounts for turns ratio + burden R)
//  5. Smooth with moving average filter
//
//  Anti-drift: DC offset is tracked with an exponential moving
//  average so it follows ESP32 ADC thermal drift over time.
// =============================================================

#include "zmct103c.h"
#include "ads1115_sensor.h"
#include "../config/config.h"

// Exponential moving average weight for offset tracking (0.0–1.0).
// Lower = smoother but slower to adapt. 0.001 ≈ 1000-sample time constant.
static const float OFFSET_ALPHA = 0.001f;

// Noise floor dead-band (in mV). Readings below this are clamped to 0.
// Prevents floating pin / ambient EM noise from producing false AC current readings.
static const float ADC_NOISE_FLOOR_MV = 45.0f;
static const float AC_MIN_CURRENT_CUTOFF = 0.05f; // Minimum valid AC current (A)

ZMCT103C::ZMCT103C(uint8_t pin, float calibration)
    : _pin(pin),
      _calibration(calibration),
      _offset(1650.0f),         // ESP32 ADC midpoint (~1.65V / 1650mV)
      _lastRawAdc(0.0f),
      _filter(FILTER_WINDOW_SIZE) {
}

void ZMCT103C::begin() {
    if (_pin >= 40) {
        Serial.println("[ZMCT103C] Disabled — invalid pin");
        return;
    }

    pinMode(_pin, INPUT);
    analogSetAttenuation(ADC_11db);
    analogReadResolution(12);

    Serial.printf("[ZMCT103C] Initialized on GPIO %d (offset=%.1fmV, cal=%.1f)\n",
                  _pin, _offset, _calibration);
}

float ZMCT103C::calibrateZeroOffset(ADS1115Sensor* ads, int8_t adsChannel) {
    const bool useAds = (ads != nullptr && ads->isEnabled() && adsChannel >= 0 && adsChannel <= 3);
    float sum = 0.0f;
    const int count = 1000;
    for (int i = 0; i < count; i++) {
        sum += useAds ? ads->readMilliVolts((uint8_t)adsChannel)
                      : (float)analogReadMilliVolts(_pin);
        delayMicroseconds(100);
    }
    _offset = sum / (float)count;
    Serial.printf("[ZMCT103C] Calibrated zero baseline offset: %.2f mV\n", _offset);
    return _offset;
}

float ZMCT103C::calculateRMS(ADS1115Sensor* ads, int8_t adsChannel) {
    float sumSquares = 0.0f;
    float sumSamples = 0.0f;
    uint32_t sampleCount = 0;
    uint32_t startMicros = micros();
    uint32_t windowMicros = ADC_SAMPLE_WINDOW * 1000UL;

    const bool useAds = (ads != nullptr && ads->isEnabled() && adsChannel >= 0 && adsChannel <= 3);

    while ((micros() - startMicros) < windowMicros && sampleCount < ADC_SAMPLES) {
        float sampleMv = useAds ? ads->readMilliVolts((uint8_t)adsChannel)
                                : (float)analogReadMilliVolts(_pin);
        float centered = sampleMv - _offset;
        sumSquares += centered * centered;
        sumSamples += sampleMv;
        sampleCount++;
    }

    if (sampleCount == 0) {
        _lastRawAdc = 0.0f;
        return 0.0f;
    }

    // Continuously track DC offset using exponential moving average.
    float measuredMean = sumSamples / (float)sampleCount;
    _offset += OFFSET_ALPHA * (measuredMean - _offset);

    _lastRawAdc = sumSquares / (float)sampleCount;

    float rmsMv = sqrtf(_lastRawAdc);

    // Noise floor dead-band: clamp floating pin noise and idle noise to exactly 0
    if (rmsMv < ADC_NOISE_FLOOR_MV) {
        return 0.0f;
    }

    // Convert mV to burden voltage (V)
    float adcVoltage = rmsMv / 1000.0f;

    // Apply calibration to get actual AC current
    float acCurrent = adcVoltage * _calibration;

    // Minimum cutoff guard: ignore residual noise below 0.05A AC
    return (acCurrent >= AC_MIN_CURRENT_CUTOFF) ? acCurrent : 0.0f;
}

float ZMCT103C::readRMSCurrent(ADS1115Sensor* ads, int8_t adsChannel) {
    const bool useAds = (ads != nullptr && ads->isEnabled() && adsChannel >= 0 && adsChannel <= 3);
    if (!useAds && _pin >= 40) return 0.0f;

    float raw = calculateRMS(ads, adsChannel);
    return _filter.update(raw);
}

float ZMCT103C::readRawADC() {
    return _lastRawAdc;
}

