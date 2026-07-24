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
    enum Health : uint16_t {
        HEALTH_AC_V1 = 1 << 0, HEALTH_AC_V2 = 1 << 1, HEALTH_AC_I = 1 << 2,
        HEALTH_INA1 = 1 << 3, HEALTH_INA2 = 1 << 4, HEALTH_TEMP1 = 1 << 5,
        HEALTH_TEMP2 = 1 << 6, HEALTH_CPU_TEMP = 1 << 7, HEALTH_RPM = 1 << 8
    };
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
    float temperature_esp;  // Internal CPU temp (°C)

    // --- RPM ---
    uint32_t rpm;

    // --- I2C Auto-Discovery ---
    uint8_t i2c_addresses[16];
    uint8_t i2c_count;
    uint16_t health;
    uint32_t sequence;
    uint32_t cycleMs;
    uint32_t overruns;
};

// Thread-safe data manager using FreeRTOS mutex
class DataManager {
public:
    DataManager();
    ~DataManager();

    // Get atomic copy of all sensor data
    SensorData getData();

    // Publish a complete measurement frame atomically.
    void publish(const SensorData& data);
    void updateI2CAddresses(const uint8_t* addresses, uint8_t count);

private:
    SensorData _data;
    SemaphoreHandle_t _mutex;
};

// Global instance — defined in data_manager.cpp
extern DataManager dataManager;
