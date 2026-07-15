// =============================================================
//  freertos_tasks.cpp — FreeRTOS Task Implementations
//
//  CORE 1 (App Core) — Measurement:
//    Reads all sensors, updates DataManager, serial logging
//
//  CORE 0 (Protocol Core) — Communication:
//    WiFi, DNS captive portal, web server, WebSocket push
// =============================================================

#include "freertos_tasks.h"
#include "data_manager.h"
#include "config_manager.h"
#include "../config/config.h"
#include "../config/pin_config.h"

// Sensors
#include "../sensors/ina226_sensor.h"
#include "../sensors/ds18b20_sensor.h"
#include "../sensors/rpm_sensor.h"
#include "../sensors/zmpt101b.h"
#include "../sensors/zmct103c.h"

// Network
#include "../network/wifi_manager.h"
#include "../network/web_server.h"

#include <Wire.h>
#include <DNSServer.h>

// =============================================================
//  CORE 1 — Sensor Measurement Task
// =============================================================

// Instantiate all sensor objects (with initial config values)
static ZMPT101B    zmpt1(PIN_ZMPT101B_1, ZMPT_CALIBRATION_1);
static ZMPT101B    zmpt2(PIN_ZMPT101B_2, ZMPT_CALIBRATION_2);
static ZMCT103C    zmct(PIN_ZMCT103C, ZMCT_CALIBRATION);
static INA226Sensor ina1(INA226_ADDR_1, PIN_I2C_SDA, PIN_I2C_SCL);
static INA226Sensor ina2(INA226_ADDR_2, PIN_I2C_SDA, PIN_I2C_SCL);
static DS18B20Sensor tempBus(PIN_DS18B20);
static RPMSensor   rpmSensor(PIN_RPM_INPUT);

