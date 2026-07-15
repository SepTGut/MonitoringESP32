// =============================================================
//  zmpt101b.cpp — ZMPT101B AC Voltage Sensor Driver
//
//  Sampling approach:
//  1. Collect N samples over at least one full AC cycle (~25ms)
//  2. Remove DC offset (midpoint)
//  3. Vrms = sqrt( sum(sample²) / N )
//  4. Apply calibration multiplier
//  5. Smooth with moving average filter
// =============================================================

#include "zmpt101b.h"
#include "../config/config.h"

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

    // Auto-calibrate DC offset: read idle samples to find midpoint
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
    uint32_t sampleCount = 0;
    uint32_t startMicros = micros();
    uint32_t windowMicros = ADC_SAMPLE_WINDOW * 1000UL;

    // Sample for the configured window duration
    while ((micros() - startMicros) < windowMicros && sampleCount < ADC_SAMPLES) {
        float sample = (float)analogRead(_pin);
        float centered = sample - _offset;  // Remove DC offset
        sumSquares += centered * centered;
        sampleCount++;
    }

    if (sampleCount == 0) {
        _lastRawAdc = 0.0f;
        return 0.0f;
    }

    _lastRawAdc = sumSquares / (float)sampleCount;  // Mean squared (diagnostic)

    // True RMS = sqrt( mean of squared samples )
    float rms = sqrtf(_lastRawAdc);

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
