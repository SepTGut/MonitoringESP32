#pragma once
// =============================================================
//  config_manager.h — Dynamic Configuration Manager
//  Handles loading and saving settings to LittleFS as JSON
// =============================================================

#include <Arduino.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

struct SystemConfig {
    // WiFi AP Mode
    char apSSID[33];
    char apPass[64];

    // WiFi STA Mode
    bool staEnabled;
    char staSSID[33];
    char staPass[64];

    // Timing (ms)
    uint32_t sensorPollMs;
    uint32_t wsPushMs;
    uint32_t serialLogMs;

    // Calibration Multipliers & Baseline Zero Offsets (mV)
    float zmpt1Cal;
    float zmpt2Cal;
    float zmctCal;
    float acs758Cal;
    float zmpt1OffsetMv;
    float zmpt2OffsetMv;
    float zmctOffsetMv;
    float acs758OffsetMv;
    float pf;

    // Display Limits — DC (INA226)
    float maxV;
    float maxA;
    // Display Limits — AC (ZMPT/ZMCT)
    float maxAcV;
    float maxAcA;
    // Display Limits — General
    uint32_t maxRpm;
    uint32_t maxTemp;

    // INA226 Bus Address Assignment
    uint8_t ina1Addr;

    // External ADS1115 16-Bit ADC Options
    bool useAds1115;
    uint8_t adsAddr;

    // Power Management & Deep Sleep
    bool enablePowerSwitch;
    uint32_t powerSwitchTimeoutMs;

    // Simulation/Dummy Mode
    bool dummyMode;
    bool setupRequired;
};

class ConfigManager {
public:
    ConfigManager();

    // Mount LittleFS and load configuration
    bool begin();

    // Load from /config.json
    bool load();

    // Save to /config.json
    bool save();

    // Get an atomic snapshot of the current configuration.
    SystemConfig getConfig() const;

    // Update settings from a JSON payload
    bool updateFromJson(const JsonVariant& json, String& error, String& field,
                        bool& restartRequired, bool allowSetupCompletion = true);

    // Update zero-point baseline offsets and save to LittleFS (thread-safe)
    bool updateOffsets(float o1, float o2, float oi, float o_acs = 1675.0f);

    // Serialize current config to JSON
    void serialize(JsonDocument& doc, bool includeSecrets = false) const;

private:
    SystemConfig _config;
    const char*  _filename = "/config.json";
    SemaphoreHandle_t _mutex;

    // Load defaults from config.h
    void loadDefaults();
};

extern ConfigManager configManager;
