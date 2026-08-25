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
#include "../sensors/ads1115_sensor.h"
#include "../sensors/acs758_sensor.h"
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

// Cross-core I2C scan request state (Core 0 to Core 1).
static volatile bool i2cScanRequested = false;
static portMUX_TYPE i2cScanMux = portMUX_INITIALIZER_UNLOCKED;

static volatile bool adcCalRequested = false;
static portMUX_TYPE adcCalMux = portMUX_INITIALIZER_UNLOCKED;

// Task handles for stack watermark diagnostics
static TaskHandle_t sensorTaskHandle = NULL;
static TaskHandle_t networkTaskHandle = NULL;

#ifdef __cplusplus
extern "C" {
#endif
uint8_t temprature_sens_read();
#ifdef __cplusplus
}
#endif

// Calibrated SoC curve for Lakoni Blue Wolf 12V 65Ah (75D23L, 57% SoH / 550 CCA)
// Cutoff: <= 11.85V -> 0.0%, >= 12.75V -> 100.0%
static float calculateBatterySoC(float v) {
    if (v <= 11.85f) return 0.0f;
    if (v >= 12.75f) return 100.0f;

    static const struct { float v; float soc; } socTable[] = {
        {11.85f,   0.0f},
        {11.95f,  10.0f},
        {12.05f,  25.0f},
        {12.15f,  38.0f},
        {12.25f,  50.0f},
        {12.38f,  65.0f},
        {12.50f,  75.0f},
        {12.62f,  88.0f},
        {12.75f, 100.0f}
    };
    constexpr size_t nPoints = sizeof(socTable) / sizeof(socTable[0]);

    for (size_t i = 0; i < nPoints - 1; i++) {
        if (v >= socTable[i].v && v <= socTable[i + 1].v) {
            float slope = (socTable[i + 1].soc - socTable[i].soc) / (socTable[i + 1].v - socTable[i].v);
            return socTable[i].soc + slope * (v - socTable[i].v);
        }
    }
    return 100.0f;
}

// =============================================================
//  CORE 1 — Sensor Measurement Task
// =============================================================

// Instantiate all sensor objects (with initial config values)
static ZMPT101B     zmpt1(PIN_ZMPT101B_1, ZMPT_CALIBRATION_1);
static ZMPT101B     zmpt2(PIN_ZMPT101B_2, ZMPT_CALIBRATION_2);
static ZMCT103C     zmct(PIN_ZMCT103C, ZMCT_CALIBRATION);
static ACS758Sensor acs758(PIN_ACS758_INPUT, ACS758_SENSITIVITY);
static DS18B20Sensor tempBus(PIN_DS18B20);
static RPMSensor    rpmSensor(PIN_RPM_INPUT);
static LcdDisplay   lcdDisplay;

