// =============================================================
//  wifi_manager.cpp — WiFi Access Point + Station Manager
// =============================================================

#include "wifi_manager.h"
#include "../config/config.h"
#include <WiFi.h>

void WiFiManager::beginAP() {
    // Static AP IP for reliable captive portal
    IPAddress apIP(192, 168, 4, 1);
    IPAddress subnet(255, 255, 255, 0);

    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(apIP, apIP, subnet);
    WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASS);

    Serial.println("[WiFi] Access Point mode");
    Serial.printf("[WiFi] SSID: %s\n", WIFI_AP_SSID);
    Serial.printf("[WiFi] AP IP: %s\n", WiFi.softAPIP().toString().c_str());
}

void WiFiManager::beginAPSTA(const char* staSSID, const char* staPass) {
    IPAddress apIP(192, 168, 4, 1);
    IPAddress subnet(255, 255, 255, 0);

    WiFi.mode(WIFI_AP_STA);
    WiFi.softAPConfig(apIP, apIP, subnet);
    WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASS);

    // Also connect to an external router
    WiFi.begin(staSSID, staPass);

    Serial.println("[WiFi] AP + STA dual mode");
    Serial.printf("[WiFi] AP SSID: %s\n", WIFI_AP_SSID);
    Serial.printf("[WiFi] AP IP: %s\n", WiFi.softAPIP().toString().c_str());
    Serial.printf("[WiFi] Connecting to STA: %s\n", staSSID);
}

bool WiFiManager::isSTAConnected() {
    return (WiFi.status() == WL_CONNECTED);
}

IPAddress WiFiManager::getAPIP() {
    return WiFi.softAPIP();
}
