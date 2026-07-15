#pragma once
// =============================================================
//  wifi_manager.h — WiFi Access Point + Station Manager
//  Supports: AP mode, STA mode, AP+STA dual mode
// =============================================================

#include <Arduino.h>

class WiFiManager {
public:
    // Setup WiFi in AP mode (creates hotspot)
    static void beginAP();

    // Setup WiFi in AP+STA mode (hotspot + connect to router)
    static void beginAPSTA(const char* staSSID, const char* staPass);

    // Check if STA is connected to a router
    static bool isSTAConnected();

    // Get AP IP address
    static IPAddress getAPIP();
};