// Dynamic pointer allocation for I2C INA226 modules and optional ADS1115 module
static INA226Sensor* ina1 = nullptr;
static INA226Sensor* ina2 = nullptr;
static ADS1115Sensor* ads  = nullptr;

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
    SystemConfig cfg = configManager.getConfig();
    
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

    if (cfg.useAds1115) {
        bool adsPresent = false;
        for (uint8_t i = 0; i < i2c_count; i++) {
            if (i2c_list[i] == cfg.adsAddr) adsPresent = true;
        }
        if (adsPresent) {
            Serial.printf("[ADS1115] Dynamic assignment: 16-bit ADC mapped to address 0x%02X\n", cfg.adsAddr);
            ads = new ADS1115Sensor(cfg.adsAddr);
            ads->begin();
            // Auto-calibrate baseline offsets via ADS1115
            zmpt1.calibrateZeroOffset(ads, ADS1115_CH_ZMPT1);
            zmpt2.calibrateZeroOffset(ads, ADS1115_CH_ZMPT2);
            zmct.calibrateZeroOffset(ads, ADS1115_CH_ZMCT);
            acs758.calibrateZeroOffset(ads, ADS1115_CH_ACS758);
        } else {
            Serial.printf("[ADS1115] Warning: ADS1115 address 0x%02X NOT found on I2C bus. Falling back to internal ADC.\n", cfg.adsAddr);
            acs758.begin();
        }
    } else {
        acs758.begin();
    }

    // Issue first DS18B20 conversion so data is ready on first read
    tempBus.requestTemperature();

    TickType_t xLastWakeTime = xTaskGetTickCount();
    uint32_t lastSerialLog = 0;

    for (;;) {
        SystemConfig currentCfg = configManager.getConfig();
        TickType_t xFrequency = pdMS_TO_TICKS(currentCfg.sensorPollMs);

        // --- On-Demand I2C Scan Request ---
        bool runI2cScan = false;
        portENTER_CRITICAL(&i2cScanMux);
        runI2cScan = i2cScanRequested;
        i2cScanRequested = false;
        portEXIT_CRITICAL(&i2cScanMux);

        if (runI2cScan) {
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

        // Sync calibration multipliers and baseline zero offsets from config
        zmpt1.setCalibration(currentCfg.zmpt1Cal);
        zmpt2.setCalibration(currentCfg.zmpt2Cal);
        zmct.setCalibration(currentCfg.zmctCal);

        if (currentCfg.zmpt1OffsetMv >= 500.0f) zmpt1.setOffset(currentCfg.zmpt1OffsetMv);
        if (currentCfg.zmpt2OffsetMv >= 500.0f) zmpt2.setOffset(currentCfg.zmpt2OffsetMv);
        if (currentCfg.zmctOffsetMv >= 500.0f)  zmct.setOffset(currentCfg.zmctOffsetMv);

        // Check if dynamic ADC zero-point calibration was requested from Core 0
        bool runCal = false;
        portENTER_CRITICAL(&adcCalMux);
        if (adcCalRequested) {
            runCal = true;
            adcCalRequested = false;
        }
        portEXIT_CRITICAL(&adcCalMux);

        if (runCal) {
            Serial.println("[ADC Cal] Executing zero-point baseline offset calibration on Core 1...");
            const bool useAds = (currentCfg.useAds1115 && ads != nullptr && ads->isEnabled());
            float o1 = zmpt1.calibrateZeroOffset(useAds ? ads : nullptr, ADS1115_CH_ZMPT1);
            float o2 = zmpt2.calibrateZeroOffset(useAds ? ads : nullptr, ADS1115_CH_ZMPT2);
            float oi = zmct.calibrateZeroOffset(useAds ? ads : nullptr, ADS1115_CH_ZMCT);
            acs758.calibrateZeroOffset(useAds ? ads : nullptr, ADS1115_CH_ACS758);
            configManager.updateOffsets(o1, o2, oi);
            Serial.printf("[ADC Cal] Baseline zero offsets saved: ZMPT1=%.1fmV, ZMPT2=%.1fmV, ZMCT=%.1fmV\n", o1, o2, oi);
        }

        // --- Read/Simulate Sensors ---
        const uint32_t cycleStartedAt = millis();
        float temp1 = 0.0f, temp2 = 0.0f, tempEsp = 0.0f;
        float acVoltage1, acRaw1, acVoltage2, acRaw2, acCurrent, acRawI, acPower;
        float invCurrent = 0.0f, invRawMv = 0.0f, invPower = 0.0f;
        float dcV1, dcA1, dcP1, dcV2, dcA2, dcP2;
        float rpm;

        bool temp1Valid = false;
        bool temp2Valid = false;
        if (currentCfg.dummyMode) {
            // Simulated Dummy Sensors Mode
            static float simStep = 0.0f;
            simStep += 0.08f;

            // Generator AC voltage (raw variable wind output before MPPT)
            float windSpeed = 5.0f + 3.0f * sin(simStep * 0.3f);
            acVoltage1 = 30.0f + 25.0f * sin(simStep * 0.5f) * (windSpeed / 8.0f);
            acRaw1     = acVoltage1 * 10.0f;

            // Inverter AC output (220V household load side)
            acVoltage2 = 218.0f + 6.0f * sin(simStep * 0.15f); // ~218-224V AC
            acRaw2     = acVoltage2 * 10.0f;
            acCurrent  = 0.5f + 0.4f * sin(simStep * 0.7f) * (windSpeed / 8.0f);
            if (acCurrent < 0.0f) acCurrent = 0.0f;
            acRawI     = acCurrent * 100.0f;
            acPower    = acVoltage2 * acCurrent * currentCfg.pf; // Inverter AC Output Power

            // Battery / MPPT Charging (INA226 #1)
            dcV1 = 12.0f + 0.8f * sin(simStep * 0.4f);
            dcA1 = 1.5f + 1.2f * sin(simStep * 0.6f);
            if (dcA1 < 0.0f) dcA1 = 0.0f;
            dcP1 = dcV1 * dcA1;

            // Inverter DC discharge (ACS758 50A)
            invCurrent = 8.5f + 5.0f * sin(simStep * 0.45f);
            if (invCurrent < 0.0f) invCurrent = 0.0f;
            invPower = dcV1 * invCurrent;
            invRawMv = 2500.0f + (invCurrent * 40.0f);

            // Control / Light power simulation (INA226 #2 before DC-DC)
            dcV2 = dcV1;
            dcA2 = 0.45f + 0.15f * sin(simStep * 0.2f);
            dcP2 = dcV2 * dcA2;

            rpm  = 800.0f + 600.0f * sin(simStep * 0.25f) * (windSpeed / 8.0f);
            if (rpm < 0.0f) rpm = 0.0f;

            temp1 = 32.0f + 4.0f * sin(simStep * 0.15f);
            temp2 = 26.0f + 2.0f * sin(simStep * 0.1f);
            tempEsp = 45.0f + 5.0f * sin(simStep * 0.2f);
            temp1Valid = true;
            temp2Valid = true;
        } else {
            // --- Read DS18B20 (conversion requested in PREVIOUS cycle) ---
            temp1Valid = tempBus.readTemperature(0, temp1);
            temp2Valid = tempBus.readTemperature(1, temp2);
            tempEsp = (temprature_sens_read() - 32.0f) / 1.8f;

            // --- Read Analog Sensors (ADS1115 16-Bit I2C ADC vs Internal ADC) ---
            if (currentCfg.useAds1115 && ads != nullptr && ads->isEnabled()) {
                acVoltage1 = zmpt1.readRMSVoltage(ads, ADS1115_CH_ZMPT1); // ADS1115 Ch0 -> ZMPT1
                acRaw1     = zmpt1.readRawADC();
                acVoltage2 = zmpt2.readRMSVoltage(ads, ADS1115_CH_ZMPT2); // ADS1115 Ch1 -> ZMPT2
                acRaw2     = zmpt2.readRawADC();
                acCurrent  = zmct.readRMSCurrent(ads, ADS1115_CH_ZMCT);   // ADS1115 Ch2 -> ZMCT
                acRawI     = zmct.readRawADC();
                invCurrent = acs758.readCurrent(ads, ADS1115_CH_ACS758); // ADS1115 Ch3 -> ACS758 50A
                invRawMv   = acs758.readRawMilliVolts(ads, ADS1115_CH_ACS758);
            } else {
                acVoltage1 = zmpt1.readRMSVoltage();                     // Internal ESP32 ADC
                acRaw1     = zmpt1.readRawADC();
                acVoltage2 = zmpt2.readRMSVoltage();
                acRaw2     = zmpt2.readRawADC();
                acCurrent  = zmct.readRMSCurrent();
                acRawI     = zmct.readRawADC();
                invCurrent = acs758.readCurrent();
                invRawMv   = acs758.readRawMilliVolts();
            }

            // --- Calculate Inverter AC Output Power & Inverter DC Input Power ---
            // ZMPT1 (A0): Generator AC Voltage (before MPPT)
            // ZMPT2 (A1): Inverter AC Output Voltage (220V)
            // ZMCT  (A2): Inverter AC Load Current
            // ACS758(A3): Inverter DC Input Discharge Current
            acPower = acVoltage2 * acCurrent * currentCfg.pf; // Inverter AC Output Power (W)

            // --- Read DC Sensors (nullptr-safe) ---
            // INA226 #1: Battery & MPPT Charging
            dcV1 = (ina1 != nullptr) ? ina1->readVoltage() : 0.0f;
            dcA1 = (ina1 != nullptr) ? ina1->readCurrent() : 0.0f;
            dcP1 = (ina1 != nullptr) ? ina1->readPower() : 0.0f;

            // ACS758 50A: Inverter DC Input Power = V_battery × I_inverter
            invPower = dcV1 * fabsf(invCurrent);

            // INA226 #2: ESP32, Control & 12V Lighting Aux Power (before DC-DC)
            dcV2 = (ina2 != nullptr) ? ina2->readVoltage() : 0.0f;
            dcA2 = (ina2 != nullptr) ? ina2->readCurrent() : 0.0f;
            dcP2 = (ina2 != nullptr) ? ina2->readPower() : 0.0f;

            // --- Read RPM ---
            rpm = rpmSensor.getRPM();
        }

        // --- Request next DS18B20 conversion (completes during vTaskDelayUntil) ---
        if (!currentCfg.dummyMode) {
            tempBus.requestTemperature();
        }

        // Publish one complete, internally consistent measurement frame.
        static uint32_t sequence = 0;
        static uint32_t overruns = 0;
        SensorData frame = {};
        frame.zmpt1_adc = acRaw1; frame.zmpt2_adc = acRaw2; frame.zmct_adc = acRawI;
        frame.acs758_adc = invRawMv;
        frame.gen_ac_voltage = acVoltage1;            // Generator AC (ZMPT1)
        frame.inv_ac_voltage = acVoltage2;            // Inverter AC Output (ZMPT2)
        frame.inv_ac_current = acCurrent;             // Inverter AC Load Current (ZMCT)
        frame.inv_ac_power = acPower;                 // Inverter AC Output Power (W)
        frame.ina1_voltage = dcV1; frame.ina1_current = dcA1; frame.ina1_power = dcP1;
        frame.battery_soc = calculateBatterySoC(dcV1);
        frame.battery_wh = (frame.battery_soc / 100.0f) * (12.0f * 39.00f); // 468.00 Wh effective (65Ah @ 60% SoH)
        frame.inverter_current = invCurrent; frame.inverter_power = invPower;
        frame.inverter_efficiency = (invPower > 5.0f && acPower > 0.0f) ? min(100.0f, (acPower / invPower) * 100.0f) : 0.0f;
        frame.ina2_voltage = dcV2; frame.ina2_current = dcA2; frame.ina2_power = dcP2;
        frame.temperature1 = temp1; frame.temperature2 = temp2; frame.temperature_esp = tempEsp;
        frame.rpm = static_cast<uint32_t>(rpm);
        frame.health = SensorData::HEALTH_AC_V1 | SensorData::HEALTH_AC_V2 |
                       SensorData::HEALTH_AC_I | SensorData::HEALTH_CPU_TEMP |
                       SensorData::HEALTH_RPM | SensorData::HEALTH_ACS758;
        if (temp1Valid) frame.health |= SensorData::HEALTH_TEMP1;
        if (temp2Valid) frame.health |= SensorData::HEALTH_TEMP2;
        if (currentCfg.useAds1115 && ads != nullptr && ads->isEnabled()) frame.health |= SensorData::HEALTH_ADS1115;
        if (currentCfg.dummyMode || (ina1 != nullptr && ina1->isEnabled())) frame.health |= SensorData::HEALTH_INA1;
        if (currentCfg.dummyMode || (ina2 != nullptr && ina2->isEnabled())) frame.health |= SensorData::HEALTH_INA2;
        frame.sequence = ++sequence;

        // --- Serial Logging (Dynamic rate) ---
#if ENABLE_SERIAL_LOG
        uint32_t now = millis();
        if (now - lastSerialLog >= currentCfg.serialLogMs) {
            lastSerialLog = now;

            Serial.println("==================================================");
            Serial.println("            WIND & SOLAR SYSTEM MONITOR           ");
            Serial.println("==================================================");
            Serial.printf("  [Generator AC]   ZMPT1 (A0): %.1f V RMS | RPM: %d\n", acVoltage1, (int)rpm);
            Serial.printf("  [MPPT Battery]   INA1: %.2f V | Charge: %.2f A (%.1f W) | SoC: %.1f%% (%.1f Wh)\n",
                          dcV1, dcA1, dcP1, frame.battery_soc, frame.battery_wh);
            Serial.printf("  [Inverter DC In] ACS758 (A3): %.2f A | DC Input: %.1f W\n", invCurrent, invPower);
            Serial.printf("  [Inverter AC Out]ZMPT2 (A1): %.1f V | ZMCT (A2): %.2f A | AC Power: %.1f W\n",
                          acVoltage2, acCurrent, acPower);
            if (frame.inverter_efficiency > 0.0f) {
                Serial.printf("  [Inverter Eff]   Efficiency: %.1f %%\n", frame.inverter_efficiency);
            }
            Serial.printf("  [Control/Lights] INA2: %.2f V | Load: %.2f A (%.2f W)\n", dcV2, dcA2, dcP2);
            Serial.printf("  [Temperature]    Gen/Box: %.1f°C / %.1f°C | ESP32 CPU: %.1f°C\n", temp1, temp2, tempEsp);
            Serial.println("==================================================");
        }
#endif

        // --- Update LCD Display (with dynamic screen rotation) ---
        lcdDisplay.update(frame);

        // Account for all work in this iteration, including logging and display I/O.
        frame.cycleMs = millis() - cycleStartedAt;
        if (frame.cycleMs > currentCfg.sensorPollMs) {
            // Deadline overrun — increment counter but still publish the frame
            // since measurements are complete; the overrun is from serial/LCD I/O.
            ++overruns;
        }
        frame.overruns = overruns;
        dataManager.publish(frame);

        // Wait for next cycle
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

// =============================================================
//  CORE 0 — Network/Communication Task
// =============================================================

static void networkTaskFunction(void* pvParameters) {
    Serial.println("[Task] Network task started on Core 0");

    SystemConfig cfg = configManager.getConfig();

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
        SystemConfig currentCfg = configManager.getConfig();

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

bool Tasks::startSensorTask() {
    const bool started = xTaskCreatePinnedToCore(
        sensorTaskFunction,
        "SensorTask",
        4096 * 2,       // Stack size (bytes) — generous for I2C scan + Serial.printf + LCD
        NULL,           // Parameters
        1,              // Priority
        &sensorTaskHandle, // Task handle for watermark diagnostics
        1               // Core 1 (App Core)
    ) == pdPASS;
    if (!started) {
        Serial.println("[Task] Failed to start sensor task");
        sensorTaskHandle = NULL;
    }
    return started;
}

bool Tasks::startNetworkTask() {
    const bool started = xTaskCreatePinnedToCore(
        networkTaskFunction,
        "NetworkTask",
        8192,           // Larger stack for networking
        NULL,           // Parameters
        1,              // Priority
        &networkTaskHandle, // Task handle for watermark diagnostics
        0               // Core 0 (Protocol Core)
    ) == pdPASS;
    if (!started) {
        Serial.println("[Task] Failed to start network task");
        networkTaskHandle = NULL;
    }
    return started;
}

void Tasks::requestI2CScan() {
    portENTER_CRITICAL(&i2cScanMux);
    i2cScanRequested = true;
    portEXIT_CRITICAL(&i2cScanMux);
}

void Tasks::requestAdcCalibration() {
    portENTER_CRITICAL(&adcCalMux);
    adcCalRequested = true;
    portEXIT_CRITICAL(&adcCalMux);
}

TaskHandle_t Tasks::getSensorTaskHandle() {
    return sensorTaskHandle;
}

TaskHandle_t Tasks::getNetworkTaskHandle() {
    return networkTaskHandle;
}
