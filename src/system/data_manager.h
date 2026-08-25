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
        HEALTH_TEMP2 = 1 << 6, HEALTH_CPU_TEMP = 1 << 7, HEALTH_RPM = 1 << 8,
        HEALTH_ACS758 = 1 << 9, HEALTH_ADS1115 = 1 << 10
    };
    // --- RAW ADC values ---
    float zmpt1_adc;        // ZMPT101B #1 raw (mV) — Generator AC
    float zmpt2_adc;        // ZMPT101B #2 raw (mV) — Inverter AC
    float zmct_adc;         // ZMCT103C raw (mV)   — Inverter AC Current
    float acs758_adc;       // ACS758 50A raw (mV) — Inverter DC Current

    // --- AC Generator Side (Before Rectifier/MPPT) ---
    float gen_ac_voltage;   // ZMPT101B #1: Generator Raw AC RMS voltage (V)

    // --- AC Inverter Output Side (Household Load) ---
    float inv_ac_voltage;   // ZMPT101B #2: Inverter AC 220V RMS voltage (V)
    float inv_ac_current;   // ZMCT103C: Inverter AC Load RMS current (A)
    float inv_ac_power;     // Inverter AC Output Real Power (W) = V_inv_ac × I_inv_ac × PF

    // --- INA226 #1 (DC / Battery & MPPT Charging) ---
    float ina1_voltage;     // Battery Bus voltage (V)
    float ina1_current;     // MPPT Charging Current (A)
    float ina1_power;       // MPPT Charging Power (W)
    float battery_soc;      // 12V 65Ah Lakoni State of Charge (%)
    float battery_wh;       // Estimated remaining energy (Wh)

    // --- ACS758 50A (Inverter DC Input Discharge) ---
    float inverter_current; // Inverter DC discharge current (A)
    float inverter_power;   // Inverter DC load power (W) = V_battery × I_inverter
    float inverter_efficiency; // Inverter conversion efficiency (%) = (P_ac_out / P_dc_in) * 100

    // --- INA226 #2 (ESP32, Control & 12V Lighting Aux Power) ---
    float ina2_voltage;     // Aux/Control Bus voltage (V)
    float ina2_current;     // Control & Lighting Current (A)
    float ina2_power;       // Control & Lighting Power (W)

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
