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
#include "../system/freertos_tasks.h"
#include "../config/config.h"
#include "../config/pin_config.h"
#include <esp_task_wdt.h>

// Server and WebSocket instances
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

static void serializeSensorData(JsonDocument& doc, const SensorData& data) {
    // AC — Generator & Inverter Output
    doc["acV"]  = data.gen_ac_voltage;     // Generator AC Voltage (ZMPT1/A0)
    doc["acV2"] = data.inv_ac_voltage;     // Inverter AC Output Voltage (ZMPT2/A1)
    doc["acA"]  = data.inv_ac_current;     // Inverter AC Load Current (ZMCT/A2)
    doc["acP"]  = data.inv_ac_power;       // Inverter AC Output Power (W)
    // DC — Battery & MPPT
    doc["dcV1"] = data.ina1_voltage; doc["dcA1"] = data.ina1_current; doc["dcP1"] = data.ina1_power;
    doc["soc"]  = data.battery_soc; doc["wh"] = data.battery_wh;
    // DC — Inverter Input (ACS758 50A)
    doc["invA"] = data.inverter_current; doc["invP"] = data.inverter_power;
    doc["invEff"] = data.inverter_efficiency;
    // DC — Control & Lights (INA226 #2)
    doc["ctrlV"] = data.ina2_voltage; doc["ctrlA"] = data.ina2_current; doc["ctrlP"] = data.ina2_power;
    // Mechanical & Thermal
    doc["rpm"] = data.rpm;
    doc["t1"] = data.temperature1; doc["t2"] = data.temperature2; doc["tEsp"] = data.temperature_esp;
    // Meta
    doc["seq"] = data.sequence; doc["cyc"] = data.cycleMs; doc["ovr"] = data.overruns;
    JsonObject health = doc.createNestedObject("health");
    health["acV1"] = (data.health & SensorData::HEALTH_AC_V1) != 0;
    health["acV2"] = (data.health & SensorData::HEALTH_AC_V2) != 0;
    health["acI"] = (data.health & SensorData::HEALTH_AC_I) != 0;
    health["ina1"] = (data.health & SensorData::HEALTH_INA1) != 0;
    health["ina2"] = (data.health & SensorData::HEALTH_INA2) != 0;
    health["acs758"] = (data.health & SensorData::HEALTH_ACS758) != 0;
    health["ads1115"] = (data.health & SensorData::HEALTH_ADS1115) != 0;
    health["temp1"] = (data.health & SensorData::HEALTH_TEMP1) != 0;
    health["temp2"] = (data.health & SensorData::HEALTH_TEMP2) != 0;
    health["cpuTemp"] = (data.health & SensorData::HEALTH_CPU_TEMP) != 0;
    health["rpm"] = (data.health & SensorData::HEALTH_RPM) != 0;
}

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
        request->send(LittleFS, "/index.html", "text/html");
    });
    server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(LittleFS, "/style.css", "text/css");
    });
    server.on("/script.js", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(LittleFS, "/script.js", "text/javascript");
    });

    // --- API: Get System Info ---
    server.on("/api/sysinfo", HTTP_GET, [](AsyncWebServerRequest* request) {
        SensorData data = dataManager.getData();
        AsyncResponseStream* response = request->beginResponseStream("application/json");
        StaticJsonDocument<1024> doc;
        doc["fw"] = FW_VERSION;
        doc["heap"] = ESP.getFreeHeap();
        doc["minHeap"] = ESP.getMinFreeHeap();
        doc["uptime"] = esp_timer_get_time() / 1000000ULL;
        doc["clients"] = ws.count();
        doc["cycleMs"] = data.cycleMs;
        doc["adcMode"] = configManager.getConfig().useAds1115 ? "ADS1115 16-Bit (400kHz Fast I2C, ALRT: GPIO 19)" : "Internal (eFuse Calibrated)";
        doc["i2cClock"] = "400 kHz Fast-Mode";
        doc["adsAlertPin"] = PIN_ADS1115_ALERT;

        // Task stack high water marks (minimum free stack in words → bytes)
        TaskHandle_t sensorHandle = Tasks::getSensorTaskHandle();
        TaskHandle_t networkHandle = Tasks::getNetworkTaskHandle();
        if (sensorHandle) doc["sensorStackFree"] = uxTaskGetStackHighWaterMark(sensorHandle) * 4;
        if (networkHandle) doc["networkStackFree"] = uxTaskGetStackHighWaterMark(networkHandle) * 4;

        JsonArray i2cArr = doc.createNestedArray("i2c");
        for (uint8_t i = 0; i < data.i2c_count; i++) {
            i2cArr.add(data.i2c_addresses[i]);
        }

        serializeJson(doc, *response);
        request->send(response);
    });

    // --- API: Get Current Sensor Data ---
    server.on("/api/data", HTTP_GET, [](AsyncWebServerRequest* request) {
        SensorData data = dataManager.getData();

        AsyncResponseStream* response = request->beginResponseStream("application/json");
        StaticJsonDocument<1024> doc;

        serializeSensorData(doc, data);

        doc["uptime"] = esp_timer_get_time() / 1000000ULL;
        doc["setupRequired"] = configManager.getConfig().setupRequired;

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
        String error, field;
        bool restartRequired = false;
        if (!configManager.updateFromJson(json, error, field, restartRequired)) {
            StaticJsonDocument<192> errorDoc;
            errorDoc["ok"] = false; errorDoc["error"] = error; errorDoc["field"] = field;
            String body; serializeJson(errorDoc, body);
            request->send(400, "application/json", body);
            return;
        }
        // Audit log — never log passwords or request bodies
        Serial.printf("[Web] Configuration update accepted (restartRequired=%s)\n",
                      restartRequired ? "true" : "false");
        bool ok = configManager.save();

        AsyncResponseStream* response = request->beginResponseStream("application/json");
        StaticJsonDocument<128> doc;
        if (ok) {
            doc["ok"] = true;
            doc["restartRequired"] = restartRequired;
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
        // Block restart if setup has not been completed
        if (configManager.getConfig().setupRequired) {
            request->send(403, "application/json",
                "{\"ok\":false,\"error\":\"Complete setup before restarting\"}");
            return;
        }
        request->send(200, "application/json", "{\"ok\":true}");
        
        // Spawn a background task to delay and restart, giving time to deliver response
        xTaskCreate([](void*){
            vTaskDelay(pdMS_TO_TICKS(1000));
            ESP.restart();
        }, "reboot_task", 2048, NULL, 1, NULL);
    });

    // --- API: Trigger On-Demand I2C Scan (POST) ---
    server.on("/api/i2c-scan", HTTP_POST, [](AsyncWebServerRequest* request) {
        Tasks::requestI2CScan();
        request->send(200, "application/json", "{\"ok\":true}");
    });

    // --- API: Trigger ADC Zero-Point Calibration (POST) ---
    server.on("/api/adc-calibrate", HTTP_POST, [](AsyncWebServerRequest* request) {
        Tasks::requestAdcCalibration();
        request->send(200, "application/json", "{\"ok\":true}");
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

        StaticJsonDocument<1024> doc;

        serializeSensorData(doc, data);

        doc["uptime"] = esp_timer_get_time() / 1000000ULL;

        char buffer[1024];
        size_t len = serializeJson(doc, buffer);
        ws.textAll(buffer, len);
    }
}
