#pragma once
// =============================================================
//  web_server.h — ESPAsyncWebServer + WebSocket Dashboard
//  Serves LittleFS files and pushes real-time sensor data
// =============================================================

#include <Arduino.h>

class WebDashboard {
public:
    // Initialize web server, WebSocket, and route handlers
    static void begin();

    // Push current sensor data to all connected WebSocket clients
    static void pushData();
};