static void sensorTaskFunction(void* pvParameters) {
    Serial.println("[Task] Sensor task started on Core 1");

    // Initialize I2C bus once before INA226 modules
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);

    // Initialize all sensors
    zmpt1.begin();
    zmpt2.begin();
    zmct.begin();
    ina1.begin();
    ina2.begin();
    tempBus.begin();
    rpmSensor.begin();

    // Issue first DS18B20 conversion so data is ready on first read
    tempBus.requestTemperature();

    TickType_t xLastWakeTime = xTaskGetTickCount();
    uint32_t lastSerialLog = 0;

    for (;;) {
        const SystemConfig& cfg = configManager.getConfig();
        TickType_t xFrequency = pdMS_TO_TICKS(cfg.sensorPollMs);

        // Update sensor calibration values dynamically at runtime
        zmpt1.setCalibration(cfg.zmpt1Cal);
        zmpt2.setCalibration(cfg.zmpt2Cal);
        zmct.setCalibration(cfg.zmctCal);

        // --- Read DS18B20 (conversion requested in PREVIOUS cycle) ---
        float temp1 = tempBus.readTemperature(0);
        float temp2 = tempBus.readTemperature(1);

        // --- Read AC Sensors ---
        float acVoltage1 = zmpt1.readRMSVoltage();
        float acRaw1     = zmpt1.readRawADC();
        float acVoltage2 = zmpt2.readRMSVoltage();
        float acRaw2     = zmpt2.readRawADC();
        float acCurrent  = zmct.readRMSCurrent();
        float acRawI     = zmct.readRawADC();

        // --- Calculate AC Power ---
        // P = Vrms × Irms × PF (using ZMPT #1 for power calc)
        float acPower = acVoltage1 * acCurrent * cfg.pf;

        // --- Read DC Sensors ---
        float dcV1 = ina1.readVoltage();
        float dcA1 = ina1.readCurrent();
        float dcP1 = ina1.readPower();

        float dcV2 = ina2.readVoltage();
        float dcA2 = ina2.readCurrent();
        float dcP2 = ina2.readPower();

        // --- Read RPM ---
        float rpm = rpmSensor.getRPM();

        // --- Request next DS18B20 conversion (completes during vTaskDelayUntil) ---
        tempBus.requestTemperature();

        // --- Update DataManager (thread-safe) ---
        dataManager.updateACVoltage(acVoltage1, acRaw1);
        dataManager.updateACVoltage2(acVoltage2, acRaw2);
        dataManager.updateACCurrent(acCurrent, acRawI);
        dataManager.updateACPower(acPower);
        dataManager.updateDC1(dcV1, dcA1, dcP1);
        dataManager.updateDC2(dcV2, dcA2, dcP2);
        dataManager.updateTemperature1(temp1);
        dataManager.updateTemperature2(temp2);
        dataManager.updateRPM((uint32_t)rpm);

        // --- Serial Logging (Dynamic rate) ---
#if ENABLE_SERIAL_LOG
        uint32_t now = millis();
        if (now - lastSerialLog >= cfg.serialLogMs) {
            lastSerialLog = now;

            Serial.println("======================");
            Serial.println("  WIND MONITOR");
            Serial.println();
            Serial.println("  AC");
            Serial.printf("  Voltage:  %.1fV\n", acVoltage1);
            Serial.printf("  Current:  %.2fA\n", acCurrent);
            Serial.printf("  Power:    %.1fW\n", acPower);
            Serial.printf("  V2 (raw): %.1fV\n", acVoltage2);
            Serial.println();
            Serial.println("  DC INA226 #1");
            Serial.printf("  Voltage:  %.2fV\n", dcV1);
            Serial.printf("  Current:  %.2fA\n", dcA1);
            Serial.printf("  Power:    %.2fW\n", dcP1);
            Serial.println();
            Serial.println("  DC INA226 #2");
            Serial.printf("  Voltage:  %.2fV\n", dcV2);
            Serial.printf("  Current:  %.2fA\n", dcA2);
            Serial.printf("  Power:    %.2fW\n", dcP2);
            Serial.println();
            Serial.printf("  Temperature: %.1f°C / %.1f°C\n", temp1, temp2);
            Serial.printf("  RPM: %d\n", (int)rpm);
            Serial.println("======================");
        }
#endif

        // Wait for next cycle
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

// =============================================================
//  CORE 0 — Network/Communication Task
// =============================================================

static void networkTaskFunction(void* pvParameters) {
    Serial.println("[Task] Network task started on Core 0");

    const SystemConfig& cfg = configManager.getConfig();

    // Setup WiFi based on whether STA is enabled
    if (cfg.staEnabled && strlen(cfg.staSSID) > 0) {
        WiFiManager::beginAPSTA(cfg.staSSID, cfg.staPass);
    } else {
        WiFiManager::beginAP();
    }

    // Setup DNS captive portal (redirect all DNS to AP IP)
    DNSServer dnsServer;
    IPAddress apIP = WiFiManager::getAPIP();
    dnsServer.start(53, "*", apIP);
    Serial.println("[DNS] Captive portal DNS started");

    // Setup Web Server + WebSocket
    WebDashboard::begin();

    uint32_t lastWsPush = millis();

    for (;;) {
        const SystemConfig& currentCfg = configManager.getConfig();

        // Process DNS requests (high frequency for captive portal)
        dnsServer.processNextRequest();

        // Periodic WebSocket push
        uint32_t now = millis();
        if (now - lastWsPush >= currentCfg.wsPushMs) {
            WebDashboard::pushData();
            lastWsPush = now;
        }

        // Yield to other tasks without hogging the core
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

// =============================================================
//  Task Startup Functions
// =============================================================

void Tasks::startSensorTask() {
    xTaskCreatePinnedToCore(
        sensorTaskFunction,
        "SensorTask",
        4096,           // Stack size (bytes)
        NULL,           // Parameters
        1,              // Priority
        NULL,           // Task handle
        1               // Core 1 (App Core)
    );
}

void Tasks::startNetworkTask() {
    xTaskCreatePinnedToCore(
        networkTaskFunction,
        "NetworkTask",
        8192,           // Larger stack for networking
        NULL,           // Parameters
        1,              // Priority
        NULL,           // Task handle
        0               // Core 0 (Protocol Core)
    );
}
