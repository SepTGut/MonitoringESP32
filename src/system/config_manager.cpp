// =============================================================
//  config_manager.cpp — Dynamic Configuration Manager
// =============================================================

#include "config_manager.h"
#include "../config/config.h"
#include "../config/pin_config.h"
#include <LittleFS.h>

ConfigManager configManager;

ConfigManager::ConfigManager() {
    _mutex = xSemaphoreCreateMutex();
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

    _config.ina1Addr = INA226_ADDR_1; // Default INA226 #1 (from pin_config.h)
    _config.ina2Addr = INA226_ADDR_2; // Default INA226 #2 (from pin_config.h)
    _config.dummyMode = false; // Simulated dummy sensors mode disabled by default
}

SystemConfig ConfigManager::getConfig() const {
    SystemConfig config = {};
    if (xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
        config = _config;
        xSemaphoreGive(_mutex);
    }
    return config;
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

    // Self-healing: if loaded SSID is corrupted or empty, reset to defaults immediately
    bool valid = true;
    if (strlen(_config.apSSID) == 0) {
        valid = false;
    } else {
        for (size_t i = 0; i < strlen(_config.apSSID); i++) {
            if (!isprint((unsigned char)_config.apSSID[i])) {
                valid = false;
                break;
            }
        }
    }

    if (!valid) {
        Serial.println("[Config] Loaded configuration contains invalid or corrupted SSID. Resetting to defaults.");
        loadDefaults();
        save();
    } else {
        Serial.println("[Config] Loaded configuration successfully");
    }
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
    if (xSemaphoreTake(_mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }
    
    if (json.containsKey("apSsid") && json["apSsid"].is<const char*>()) {
        strncpy(_config.apSSID, json["apSsid"].as<const char*>(), sizeof(_config.apSSID) - 1);
        _config.apSSID[sizeof(_config.apSSID) - 1] = '\0';
    }
    if (json.containsKey("apPass") && json["apPass"].is<const char*>()) {
        strncpy(_config.apPass, json["apPass"].as<const char*>(), sizeof(_config.apPass) - 1);
        _config.apPass[sizeof(_config.apPass) - 1] = '\0';
    }

    if (json.containsKey("staEnabled")) {
        _config.staEnabled = json["staEnabled"].as<bool>();
    }
    if (json.containsKey("staSsid") && json["staSsid"].is<const char*>()) {
        strncpy(_config.staSSID, json["staSsid"].as<const char*>(), sizeof(_config.staSSID) - 1);
        _config.staSSID[sizeof(_config.staSSID) - 1] = '\0';
    }
    if (json.containsKey("staPass") && json["staPass"].is<const char*>()) {
        strncpy(_config.staPass, json["staPass"].as<const char*>(), sizeof(_config.staPass) - 1);
        _config.staPass[sizeof(_config.staPass) - 1] = '\0';
    }

    if (json.containsKey("pollMs")) {
        _config.sensorPollMs = json["pollMs"].as<uint32_t>();
    }
    if (json.containsKey("wsPushMs")) {
        _config.wsPushMs = json["wsPushMs"].as<uint32_t>();
    }
    if (json.containsKey("logMs")) {
        _config.serialLogMs = json["logMs"].as<uint32_t>();
    }

    if (json.containsKey("zmpt1Cal")) {
        _config.zmpt1Cal = json["zmpt1Cal"].as<float>();
    }
    if (json.containsKey("zmpt2Cal")) {
        _config.zmpt2Cal = json["zmpt2Cal"].as<float>();
    }
    if (json.containsKey("zmctCal")) {
        _config.zmctCal = json["zmctCal"].as<float>();
    }
    if (json.containsKey("pf")) {
        _config.pf = json["pf"].as<float>();
    }

    if (json.containsKey("maxV")) {
        _config.maxV = json["maxV"].as<float>();
    }
    if (json.containsKey("maxA")) {
        _config.maxA = json["maxA"].as<float>();
    }
    if (json.containsKey("maxRpm")) {
        _config.maxRpm = json["maxRpm"].as<uint32_t>();
    }
    if (json.containsKey("maxTemp")) {
        _config.maxTemp = json["maxTemp"].as<uint32_t>();
    }

    if (json.containsKey("ina1Addr")) {
        _config.ina1Addr = json["ina1Addr"].as<uint8_t>();
    }
    if (json.containsKey("ina2Addr")) {
        _config.ina2Addr = json["ina2Addr"].as<uint8_t>();
    }
    if (json.containsKey("dummyMode")) {
        _config.dummyMode = json["dummyMode"].as<bool>();
    }

    // Prevent invalid HTTP payloads from turning the task loops into busy loops.
    if (_config.sensorPollMs < 1) _config.sensorPollMs = 1;
    if (_config.wsPushMs < 20) _config.wsPushMs = 20;
    if (_config.serialLogMs < 20) _config.serialLogMs = 20;
    _config.pf = constrain(_config.pf, 0.0f, 1.0f);
    
    xSemaphoreGive(_mutex);
}

void ConfigManager::serialize(JsonDocument& doc) const {
    const SystemConfig config = getConfig();
    doc["apSsid"]     = config.apSSID;
    doc["apPass"]     = config.apPass;
    doc["staEnabled"] = config.staEnabled;
    doc["staSsid"]    = config.staSSID;
    doc["staPass"]    = config.staPass;

    doc["pollMs"]     = config.sensorPollMs;
    doc["wsPushMs"]   = config.wsPushMs;
    doc["logMs"]      = config.serialLogMs;

    doc["zmpt1Cal"]   = config.zmpt1Cal;
    doc["zmpt2Cal"]   = config.zmpt2Cal;
    doc["zmctCal"]    = config.zmctCal;
    doc["pf"]         = config.pf;

    doc["maxV"]       = config.maxV;
    doc["maxA"]       = config.maxA;
    doc["maxRpm"]     = config.maxRpm;
    doc["maxTemp"]    = config.maxTemp;

    doc["ina1Addr"]   = config.ina1Addr;
    doc["ina2Addr"]   = config.ina2Addr;
    doc["dummyMode"]  = config.dummyMode;
}
