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
#include "../config/config.h"

// Exponential moving average weight for offset tracking (0.0–1.0).
// Lower = smoother but slower to adapt. 0.001 ≈ 1000-sample time constant.
static const float OFFSET_ALPHA = 0.001f;

// ADC RMS noise floor (in ADC units). Readings below this are clamped to 0.
static const float ADC_NOISE_FLOOR = 6.0f;

ZMPT101B::ZMPT101B(uint8_t pin, float calibration)
    : _pin(pin),
      _calibration(calibration),
      _offset(2048.0f),         // ESP32 12-bit ADC midpoint (3.3V / 2)
      _lastRawAdc(0.0f),
      _filter(FILTER_WINDOW_SIZE) {
}

void ZMPT101B::begin() {
    if (_pin >= 40) {
        Serial.println("[ZMPT101B] Disabled — invalid pin");
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

    Serial.printf("[ZMPT101B] Initialized on GPIO %d (offset=%.1f, cal=%.1f)\n",
                  _pin, _offset, _calibration);
}

float ZMPT101B::calculateRMS() {
    float sumSquares = 0.0f;
    float sumSamples = 0.0f;
    uint32_t sampleCount = 0;
    uint32_t startMicros = micros();
    uint32_t windowMicros = ADC_SAMPLE_WINDOW * 1000UL;

    // Sample for the configured window duration
    while ((micros() - startMicros) < windowMicros && sampleCount < ADC_SAMPLES) {
        float sample = (float)analogRead(_pin);
        float centered = sample - _offset;  // Remove DC offset
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

    _lastRawAdc = sumSquares / (float)sampleCount;  // Mean squared (diagnostic)

    // True RMS = sqrt( mean of squared samples )
    float rms = sqrtf(_lastRawAdc);

    // Noise floor dead-band: clamp near-zero readings to exactly 0
    if (rms < ADC_NOISE_FLOOR) {
        return 0.0f;
    }

    // Convert ADC units to voltage: (rms / 4095) × 3.3V
    float adcVoltage = rms * 3.3f / 4095.0f;

    // Apply calibration to get actual AC voltage
    return adcVoltage * _calibration;
}

float ZMPT101B::readRMSVoltage() {
    if (_pin >= 40) return 0.0f;

    float raw = calculateRMS();
    return _filter.update(raw);  // Apply moving average smoothing
}

float ZMPT101B::readRawADC() {
    return _lastRawAdc;
}

