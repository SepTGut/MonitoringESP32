#pragma once
// =============================================================
//  data_manager.h — Thread-Safe Central Data Store
//  Mutex-protected SensorData shared between Core 0 and Core 1
// =============================================================

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// Central data structure holding all sensor readings
struct SensorData {
    // --- RAW ADC values ---
    float zmpt1_adc;        // ZMPT101B #1 raw
    float zmpt2_adc;        // ZMPT101B #2 raw
    float zmct_adc;         // ZMCT103C raw

    // --- AC Processed ---
    float ac_voltage;       // ZMPT101B #1 RMS voltage (V)
    float ac_current;       // ZMCT103C RMS current (A)
    float ac_power;         // Estimated real power (W) = V × I × PF
    float ac_voltage2;      // ZMPT101B #2 raw monitoring (V)

    // --- INA226 #1 (DC) ---
    float ina1_voltage;     // Bus voltage (V)
    float ina1_current;     // Current (A)
    float ina1_power;       // Power (W)

    // --- INA226 #2 (DC) ---
    float ina2_voltage;
    float ina2_current;
    float ina2_power;

    // --- Temperature ---
    float temperature1;     // DS18B20 #1 (°C)
    float temperature2;     // DS18B20 #2 (°C)

    // --- RPM ---
    uint32_t rpm;
};

// Thread-safe data manager using FreeRTOS mutex
class DataManager {
public:
    DataManager();
    ~DataManager();

    // Get atomic copy of all sensor data
    SensorData getData();

    // Individual thread-safe update methods
    void updateACVoltage(float voltage, float rawAdc);
    void updateACVoltage2(float voltage, float rawAdc);
    void updateACCurrent(float current, float rawAdc);
    void updateACPower(float power);
    void updateDC1(float voltage, float current, float power);
    void updateDC2(float voltage, float current, float power);
    void updateTemperature1(float tempC);
    void updateTemperature2(float tempC);
    void updateRPM(uint32_t rpm);

private:
    SensorData _data;
    SemaphoreHandle_t _mutex;
};

// Global instance — defined in data_manager.cpp
extern DataManager dataManager;
