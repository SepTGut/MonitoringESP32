// =============================================================
//  filters.cpp — Signal Processing Utilities
// =============================================================

#include "filters.h"

MovingAverage::MovingAverage(uint8_t windowSize)
    : _size(windowSize == 0 ? 1 : windowSize), _index(0), _sum(0.0f), _filled(false) {
    _buffer = new float[_size];
    memset(_buffer, 0, sizeof(float) * _size);
}

MovingAverage::~MovingAverage() {
    delete[] _buffer;
}

float MovingAverage::update(float newValue) {
    // Subtract the oldest value from the running sum
    _sum -= _buffer[_index];
    // Store and add the new value
    _buffer[_index] = newValue;
    _sum += newValue;
    // Advance circular index
    _index++;
    if (_index >= _size) {
        _index = 0;
        _filled = true;
    }
    // Return average
    uint8_t count = _filled ? _size : _index;
    return (count > 0) ? (_sum / count) : 0.0f;
}

float MovingAverage::getValue() const {
    uint8_t count = _filled ? _size : _index;
    return (count > 0) ? (_sum / count) : 0.0f;
}

void MovingAverage::reset() {
    memset(_buffer, 0, sizeof(float) * _size);
    _index = 0;
    _sum = 0.0f;
    _filled = false;
}
