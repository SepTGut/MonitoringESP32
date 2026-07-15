// =============================================================
//  web_server.cpp — ESPAsyncWebServer + WebSocket Dashboard
//
//  Serves the dashboard from LittleFS and pushes real-time
//  sensor data via WebSocket to connected clients.
// =============================================================

#include "web_server.h"
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "../system/data_manager.h"
#include "../system/config_manager.h"
#include "../config/config.h"

// Server and WebSocket instances
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// WebSocket event handler
void onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
               AwsEventType type, void* arg, uint8_t* data, size_t len) {
    if (type == WS_EVT_CONNECT) {
        Serial.printf("[WS] Client #%u connected from %s\n",
                      client->id(), client->remoteIP().toString().c_str());
    } else if (type == WS_EVT_DISCONNECT) {
        Serial.printf("[WS] Client #%u disconnected\n", client->id());
    }
}

void WebDashboard::begin() {
    // Mount LittleFS
    if (!LittleFS.begin(true)) {
        Serial.println("[Web] Error mounting LittleFS");
    }

    // --- Serve Static Files ---
    server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
        if (LittleFS.exists("/index.html")) {
            request->send(LittleFS, "/index.html", "text/html");
        } else {
            request->send(200, "text/html",
                "<h1>ESP32 Wind Monitor</h1>"
                "<p>index.html not found. Upload filesystem: "
                "<code>pio run -t uploadfs</code></p>");
        }
    });

    server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest* request) {
        if (LittleFS.exists("/style.css")) {
            request->send(LittleFS, "/style.css", "text/css");
        } else {
            request->send(404, "text/plain", "style.css not found");
        }
    });

    server.on("/script.js", HTTP_GET, [](AsyncWebServerRequest* request) {
        if (LittleFS.exists("/script.js")) {
            request->send(LittleFS, "/script.js", "text/javascript");
        } else {
            request->send(404, "text/plain", "script.js not found");
        }
    });

    // --- API: Get System Info ---
    server.on("/api/sysinfo", HTTP_GET, [](AsyncWebServerRequest* request) {
        AsyncResponseStream* response = request->beginResponseStream("application/json");
        StaticJsonDocument<256> doc;
        doc["fw"] = FW_VERSION;
        doc["heap"] = ESP.getFreeHeap();
        doc["uptime"] = esp_timer_get_time() / 1000000ULL;
        doc["clients"] = ws.count();
        serializeJson(doc, *response);
        request->send(response);
    });

    // --- API: Get Current Sensor Data ---
    server.on("/api/data", HTTP_GET, [](AsyncWebServerRequest* request) {
        SensorData data = dataManager.getData();

        AsyncResponseStream* response = request->beginResponseStream("application/json");
        StaticJsonDocument<512> doc;

        doc["acV"]  = data.ac_voltage;
        doc["acV2"] = data.ac_voltage2;
        doc["acA"]  = data.ac_current;
        doc["acP"]  = data.ac_power;

        doc["dcV1"] = data.ina1_voltage;
        doc["dcA1"] = data.ina1_current;
        doc["dcP1"] = data.ina1_power;

        doc["dcV2"] = data.ina2_voltage;
        doc["dcA2"] = data.ina2_current;
        doc["dcP2"] = data.ina2_power;

        doc["rpm"]  = data.rpm;
        doc["t1"]   = data.temperature1;
        doc["t2"]   = data.temperature2;

        doc["uptime"] = esp_timer_get_time() / 1000000ULL;

        serializeJson(doc, *response);
        request->send(response);
    });

    // --- API: Get Settings Config ---
    server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest* request) {
        AsyncResponseStream* response = request->beginResponseStream("application/json");
        StaticJsonDocument<1024> doc;
        configManager.serialize(doc);
        serializeJson(doc, *response);
        request->send(response);
    });

    // --- API: Save Settings Config (JSON POST) ---
    AsyncCallbackJsonWebHandler* saveConfigHandler = new AsyncCallbackJsonWebHandler("/api/config", [](AsyncWebServerRequest* request, JsonVariant& json) {
        configManager.updateFromJson(json);
        bool ok = configManager.save();

        AsyncResponseStream* response = request->beginResponseStream("application/json");
        StaticJsonDocument<128> doc;
        if (ok) {
            doc["ok"] = true;
        } else {
            doc["ok"] = false;
            doc["error"] = "Failed to save configuration file";
        }
        serializeJson(doc, *response);
        request->send(response);
    });
    server.addHandler(saveConfigHandler);

    // --- API: Restart ESP32 (POST) ---
    server.on("/api/restart", HTTP_POST, [](AsyncWebServerRequest* request) {
        request->send(200, "application/json", "{\"ok\":true}");
        
        // Spawn a background task to delay and restart, giving time to deliver response
        xTaskCreate([](void*){
            vTaskDelay(pdMS_TO_TICKS(1000));
            ESP.restart();
        }, "reboot_task", 2048, NULL, 1, NULL);
    });

    // --- WebSocket Handler ---
    ws.onEvent(onWsEvent);
    server.addHandler(&ws);

    // --- Captive Portal: redirect unknown hosts ---
    server.onNotFound([](AsyncWebServerRequest* request) {
        String host = request->host();
        if (host != "192.168.4.1" && !host.endsWith(".local")) {
            request->redirect("http://192.168.4.1/");
        } else {
            if (LittleFS.exists("/index.html")) {
                request->send(LittleFS, "/index.html", "text/html");
            } else {
                request->send(200, "text/html",
                    "<h1>ESP32 Wind Monitor</h1><p>Upload filesystem first.</p>");
            }
        }
    });

    server.begin();
    Serial.println("[Web] Server started on port 80");
}

void WebDashboard::pushData() {
    ws.cleanupClients();

    if (ws.count() > 0) {
        SensorData data = dataManager.getData();

        StaticJsonDocument<512> doc;

        doc["acV"]  = data.ac_voltage;
        doc["acV2"] = data.ac_voltage2;
        doc["acA"]  = data.ac_current;
        doc["acP"]  = data.ac_power;

        doc["dcV1"] = data.ina1_voltage;
        doc["dcA1"] = data.ina1_current;
        doc["dcP1"] = data.ina1_power;

        doc["dcV2"] = data.ina2_voltage;
        doc["dcA2"] = data.ina2_current;
        doc["dcP2"] = data.ina2_power;

        doc["rpm"]  = data.rpm;
        doc["t1"]   = data.temperature1;
        doc["t2"]   = data.temperature2;

        doc["uptime"] = esp_timer_get_time() / 1000000ULL;

        char buffer[512];
        size_t len = serializeJson(doc, buffer);
        ws.textAll(buffer, len);
    }
}
