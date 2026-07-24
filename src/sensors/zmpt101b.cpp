// =============================================================
//  zmpt101b.cpp — ZMPT101B AC Voltage Sensor Driver
//
//  Sampling approach:
//  1. Collect N samples over at least one full AC cycle (~25ms)
//  2. Remove DC offset (continuously tracked)
//  3. Vrms = sqrt( sum(sample²) / N )
//  4. Apply calibration multiplier
//  5. Smooth with moving average filter
//
//  Anti-drift: DC offset is tracked with an exponential moving
//  average so it follows ESP32 ADC thermal drift over time.
// =============================================================

#include "zmpt101b.h"
#include "ads1115_sensor.h"
#include "../config/config.h"

// Exponential moving average weight for offset tracking (0.0–1.0).
// Lower = smoother but slower to adapt. 0.001 ≈ 1000-sample time constant.
static const float OFFSET_ALPHA = 0.001f;

// Noise floor dead-band (in mV). Readings below this are clamped to 0.
// Prevents floating pin / ambient EM noise from producing false AC readings.
static const float ADC_NOISE_FLOOR_MV = 45.0f;
static const float AC_MIN_VOLTAGE_CUTOFF = 3.0f; // Minimum valid AC voltage (V)

ZMPT101B::ZMPT101B(uint8_t pin, float calibration)
    : _pin(pin),
      _calibration(calibration),
      _offset(1650.0f),         // ESP32 ADC midpoint (~1.65V / 1650mV)
      _lastRawAdc(0.0f),
      _filter(FILTER_WINDOW_SIZE) {
}

void ZMPT101B::begin() {
    if (_pin >= 40) {
        Serial.println("[ZMPT101B] Disabled — invalid pin");
        return;
    }

    pinMode(_pin, INPUT);
    analogSetAttenuation(ADC_11db);
    analogReadResolution(12);

    // Auto-calibrate DC offset at idle using eFuse calibrated millivolts
    calibrateZeroOffset();

    Serial.printf("[ZMPT101B] Initialized on GPIO %d (eFuse offset=%.1fmV, cal=%.1f)\n",
                  _pin, _offset, _calibration);
}

float ZMPT101B::calibrateZeroOffset(ADS1115Sensor* ads, int8_t adsChannel) {
    const bool useAds = (ads != nullptr && ads->isEnabled() && adsChannel >= 0 && adsChannel <= 3);
    float sum = 0.0f;
    const int count = 1000;
    for (int i = 0; i < count; i++) {
        sum += useAds ? ads->readMilliVolts((uint8_t)adsChannel)
                      : (float)analogReadMilliVolts(_pin);
        delayMicroseconds(100);
    }
    _offset = sum / (float)count;
    Serial.printf("[ZMPT101B] Calibrated zero baseline offset: %.2f mV\n", _offset);
    return _offset;
}

float ZMPT101B::calculateRMS(ADS1115Sensor* ads, int8_t adsChannel) {
    float sumSquares = 0.0f;
    float sumSamples = 0.0f;
    uint32_t sampleCount = 0;
    uint32_t startMicros = micros();
    uint32_t windowMicros = ADC_SAMPLE_WINDOW * 1000UL;

    const bool useAds = (ads != nullptr && ads->isEnabled() && adsChannel >= 0 && adsChannel <= 3);

    // Sample for the configured window duration
    while ((micros() - startMicros) < windowMicros && sampleCount < ADC_SAMPLES) {
        float sampleMv = useAds ? ads->readMilliVolts((uint8_t)adsChannel)
                                : (float)analogReadMilliVolts(_pin);
        float centered = sampleMv - _offset;  // Remove DC offset in mV
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

    _lastRawAdc = sumSquares / (float)sampleCount;  // Mean squared (diagnostic in mV^2)

    // True RMS in mV = sqrt( mean of squared samples )
    float rmsMv = sqrtf(_lastRawAdc);

    // Noise floor dead-band: clamp floating pin noise and idle noise to exactly 0
    if (rmsMv < ADC_NOISE_FLOOR_MV) {
        return 0.0f;
    }

    // Convert mV to Volts
    float adcVoltage = rmsMv / 1000.0f;

    // Apply calibration to get actual AC voltage
    float acVoltage = adcVoltage * _calibration;

    // Minimum cutoff guard: ignore residual noise below 3V AC
    return (acVoltage >= AC_MIN_VOLTAGE_CUTOFF) ? acVoltage : 0.0f;
}

float ZMPT101B::readRMSVoltage(ADS1115Sensor* ads, int8_t adsChannel) {
    const bool useAds = (ads != nullptr && ads->isEnabled() && adsChannel >= 0 && adsChannel <= 3);
    if (!useAds && _pin >= 40) return 0.0f;

    float raw = calculateRMS(ads, adsChannel);
    return _filter.update(raw);  // Apply moving average smoothing
}

float ZMPT101B::readRawADC() {
    return _lastRawAdc;
}

