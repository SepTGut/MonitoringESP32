#pragma once
// =============================================================
//  filters.h — Signal Processing Utilities
//  Moving average filter for noise reduction
// =============================================================

#include <Arduino.h>

class MovingAverage {
public:
    MovingAverage(uint8_t windowSize = 10);
    ~MovingAverage();

    // Add a new value and return the current average
    float update(float newValue);

    // Get current average without adding a value
    float getValue() const;

    // Reset all values to zero
    void reset();

private:
    float*  _buffer;
    uint8_t _size;
    uint8_t _index;
    float   _sum;
    bool    _filled;
};
