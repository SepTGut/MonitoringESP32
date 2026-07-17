#pragma once
// =============================================================
//  config_manager.h — Dynamic Configuration Manager
//  Handles loading and saving settings to LittleFS as JSON
// =============================================================

#include <Arduino.h>
#include <ArduinoJson.h>

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

    // Calibration Multipliers
    float zmpt1Cal;
    float zmpt2Cal;
    float zmctCal;
    float pf;

    // Display Limits
    float maxV;
    float maxA;
    uint32_t maxRpm;
    uint32_t maxTemp;

    // INA226 Bus Address Assignments
    uint8_t ina1Addr;
    uint8_t ina2Addr;

    // Simulation/Dummy Mode
    bool dummyMode;
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

    // Get the current configuration
    const SystemConfig& getConfig() const { return _config; }

    // Update settings from a JSON payload
    void updateFromJson(const JsonVariant& json);

    // Serialize current config to JSON
    void serialize(JsonDocument& doc) const;

private:
    SystemConfig _config;
    const char*  _filename = "/config.json";

    // Load defaults from config.h
    void loadDefaults();
};

extern ConfigManager configManager;
