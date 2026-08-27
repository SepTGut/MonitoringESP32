// =============================================================
//  config_manager.cpp — Dynamic Configuration Manager
// =============================================================

#include "config_manager.h"
#include "../config/config.h"
#include "../config/pin_config.h"
#include <LittleFS.h>
#include <math.h>

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
    _config.acs758Cal = ACS758_CAL_MULTIPLIER;
    _config.zmpt1OffsetMv = 1665.0f;
    _config.zmpt2OffsetMv = 935.0f;
    _config.zmctOffsetMv  = 1672.0f;
    _config.acs758OffsetMv = ACS758_ZERO_OFFSET; // 1675.0f (ADS1115 A3)
    _config.pf       = AC_POWER_FACTOR;

    _config.maxV     = DEFAULT_MAX_V;
    _config.maxA     = DEFAULT_MAX_A;
    _config.maxAcV   = DEFAULT_MAX_AC_V;
    _config.maxAcA   = DEFAULT_MAX_AC_A;
    _config.maxRpm   = DEFAULT_MAX_RPM;
    _config.maxTemp  = DEFAULT_MAX_TEMP;

    _config.ina1Addr = INA226_ADDR_1; // Default INA226 #1 (from pin_config.h)
    _config.ina2Addr = INA226_ADDR_2; // Default INA226 #2 (from pin_config.h)
    _config.useAds1115 = true;        // External ADS1115 16-bit I2C ADC used by default
    _config.adsAddr = DEFAULT_ADS1115_ADDR; // Default ADS1115 address (0x48)
    _config.enablePowerSwitch = ENABLE_POWER_SWITCH;
    _config.powerSwitchTimeoutMs = POWER_SWITCH_TIMEOUT;
    _config.dummyMode = false; // Simulated dummy sensors mode disabled by default
    _config.setupRequired = true;
}

SystemConfig ConfigManager::getConfig() const {
    SystemConfig config = {};
    if (_mutex == NULL) {
        const_cast<ConfigManager*>(this)->_mutex = xSemaphoreCreateMutex();
    }
    if (_mutex != NULL && xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
        config = _config;
        xSemaphoreGive(_mutex);
    } else {
        config = _config;
    }
    return config;
}

