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
#include "../display/lcd_display.h"

// Network
#include "../network/wifi_manager.h"
#include "../network/web_server.h"

#include <Wire.h>
#include <DNSServer.h>

// Safe dynamic cross-core flag for I2C scans (Core 0 to Core 1)
static volatile bool i2cScanRequested = false;

// =============================================================
//  CORE 1 — Sensor Measurement Task
// =============================================================

// Instantiate all sensor objects (with initial config values)
static ZMPT101B    zmpt1(PIN_ZMPT101B_1, ZMPT_CALIBRATION_1);
static ZMPT101B    zmpt2(PIN_ZMPT101B_2, ZMPT_CALIBRATION_2);
static ZMCT103C    zmct(PIN_ZMCT103C, ZMCT_CALIBRATION);
static DS18B20Sensor tempBus(PIN_DS18B20);
static RPMSensor   rpmSensor(PIN_RPM_INPUT);
static LcdDisplay   lcdDisplay;

// Dynamic pointer allocation for I2C INA226 modules to support auto-discovery and soft-assignment
static INA226Sensor* ina1 = nullptr;
static INA226Sensor* ina2 = nullptr;

static void sensorTaskFunction(void* pvParameters) {
    Serial.println("[Task] Sensor task started on Core 1");

    // Initialize I2C bus once before scanning
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);

    Serial.println("[I2C] Scanning I2C bus for connected devices...");
    uint8_t i2c_list[16];
    uint8_t i2c_count = 0;
    uint8_t lcdAddr = 0;

    for (uint8_t addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            Serial.printf("[I2C] Found responsive device at address 0x%02X\n", addr);
            if (i2c_count < 16) {
                i2c_list[i2c_count++] = addr;
            }
            
            // Check for LCD (0x20-0x27 or 0x38-0x3F)
            if ((addr >= 0x20 && addr <= 0x27) || (addr >= 0x38 && addr <= 0x3F)) {
                if (lcdAddr == 0) lcdAddr = addr;
            }
        }
    }

    // Save detected I2C list to DataManager (for Web Dashboard UI)
    dataManager.updateI2CAddresses(i2c_list, i2c_count);

    // Initialize LCD display with detected address (disabled if 0)
    lcdDisplay.begin(lcdAddr);

    // Initialize non-I2C sensors
    zmpt1.begin();
    zmpt2.begin();
    zmct.begin();
    tempBus.begin();
    rpmSensor.begin();

    // Fetch config configuration and assign INA226 devices based on software settings
    const SystemConfig& cfg = configManager.getConfig();
    
    // Check if INA226 #1 address is present on the bus
    bool ina1Present = false;
    bool ina2Present = false;
    for (uint8_t i = 0; i < i2c_count; i++) {
        if (i2c_list[i] == cfg.ina1Addr) ina1Present = true;
        if (i2c_list[i] == cfg.ina2Addr) ina2Present = true;
    }

    if (ina1Present) {
        Serial.printf("[INA226] Dynamic assignment: INA1 mapped to address 0x%02X\n", cfg.ina1Addr);
        ina1 = new INA226Sensor(cfg.ina1Addr, PIN_I2C_SDA, PIN_I2C_SCL);
        ina1->begin();
    } else {
        Serial.printf("[INA226] Warning: INA1 address 0x%02X NOT found on I2C bus. Sensor disabled.\n", cfg.ina1Addr);
    }

    if (ina2Present) {
        Serial.printf("[INA226] Dynamic assignment: INA2 mapped to address 0x%02X\n", cfg.ina2Addr);
        ina2 = new INA226Sensor(cfg.ina2Addr, PIN_I2C_SDA, PIN_I2C_SCL);
        ina2->begin();
    } else {
        Serial.printf("[INA226] Warning: INA2 address 0x%02X NOT found on I2C bus. Sensor disabled.\n", cfg.ina2Addr);
    }

    // Issue first DS18B20 conversion so data is ready on first read
    tempBus.requestTemperature();

    TickType_t xLastWakeTime = xTaskGetTickCount();
    uint32_t lastSerialLog = 0;

    for (;;) {
        const SystemConfig& currentCfg = configManager.getConfig();
        TickType_t xFrequency = pdMS_TO_TICKS(currentCfg.sensorPollMs);

        // --- On-Demand I2C Scan Request ---
        if (i2cScanRequested) {
            i2cScanRequested = false;
            Serial.println("[I2C] Running on-demand bus scan...");
            uint8_t i2c_list[16];
            uint8_t i2c_count = 0;
            for (uint8_t addr = 1; addr < 127; addr++) {
                Wire.beginTransmission(addr);
                if (Wire.endTransmission() == 0) {
                    if (i2c_count < 16) {
                        i2c_list[i2c_count++] = addr;
                    }
                }
            }
            dataManager.updateI2CAddresses(i2c_list, i2c_count);
            Serial.printf("[I2C] On-demand scan complete. Found %d devices.\n", i2c_count);
        }

        // Update sensor calibration values dynamically at runtime
        zmpt1.setCalibration(currentCfg.zmpt1Cal);
        zmpt2.setCalibration(currentCfg.zmpt2Cal);
        zmct.setCalibration(currentCfg.zmctCal);

        // --- Read/Simulate Sensors ---
        float temp1, temp2;
        float acVoltage1, acRaw1, acVoltage2, acRaw2, acCurrent, acRawI, acPower;
        float dcV1, dcA1, dcP1, dcV2, dcA2, dcP2;
        float rpm;

        if (currentCfg.dummyMode) {
            // Simulated Dummy Sensors Mode
            static float simStep = 0.0f;
            simStep += 0.08f;

            float windSpeed = 5.0f + 3.0f * sin(simStep * 0.3f);
            acVoltage1 = 30.0f + 25.0f * sin(simStep * 0.5f) * (windSpeed / 8.0f);
            acRaw1     = acVoltage1 * 10.0f; // Fake raw ADC representation
            acVoltage2 = acVoltage1 * 0.95f + 1.5f * sin(simStep * 1.1f);
            acRaw2     = acVoltage2 * 10.0f;
            acCurrent  = 2.0f + 1.5f * sin(simStep * 0.7f) * (windSpeed / 8.0f);
            if (acCurrent < 0.0f) acCurrent = 0.0f;
            acRawI     = acCurrent * 100.0f;
            acPower    = acVoltage1 * acCurrent * currentCfg.pf;

            dcV1 = 12.0f + 2.0f * sin(simStep * 0.4f);
            dcA1 = 3.0f + 2.0f * sin(simStep * 0.6f);
            if (dcA1 < 0.0f) dcA1 = 0.0f;
            dcP1 = dcV1 * dcA1;

            dcV2 = 24.0f + 3.0f * sin(simStep * 0.35f);
            dcA2 = 1.5f + 1.0f * sin(simStep * 0.55f);
            if (dcA2 < 0.0f) dcA2 = 0.0f;
            dcP2 = dcV2 * dcA2;

            rpm  = 800.0f + 600.0f * sin(simStep * 0.25f) * (windSpeed / 8.0f);
            if (rpm < 0.0f) rpm = 0.0f;

            temp1 = 32.0f + 4.0f * sin(simStep * 0.15f);
            temp2 = 26.0f + 2.0f * sin(simStep * 0.1f);
        } else {
            // --- Read DS18B20 (conversion requested in PREVIOUS cycle) ---
            temp1 = tempBus.readTemperature(0);
            temp2 = tempBus.readTemperature(1);

            // --- Read AC Sensors ---
            acVoltage1 = zmpt1.readRMSVoltage();
            acRaw1     = zmpt1.readRawADC();
            acVoltage2 = zmpt2.readRMSVoltage();
            acRaw2     = zmpt2.readRawADC();
            acCurrent  = zmct.readRMSCurrent();
            acRawI     = zmct.readRawADC();

            // --- Calculate AC Power ---
            acPower = acVoltage1 * acCurrent * currentCfg.pf;

            // --- Read DC Sensors (nullptr-safe) ---
            dcV1 = (ina1 != nullptr) ? ina1->readVoltage() : 0.0f;
            dcA1 = (ina1 != nullptr) ? ina1->readCurrent() : 0.0f;
            dcP1 = (ina1 != nullptr) ? ina1->readPower() : 0.0f;

            dcV2 = (ina2 != nullptr) ? ina2->readVoltage() : 0.0f;
            dcA2 = (ina2 != nullptr) ? ina2->readCurrent() : 0.0f;
            dcP2 = (ina2 != nullptr) ? ina2->readPower() : 0.0f;

            // --- Read RPM ---
            rpm = rpmSensor.getRPM();
        }

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
        if (now - lastSerialLog >= currentCfg.serialLogMs) {
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

        // --- Update LCD Display (with dynamic screen rotation) ---
        lcdDisplay.update(dataManager.getData());

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

void Tasks::requestI2CScan() {
    i2cScanRequested = true;
}
