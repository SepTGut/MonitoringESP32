// =============================================================
//  wifi_manager.cpp — WiFi Access Point + Station Manager
// =============================================================

#include "wifi_manager.h"
#include "../config/config.h"
#include "../system/config_manager.h"
#include <WiFi.h>

void WiFiManager::beginAP() {
    // Static AP IP for reliable captive portal
    IPAddress apIP(192, 168, 4, 1);
    IPAddress subnet(255, 255, 255, 0);

    const SystemConfig& cfg = configManager.getConfig();

    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(apIP, apIP, subnet);
    WiFi.softAP(cfg.apSSID, cfg.apPass);

    Serial.println("[WiFi] Access Point mode");
    Serial.printf("[WiFi] SSID: %s\n", cfg.apSSID);
    Serial.printf("[WiFi] AP IP: %s\n", WiFi.softAPIP().toString().c_str());
}

void WiFiManager::beginAPSTA(const char* staSSID, const char* staPass) {
    IPAddress apIP(192, 168, 4, 1);
    IPAddress subnet(255, 255, 255, 0);

    const SystemConfig& cfg = configManager.getConfig();

    WiFi.mode(WIFI_AP_STA);
    WiFi.softAPConfig(apIP, apIP, subnet);
    WiFi.softAP(cfg.apSSID, cfg.apPass);

    // Also connect to an external router
    WiFi.begin(staSSID, staPass);

    Serial.println("[WiFi] AP + STA dual mode");
    Serial.printf("[WiFi] AP SSID: %s\n", cfg.apSSID);
    Serial.printf("[WiFi] AP IP: %s\n", WiFi.softAPIP().toString().c_str());
    Serial.printf("[WiFi] Connecting to STA: %s\n", staSSID);
}

bool WiFiManager::isSTAConnected() {
    return (WiFi.status() == WL_CONNECTED);
}

IPAddress WiFiManager::getAPIP() {
    return WiFi.softAPIP();
}
