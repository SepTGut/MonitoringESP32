// =============================================================
//  config_manager.cpp — Dynamic Configuration Manager
// =============================================================

#include "config_manager.h"
#include "../config/config.h"
#include <LittleFS.h>

ConfigManager configManager;

ConfigManager::ConfigManager() {
    loadDefaults();
}

void ConfigManager::loadDefaults() {
    strncpy(_config.apSSID, WIFI_AP_SSID, sizeof(_config.apSSID) - 1);
    _config.apSSID[sizeof(_config.apSSID) - 1] = '\0';

    strncpy(_config.apPass, WIFI_AP_PASS, sizeof(_config.apPass) - 1);
    _config.apPass[sizeof(_config.apPass) - 1] = '\0';

    _config.staEnabled = false;
    _config.staSSID[0] = '\0';
    _config.staPass[0] = '\0';

    _config.sensorPollMs = SENSOR_POLL_MS;
    _config.wsPushMs     = WEBSOCKET_PUSH_MS;
    _config.serialLogMs  = SERIAL_LOG_MS;

    _config.zmpt1Cal = ZMPT_CALIBRATION_1;
    _config.zmpt2Cal = ZMPT_CALIBRATION_2;
    _config.zmctCal  = ZMCT_CALIBRATION;
    _config.pf       = AC_POWER_FACTOR;

    _config.maxV     = DEFAULT_MAX_V;
    _config.maxA     = DEFAULT_MAX_A;
    _config.maxRpm   = DEFAULT_MAX_RPM;
    _config.maxTemp  = DEFAULT_MAX_TEMP;
}

bool ConfigManager::begin() {
    // LittleFS initialization is also checked by WebDashboard, 
    // but we mount it here to ensure settings can load.
    if (!LittleFS.begin(true)) {
        Serial.println("[Config] Error mounting LittleFS, using defaults");
        loadDefaults();
        return false;
    }
    return load();
}

bool ConfigManager::load() {
    if (!LittleFS.exists(_filename)) {
        Serial.println("[Config] File not found. Saving defaults.");
        save();
        return true;
    }

    File configFile = LittleFS.open(_filename, "r");
    if (!configFile) {
        Serial.println("[Config] Failed to open config file for reading");
        return false;
    }

    StaticJsonDocument<1024> doc;
    DeserializationError error = deserializeJson(doc, configFile);
    configFile.close();

    if (error) {
        Serial.println("[Config] Failed to parse config file, using defaults");
        loadDefaults();
        return false;
    }

    updateFromJson(doc.as<JsonVariant>());
    Serial.println("[Config] Loaded configuration successfully");
    return true;
}

bool ConfigManager::save() {
    File configFile = LittleFS.open(_filename, "w");
    if (!configFile) {
        Serial.println("[Config] Failed to open config file for writing");
        return false;
    }

    StaticJsonDocument<1024> doc;
    serialize(doc);

    if (serializeJson(doc, configFile) == 0) {
        Serial.println("[Config] Failed to write config to file");
        configFile.close();
        return false;
    }

    configFile.close();
    Serial.println("[Config] Saved configuration successfully");
    return true;
}

void ConfigManager::updateFromJson(const JsonVariant& json) {
    if (json["apSsid"].is<const char*>()) {
        strncpy(_config.apSSID, json["apSsid"], sizeof(_config.apSSID) - 1);
        _config.apSSID[sizeof(_config.apSSID) - 1] = '\0';
    }
    if (json["apPass"].is<const char*>()) {
        strncpy(_config.apPass, json["apPass"], sizeof(_config.apPass) - 1);
        _config.apPass[sizeof(_config.apPass) - 1] = '\0';
    }

    if (json["staEnabled"].is<bool>()) {
        _config.staEnabled = json["staEnabled"];
    }
    if (json["staSsid"].is<const char*>()) {
        strncpy(_config.staSSID, json["staSsid"], sizeof(_config.staSSID) - 1);
        _config.staSSID[sizeof(_config.staSSID) - 1] = '\0';
    }
    if (json["staPass"].is<const char*>()) {
        strncpy(_config.staPass, json["staPass"], sizeof(_config.staPass) - 1);
        _config.staPass[sizeof(_config.staPass) - 1] = '\0';
    }

    if (json["pollMs"].is<uint32_t>()) {
        _config.sensorPollMs = json["pollMs"];
    }
    if (json["wsPushMs"].is<uint32_t>()) {
        _config.wsPushMs = json["wsPushMs"];
    }
    if (json["logMs"].is<uint32_t>()) {
        _config.serialLogMs = json["logMs"];
    }

    if (json["zmpt1Cal"].is<float>()) {
        _config.zmpt1Cal = json["zmpt1Cal"];
    }
    if (json["zmpt2Cal"].is<float>()) {
        _config.zmpt2Cal = json["zmpt2Cal"];
    }
    if (json["zmctCal"].is<float>()) {
        _config.zmctCal = json["zmctCal"];
    }
    if (json["pf"].is<float>()) {
        _config.pf = json["pf"];
    }

    if (json["maxV"].is<float>()) {
        _config.maxV = json["maxV"];
    }
    if (json["maxA"].is<float>()) {
        _config.maxA = json["maxA"];
    }
    if (json["maxRpm"].is<uint32_t>()) {
        _config.maxRpm = json["maxRpm"];
    }
    if (json["maxTemp"].is<uint32_t>()) {
        _config.maxTemp = json["maxTemp"];
    }
}

void ConfigManager::serialize(JsonDocument& doc) const {
    doc["apSsid"]     = _config.apSSID;
    doc["apPass"]     = _config.apPass;
    doc["staEnabled"] = _config.staEnabled;
    doc["staSsid"]    = _config.staSSID;
    doc["staPass"]    = _config.staPass;

    doc["pollMs"]     = _config.sensorPollMs;
    doc["wsPushMs"]   = _config.wsPushMs;
    doc["logMs"]      = _config.serialLogMs;

    doc["zmpt1Cal"]   = _config.zmpt1Cal;
    doc["zmpt2Cal"]   = _config.zmpt2Cal;
    doc["zmctCal"]    = _config.zmctCal;
    doc["pf"]         = _config.pf;

    doc["maxV"]       = _config.maxV;
    doc["maxA"]       = _config.maxA;
    doc["maxRpm"]     = _config.maxRpm;
    doc["maxTemp"]    = _config.maxTemp;
}
