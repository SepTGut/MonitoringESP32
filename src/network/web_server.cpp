#include "web_server.h"
#include "wifi_manager.h"
#include "web_assets.h"
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

// Helper to stream file from LittleFS if present, otherwise from Flash PROGMEM (GZIP)
static void sendGzipOrFile(AsyncWebServerRequest* request, const char* path, const char* mime, const uint8_t* embeddedGz, size_t embeddedLen) {
    if (LittleFS.exists(path)) {
        request->send(LittleFS, path, mime);
        return;
    }
    if (embeddedGz != nullptr && embeddedLen > 0) {
        AsyncWebServerResponse* response = request->beginResponse_P(200, mime, embeddedGz, embeddedLen);
        response->addHeader("Content-Encoding", "gzip");
        response->addHeader("Cache-Control", "public, max-age=3600");
        request->send(response);
        return;
    }
    request->send(404, "text/plain", "Asset not found");
}

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
    // Mount LittleFS without formatting on fail so flashed files are preserved
    if (!LittleFS.begin(false)) {
        Serial.println("[Web] Notice: LittleFS not mounted; serving built-in Flash dashboard (100% available)");
    } else {
        Serial.println("[Web] LittleFS mounted successfully");
    }

    // --- Global CORS Headers ---
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type");

    // --- Serve Static Files (LittleFS override with embedded GZIP fallback) ---
    server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
        sendGzipOrFile(request, "/index.html", "text/html", INDEX_HTML_GZ, INDEX_HTML_GZ_LEN);
    });
    server.on("/index.html", HTTP_GET, [](AsyncWebServerRequest* request) {
        sendGzipOrFile(request, "/index.html", "text/html", INDEX_HTML_GZ, INDEX_HTML_GZ_LEN);
    });
    server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest* request) {
        sendGzipOrFile(request, "/style.css", "text/css", STYLE_CSS_GZ, STYLE_CSS_GZ_LEN);
    });
    server.on("/script.js", HTTP_GET, [](AsyncWebServerRequest* request) {
        sendGzipOrFile(request, "/script.js", "application/javascript", SCRIPT_JS_GZ, SCRIPT_JS_GZ_LEN);
    });
    server.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest* request) {
        sendGzipOrFile(request, "/favicon.ico", "image/x-icon", FAVICON_ICO_GZ, FAVICON_ICO_GZ_LEN);
    });
    server.on("/web_serial_plotter.html", HTTP_GET, [](AsyncWebServerRequest* request) {
        if (LittleFS.exists("/web_serial_plotter.html")) {
            request->send(LittleFS, "/web_serial_plotter.html", "text/html");
        } else {
            sendGzipOrFile(request, "/index.html", "text/html", INDEX_HTML_GZ, INDEX_HTML_GZ_LEN);
        }
    });

    // --- Captive Portal Probe Endpoints (Android / Apple / Windows) ---
    auto sendCaptiveRedirect = [](AsyncWebServerRequest* request) {
        request->redirect("http://192.168.4.1/");
    };
    server.on("/generate_204", HTTP_GET, sendCaptiveRedirect);
    server.on("/gen_204", HTTP_GET, sendCaptiveRedirect);
    server.on("/hotspot-detect.html", HTTP_GET, sendCaptiveRedirect);
    server.on("/canonical.html", HTTP_GET, sendCaptiveRedirect);
    server.on("/success.txt", HTTP_GET, [](AsyncWebServerRequest* request) { request->send(200, "text/plain", "success\n"); });
    server.on("/connecttest.txt", HTTP_GET, sendCaptiveRedirect);
    server.on("/ncsi.txt", HTTP_GET, sendCaptiveRedirect);

    // Serve all other LittleFS assets if available
    server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

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
        if (request->method() == HTTP_OPTIONS) {
            request->send(200);
            return;
        }

        String host = request->host();
        IPAddress apIP = WiFiManager::getAPIP();
        String apIPStr = apIP.toString();

        // Redirect external domains probed during captive portal discovery to AP IP
        if (host.indexOf(apIPStr) == -1 && !host.endsWith(".local") && !WiFiManager::isSTAConnected()) {
            request->redirect("http://" + apIPStr + "/");
        } else {
            sendGzipOrFile(request, "/index.html", "text/html", INDEX_HTML_GZ, INDEX_HTML_GZ_LEN);
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
