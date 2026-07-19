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
#include "../config/config.h"

// Exponential moving average weight for offset tracking (0.0–1.0).
// Lower = smoother but slower to adapt. 0.001 ≈ 1000-sample time constant.
static const float OFFSET_ALPHA = 0.001f;

// ADC RMS noise floor (in ADC units). Readings below this are clamped to 0.
// Typical ESP32 ADC idle noise is ~20-40 LSB RMS.
static const float ADC_NOISE_FLOOR = 6.0f;

ZMCT103C::ZMCT103C(uint8_t pin, float calibration)
    : _pin(pin),
      _calibration(calibration),
      _offset(2048.0f),
      _lastRawAdc(0.0f),
      _filter(FILTER_WINDOW_SIZE) {
}

void ZMCT103C::begin() {
    if (_pin >= 40) {
        Serial.println("[ZMCT103C] Disabled — invalid pin");
        return;
    }

    pinMode(_pin, INPUT);

    // Auto-calibrate DC offset at idle (initial seed for continuous tracking)
    float sum = 0.0f;
    for (int i = 0; i < 1000; i++) {
        sum += analogRead(_pin);
        delayMicroseconds(100);
    }
    _offset = sum / 1000.0f;

    Serial.printf("[ZMCT103C] Initialized on GPIO %d (offset=%.1f, cal=%.1f)\n",
                  _pin, _offset, _calibration);
}

float ZMCT103C::calculateRMS() {
    float sumSquares = 0.0f;
    float sumSamples = 0.0f;
    uint32_t sampleCount = 0;
    uint32_t startMicros = micros();
    uint32_t windowMicros = ADC_SAMPLE_WINDOW * 1000UL;

    while ((micros() - startMicros) < windowMicros && sampleCount < ADC_SAMPLES) {
        float sample = (float)analogRead(_pin);
        float centered = sample - _offset;
        sumSquares += centered * centered;
        sumSamples += sample;
        sampleCount++;
    }

    if (sampleCount == 0) {
        _lastRawAdc = 0.0f;
        return 0.0f;
    }

    // Continuously track DC offset using exponential moving average.
    // This adapts to thermal drift without needing a recalibration cycle.
    float measuredMean = sumSamples / (float)sampleCount;
    _offset += OFFSET_ALPHA * (measuredMean - _offset);

    _lastRawAdc = sumSquares / (float)sampleCount;

    float rms = sqrtf(_lastRawAdc);

    // Noise floor dead-band: clamp near-zero readings to exactly 0
    if (rms < ADC_NOISE_FLOOR) {
        return 0.0f;
    }

    // Convert ADC units to burden voltage: (rms / 4095) × 3.3V
    float adcVoltage = rms * 3.3f / 4095.0f;

    // Apply calibration to get actual AC current
    // Calibration absorbs: turns ratio (1000:1) + burden resistor value
    return adcVoltage * _calibration;
}

float ZMCT103C::readRMSCurrent() {
    if (_pin >= 40) return 0.0f;

    float raw = calculateRMS();
    return _filter.update(raw);
}

float ZMCT103C::readRawADC() {
    return _lastRawAdc;
}