bool ConfigManager::begin() {
    // Mount LittleFS. If unformatted, format once so partition is writable.
    if (!LittleFS.begin(false)) {
        Serial.println("[Config] LittleFS not mounted. Attempting initial format...");
        if (LittleFS.format() && LittleFS.begin(false)) {
            Serial.println("[Config] LittleFS formatted and mounted successfully");
        } else {
            Serial.println("[Config] Error mounting LittleFS, using defaults in RAM");
            loadDefaults();
            return false;
        }
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
    DeserializationError parseErr = deserializeJson(doc, configFile);
    configFile.close();

    if (parseErr) {
        Serial.printf("[Config] Failed to parse config file (%s), using defaults\n", parseErr.c_str());
        loadDefaults();
        return false;
    }

    String validationError, validationField;
    bool restartRequired = false;
    if (!updateFromJson(doc.as<JsonVariant>(), validationError, validationField, restartRequired, false)) {
        Serial.printf("[Config] Invalid saved configuration (%s), using defaults\n", validationError.c_str());
        loadDefaults();
        save();
        return false;
    }

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
    if (!LittleFS.begin(false)) {
        LittleFS.format();
        LittleFS.begin(false);
    }

    const char* temporaryFilename = "/config.tmp";
    File configFile = LittleFS.open(temporaryFilename, "w");
    if (!configFile) {
        Serial.println("[Config] Failed to open /config.tmp for writing. Formatting partition...");
        if (LittleFS.format() && LittleFS.begin(false)) {
            configFile = LittleFS.open(temporaryFilename, "w");
        }
        if (!configFile) {
            Serial.println("[Config] Critical error: could not open config file for writing");
            return false;
        }
    }

    StaticJsonDocument<1024> doc;
    serialize(doc, true);

    if (serializeJson(doc, configFile) == 0) {
        Serial.println("[Config] Failed to write config to file");
        configFile.close();
        return false;
    }

    configFile.close();
    const char* backupFilename = "/config.bak";
    LittleFS.remove(backupFilename);
    const bool hadPreviousConfig = LittleFS.exists(_filename);
    if (hadPreviousConfig && !LittleFS.rename(_filename, backupFilename)) {
        Serial.println("[Config] Failed to preserve previous config file");
        LittleFS.remove(temporaryFilename);
        return false;
    }
    if (!LittleFS.rename(temporaryFilename, _filename)) {
        Serial.println("[Config] Failed to replace config file");
        LittleFS.remove(temporaryFilename);
        if (hadPreviousConfig) LittleFS.rename(backupFilename, _filename);
        return false;
    }
    LittleFS.remove(backupFilename);
    Serial.println("[Config] Saved configuration successfully");
    return true;
}

namespace {
bool printableString(const char* value, size_t minLength, size_t maxLength) {
    if (value == nullptr) return false;
    const size_t length = strlen(value);
    if (length < minLength || length > maxLength) return false;
    for (size_t i = 0; i < length; ++i) if (!isprint(static_cast<unsigned char>(value[i]))) return false;
    return true;
}
bool validFinite(float value, float min, float max) { return isfinite(value) && value >= min && value <= max; }
bool validInaAddress(uint8_t address) { return address >= 0x40 && address <= 0x4F; }
void copyString(char* target, size_t size, const char* source) { strncpy(target, source, size - 1); target[size - 1] = '\0'; }
}

bool ConfigManager::updateFromJson(const JsonVariant& json, String& error, String& field,
                                   bool& restartRequired, bool allowSetupCompletion) {
    if (!json.is<JsonObjectConst>()) { error = "Payload must be a JSON object"; field = "payload"; return false; }
    SystemConfig next = getConfig();
    restartRequired = false;
    const JsonObjectConst object = json.as<JsonObjectConst>();
    auto fail = [&](const char* name, const char* message) { field = name; error = message; return false; };
    
    if (json.containsKey("apSsid") && json["apSsid"].is<const char*>()) {
        const char* value = json["apSsid"].as<const char*>(); if (!printableString(value, 1, 32)) return fail("apSsid", "SSID must contain 1-32 printable characters");
        copyString(next.apSSID, sizeof(next.apSSID), value); restartRequired = true;
    }
    if (json.containsKey("apPass") && json["apPass"].is<const char*>()) {
        const char* value = json["apPass"].as<const char*>(); if (!printableString(value, 8, 63)) return fail("apPass", "Password must contain 8-63 printable characters");
        if (strcmp(value, WIFI_AP_PASS) == 0 && next.setupRequired && allowSetupCompletion) return fail("apPass", "Replace the temporary installer password");
        copyString(next.apPass, sizeof(next.apPass), value);
        if (strcmp(value, WIFI_AP_PASS) != 0) next.setupRequired = false;
        restartRequired = true;
    }

    if (json.containsKey("staEnabled")) {
        if (!json["staEnabled"].is<bool>()) return fail("staEnabled", "Must be a boolean"); next.staEnabled = json["staEnabled"].as<bool>(); restartRequired = true;
    }
    if (json.containsKey("staSsid") && json["staSsid"].is<const char*>()) {
        const char* value = json["staSsid"].as<const char*>();
        if (strlen(value) > 0 && !printableString(value, 1, 32)) return fail("staSsid", "SSID must contain 1-32 printable characters");
        copyString(next.staSSID, sizeof(next.staSSID), value); restartRequired = true;
    }
    if (json.containsKey("staPass") && json["staPass"].is<const char*>()) {
        const char* value = json["staPass"].as<const char*>(); if (!printableString(value, 8, 63)) return fail("staPass", "Password must contain 8-63 printable characters"); copyString(next.staPass, sizeof(next.staPass), value); restartRequired = true;
    }

    if (json.containsKey("pollMs")) {
        const uint32_t value = json["pollMs"].as<uint32_t>(); if (value < 100 || value > 60000) return fail("pollMs", "Must be 100-60000"); next.sensorPollMs = value;
    }
    if (json.containsKey("wsPushMs")) {
        const uint32_t value = json["wsPushMs"].as<uint32_t>(); if (value < 100 || value > 60000) return fail("wsPushMs", "Must be 100-60000"); next.wsPushMs = value;
    }
    if (json.containsKey("logMs")) {
        const uint32_t value = json["logMs"].as<uint32_t>(); if (value < 250 || value > 60000) return fail("logMs", "Must be 250-60000"); next.serialLogMs = value;
    }

    if (json.containsKey("zmpt1Cal")) { const float v = json["zmpt1Cal"].as<float>(); if (v <= 0.0f || v > 1000.0f) return fail("zmpt1Cal", "Must be >0 and <=1000"); next.zmpt1Cal = v; }
    if (json.containsKey("zmpt2Cal")) { const float v = json["zmpt2Cal"].as<float>(); if (v <= 0.0f || v > 1000.0f) return fail("zmpt2Cal", "Must be >0 and <=1000"); next.zmpt2Cal = v; }
    if (json.containsKey("zmctCal"))  { const float v = json["zmctCal"].as<float>();  if (v <= 0.0f || v > 1000.0f) return fail("zmctCal", "Must be >0 and <=1000"); next.zmctCal = v; }
    if (json.containsKey("acs758Cal")) { const float v = json["acs758Cal"].as<float>(); if (v <= 0.0f || v > 50.0f) return fail("acs758Cal", "Must be >0 and <=50"); next.acs758Cal = v; }
    if (json.containsKey("zmpt1OffsetMv")) { const float v = json["zmpt1OffsetMv"].as<float>(); if (v < 500.0f || v > 3000.0f) return fail("zmpt1OffsetMv", "Must be 500-3000 mV"); next.zmpt1OffsetMv = v; }
    if (json.containsKey("zmpt2OffsetMv")) { const float v = json["zmpt2OffsetMv"].as<float>(); if (v < 500.0f || v > 3000.0f) return fail("zmpt2OffsetMv", "Must be 500-3000 mV"); next.zmpt2OffsetMv = v; }
    if (json.containsKey("zmctOffsetMv"))  { const float v = json["zmctOffsetMv"].as<float>();  if (v < 500.0f || v > 3000.0f) return fail("zmctOffsetMv", "Must be 500-3000 mV"); next.zmctOffsetMv = v; }
    if (json.containsKey("acs758OffsetMv")) { const float v = json["acs758OffsetMv"].as<float>(); if (v < 0.0f || v > 5000.0f) return fail("acs758OffsetMv", "Must be 0-5000 mV"); next.acs758OffsetMv = v; }
    if (json.containsKey("pf")) {
        const float value = json["pf"].as<float>(); if (!validFinite(value, 0.0f, 1.0f)) return fail("pf", "Must be finite and between 0-1"); next.pf = value;
    }

    if (json.containsKey("maxV")) {
        const float value = json["maxV"].as<float>(); if (!validFinite(value, 1.0f, 1000.0f)) return fail("maxV", "Must be finite and between 1-1000"); next.maxV = value;
    }
    if (json.containsKey("maxA")) {
        const float value = json["maxA"].as<float>(); if (!validFinite(value, 0.1f, 1000.0f)) return fail("maxA", "Must be finite and between 0.1-1000"); next.maxA = value;
    }
    if (json.containsKey("maxAcV")) {
        const float value = json["maxAcV"].as<float>(); if (!validFinite(value, 1.0f, 10000.0f)) return fail("maxAcV", "Must be finite and between 1-10000"); next.maxAcV = value;
    }
    if (json.containsKey("maxAcA")) {
        const float value = json["maxAcA"].as<float>(); if (!validFinite(value, 0.1f, 1000.0f)) return fail("maxAcA", "Must be finite and between 0.1-1000"); next.maxAcA = value;
    }
    if (json.containsKey("maxRpm")) {
        const uint32_t value = json["maxRpm"].as<uint32_t>(); if (value < 1 || value > 100000) return fail("maxRpm", "Must be 1-100000"); next.maxRpm = value;
    }
    if (json.containsKey("maxTemp")) {
        const uint32_t value = json["maxTemp"].as<uint32_t>(); if (value < 1 || value > 200) return fail("maxTemp", "Must be 1-200"); next.maxTemp = value;
    }

    if (json.containsKey("ina1Addr")) {
        const uint8_t value = json["ina1Addr"].as<uint8_t>(); if (!validInaAddress(value)) return fail("ina1Addr", "Must be an INA226 address (0x40-0x4F)"); next.ina1Addr = value; restartRequired = true;
    }
    if (json.containsKey("ina2Addr")) {
        const uint8_t value = json["ina2Addr"].as<uint8_t>(); if (!validInaAddress(value)) return fail("ina2Addr", "Must be an INA226 address (0x40-0x4F)"); next.ina2Addr = value; restartRequired = true;
    }
    if (json.containsKey("useAds1115")) {
        if (!json["useAds1115"].is<bool>()) return fail("useAds1115", "Must be a boolean"); next.useAds1115 = json["useAds1115"].as<bool>(); restartRequired = true;
    }
    if (json.containsKey("adsAddr")) {
        const uint8_t value = json["adsAddr"].as<uint8_t>(); if (value < 0x48 || value > 0x4B) return fail("adsAddr", "Must be an ADS1115 address (0x48-0x4B)"); next.adsAddr = value; restartRequired = true;
    }
    if (json.containsKey("dummyMode")) {
        if (!json["dummyMode"].is<bool>()) return fail("dummyMode", "Must be a boolean"); next.dummyMode = json["dummyMode"].as<bool>();
    }
    if (json.containsKey("pwrSwEn")) {
        if (!json["pwrSwEn"].is<bool>()) return fail("pwrSwEn", "Must be a boolean"); next.enablePowerSwitch = json["pwrSwEn"].as<bool>();
    }
    if (json.containsKey("pwrSwTimeout")) {
        const uint32_t value = json["pwrSwTimeout"].as<uint32_t>(); if (value < 1000 || value > 3600000) return fail("pwrSwTimeout", "Must be 1000-3600000 ms"); next.powerSwitchTimeoutMs = value;
    }

    if (next.ina1Addr == next.ina2Addr) return fail("ina2Addr", "INA226 addresses must be distinct");
    if (next.staEnabled && (!printableString(next.staSSID, 1, 32) || !printableString(next.staPass, 8, 63))) return fail("staEnabled", "Enabled STA requires SSID and password");
    if (_mutex == NULL) {
        _mutex = xSemaphoreCreateMutex();
    }
    if (_mutex == NULL || xSemaphoreTake(_mutex, portMAX_DELAY) != pdTRUE) { error = "Configuration lock unavailable"; field = "system"; return false; }
    _config = next;
    xSemaphoreGive(_mutex);
    return true;
}

bool ConfigManager::updateOffsets(float o1, float o2, float oi, float o_acs) {
    if (_mutex == NULL) {
        _mutex = xSemaphoreCreateMutex();
    }
    if (_mutex != NULL && xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
        _config.zmpt1OffsetMv = o1;
        _config.zmpt2OffsetMv = o2;
        _config.zmctOffsetMv  = oi;
        _config.acs758OffsetMv = o_acs;
        xSemaphoreGive(_mutex);
        return save();
    }
    return false;
}

void ConfigManager::serialize(JsonDocument& doc, bool includeSecrets) const {
    const SystemConfig config = getConfig();
    doc["apSsid"]     = config.apSSID;
    if (includeSecrets) doc["apPass"] = config.apPass;
    doc["apPasswordConfigured"] = strlen(config.apPass) >= 8;
    doc["staEnabled"] = config.staEnabled;
    doc["staSsid"]    = config.staSSID;
    if (includeSecrets) doc["staPass"] = config.staPass;
    doc["staPasswordConfigured"] = strlen(config.staPass) >= 8;

    doc["pollMs"]     = config.sensorPollMs;
    doc["wsPushMs"]   = config.wsPushMs;
    doc["logMs"]      = config.serialLogMs;

    doc["zmpt1Cal"]   = config.zmpt1Cal;
    doc["zmpt2Cal"]   = config.zmpt2Cal;
    doc["zmctCal"]    = config.zmctCal;
    doc["acs758Cal"]  = config.acs758Cal;
    doc["zmpt1OffsetMv"] = config.zmpt1OffsetMv;
    doc["zmpt2OffsetMv"] = config.zmpt2OffsetMv;
    doc["zmctOffsetMv"]  = config.zmctOffsetMv;
    doc["acs758OffsetMv"] = config.acs758OffsetMv;
    doc["pf"]         = config.pf;

    doc["maxV"]       = config.maxV;
    doc["maxA"]       = config.maxA;
    doc["maxAcV"]     = config.maxAcV;
    doc["maxAcA"]     = config.maxAcA;
    doc["maxRpm"]     = config.maxRpm;
    doc["maxTemp"]    = config.maxTemp;

    doc["ina1Addr"]   = config.ina1Addr;
    doc["ina2Addr"]   = config.ina2Addr;
    doc["useAds1115"] = config.useAds1115;
    doc["adsAddr"]    = config.adsAddr;
    doc["pwrSwEn"]    = config.enablePowerSwitch;
    doc["pwrSwTimeout"] = config.powerSwitchTimeoutMs;
    doc["dummyMode"]  = config.dummyMode;
    doc["setupRequired"] = config.setupRequired;
}
