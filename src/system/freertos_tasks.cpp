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
#include <WiFi.h>
#include <esp_wifi.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <esp_sleep.h>
#include <driver/rtc_io.h>

// Cross-core I2C scan request state (Core 0 to Core 1).
static volatile bool i2cScanRequested = false;
static portMUX_TYPE i2cScanMux = portMUX_INITIALIZER_UNLOCKED;

static volatile bool adcCalRequested = false;
static portMUX_TYPE adcCalMux = portMUX_INITIALIZER_UNLOCKED;

// Serial output mode (false = formatted text box, true = comma-separated values for SerialPlot)
static volatile bool serialCsvMode = false;

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

// Calibrated SoC curve for Lakoni Blue Wolf 12V 65Ah (75D23L, 100% SoH / 550 CCA)
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
static ACS758Sensor acs758(PIN_ACS758_INPUT, ACS758_SENSITIVITY, ACS758_CAL_MULTIPLIER);
static DS18B20Sensor tempBus(PIN_DS18B20);
static RPMSensor    rpmSensor(PIN_RPM_INPUT);
static LcdDisplay   lcdDisplay;

// Dynamic pointer allocation for I2C INA226 module and optional ADS1115 module
static INA226Sensor* ina1 = nullptr;
static ADS1115Sensor* ads  = nullptr;

static void sensorTaskFunction(void* pvParameters) {
    Serial.println("[Task] Sensor task started on Core 1");

    // Power-on stabilization delay for I2C modules and LCD controller
    vTaskDelay(pdMS_TO_TICKS(100));

    // Log GPIO physical logic levels for diagnostic feedback
    pinMode(PIN_I2C_SDA, INPUT_PULLUP);
    pinMode(PIN_I2C_SCL, INPUT_PULLUP);
    int sdaLevel = digitalRead(PIN_I2C_SDA);
    int sclLevel = digitalRead(PIN_I2C_SCL);
    Serial.printf("[I2C Diag] GPIO%d(SDA)=%s, GPIO%d(SCL)=%s\n",
                  PIN_I2C_SDA, sdaLevel ? "HIGH (Normal)" : "LOW (Shorted or pulled down!)",
                  PIN_I2C_SCL, sclLevel ? "HIGH (Normal)" : "LOW (Shorted or pulled down!)");

    // Initialize I2C bus with 100kHz standard mode
    uint8_t activeSda = PIN_I2C_SDA;
    uint8_t activeScl = PIN_I2C_SCL;
    Wire.begin(activeSda, activeScl);
    Wire.setClock(I2C_CLOCK_SPEED);
    Wire.setTimeOut(50);

    Serial.printf("[I2C] Scanning primary I2C bus on SDA=%d, SCL=%d (%lu Hz)...\n",
                  activeSda, activeScl, I2C_CLOCK_SPEED);
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
            if ((addr >= 0x20 && addr <= 0x27) || (addr >= 0x38 && addr <= 0x3F)) {
                if (lcdAddr == 0) lcdAddr = addr;
            }
        }
    }

    // Auto-Recovery: If 0 devices found, test reversed pin orientation (SDA <-> SCL)
    if (i2c_count == 0) {
        Serial.printf("[I2C] No devices on SDA=%d/SCL=%d. Testing reversed pins SDA=%d, SCL=%d...\n",
                      PIN_I2C_SDA, PIN_I2C_SCL, PIN_I2C_SCL, PIN_I2C_SDA);
        Wire.end();
        Wire.begin(PIN_I2C_SCL, PIN_I2C_SDA);
        Wire.setClock(I2C_CLOCK_SPEED);
        Wire.setTimeOut(50);

        for (uint8_t addr = 1; addr < 127; addr++) {
            Wire.beginTransmission(addr);
            if (Wire.endTransmission() == 0) {
                Serial.printf("[I2C-Reversed] Found responsive device at address 0x%02X\n", addr);
                if (i2c_count < 16) {
                    i2c_list[i2c_count++] = addr;
                }
                if ((addr >= 0x20 && addr <= 0x27) || (addr >= 0x38 && addr <= 0x3F)) {
                    if (lcdAddr == 0) lcdAddr = addr;
                }
            }
        }

        if (i2c_count > 0) {
            Serial.println("[I2C] Success! Devices found on reversed pins. Operating on reversed mapping.");
            activeSda = PIN_I2C_SCL;
            activeScl = PIN_I2C_SDA;
        } else {
            Serial.println("[I2C] No devices found on either pin configuration.");
            Wire.end();
            Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
            Wire.setClock(I2C_CLOCK_SPEED);
        }
    }

    if (lcdAddr == 0) {
        lcdAddr = LCD_I2C_ADDR; // Default 0x27 fallback
    }

    // Save detected I2C list to DataManager (for Web Dashboard UI)
    dataManager.updateI2CAddresses(i2c_list, i2c_count);

    // Initialize LCD display with detected address (or fallback)
    lcdDisplay.begin(lcdAddr);

    // Initialize non-I2C sensors
    zmpt1.begin();
    zmpt2.begin();
    zmct.begin();
    tempBus.begin();
    rpmSensor.begin();

    // Initialize power switch pin (RTC GPIO 27)
    pinMode(PIN_POWER_SWITCH, INPUT_PULLDOWN);

    // Fetch config configuration and assign INA226 device based on software settings
    SystemConfig cfg = configManager.getConfig();
    
    // Check if INA226 address is present on the bus
    bool ina1Present = false;
    for (uint8_t i = 0; i < i2c_count; i++) {
        if (i2c_list[i] == cfg.ina1Addr) ina1Present = true;
    }

    if (ina1Present) {
        Serial.printf("[INA226] Dynamic assignment: INA1 (Battery/MPPT) mapped to address 0x%02X\n", cfg.ina1Addr);
        ina1 = new INA226Sensor(cfg.ina1Addr, PIN_I2C_SDA, PIN_I2C_SCL);
        ina1->begin();
    } else {
        Serial.printf("[INA226] Warning: INA1 address 0x%02X NOT found on I2C bus. Sensor disabled.\n", cfg.ina1Addr);
    }

    if (cfg.useAds1115) {
        uint8_t activeAdsAddr = cfg.adsAddr;
        bool adsPresent = false;
        for (uint8_t i = 0; i < i2c_count; i++) {
            if (i2c_list[i] == activeAdsAddr) {
                adsPresent = true;
                break;
            }
        }
        // If configured address not found, probe all 4 possible ADDR variants (0x48..0x4B)
        if (!adsPresent) {
            const uint8_t possibleAdsAddrs[4] = { 0x48, 0x49, 0x4A, 0x4B };
            for (uint8_t a : possibleAdsAddrs) {
                for (uint8_t i = 0; i < i2c_count; i++) {
                    if (i2c_list[i] == a) {
                        activeAdsAddr = a;
                        adsPresent = true;
                        Serial.printf("[ADS1115] Auto-discovered ADS1115 at alternative address 0x%02X\n", a);
                        break;
                    }
                }
                if (adsPresent) break;
            }
        }

        if (adsPresent) {
            Serial.printf("[ADS1115] Dynamic assignment: 16-bit ADC mapped to address 0x%02X (ALRT pin: %d)\n",
                          activeAdsAddr, ADS1115_USE_ALERT ? PIN_ADS1115_ALERT : -1);
            ads = new ADS1115Sensor(activeAdsAddr, ADS1115_USE_ALERT ? PIN_ADS1115_ALERT : -1);
            ads->begin();
        } else {
            Serial.printf("[ADS1115] Warning: ADS1115 NOT found at any address (0x48-0x4B). Falling back to internal ADC.\n");
            acs758.begin();
        }
    } else {
        acs758.begin();
    }

    // Apply saved baseline offsets and calibration directly from LittleFS config
    zmpt1.setOffset(cfg.zmpt1OffsetMv);
    zmpt2.setOffset(cfg.zmpt2OffsetMv);
    zmct.setOffset(cfg.zmctOffsetMv);
    acs758.setZeroOffset(cfg.acs758OffsetMv);
    zmpt1.setCalibration(cfg.zmpt1Cal);
    zmpt2.setCalibration(cfg.zmpt2Cal);
    zmct.setCalibration(cfg.zmctCal);
    acs758.setCalMultiplier(cfg.acs758Cal);
    Serial.printf("[Sensors] Loaded saved calibration from flash: Z1_Off=%.1fmV, Z2_Off=%.1fmV, ZMCT_Off=%.1fmV, ACS_Off=%.1fmV\n",
                  cfg.zmpt1OffsetMv, cfg.zmpt2OffsetMv, cfg.zmctOffsetMv, cfg.acs758OffsetMv);

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
        acs758.setCalMultiplier(currentCfg.acs758Cal);

        if (currentCfg.zmpt1OffsetMv >= 500.0f) zmpt1.setOffset(currentCfg.zmpt1OffsetMv);
        if (currentCfg.zmpt2OffsetMv >= 500.0f) zmpt2.setOffset(currentCfg.zmpt2OffsetMv);
        if (currentCfg.zmctOffsetMv >= 500.0f)  zmct.setOffset(currentCfg.zmctOffsetMv);
        if (currentCfg.acs758OffsetMv >= 100.0f) acs758.setZeroOffset(currentCfg.acs758OffsetMv);

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
            float o_acs = acs758.calibrateZeroOffset(useAds ? ads : nullptr, ADS1115_CH_ACS758);
            configManager.updateOffsets(o1, o2, oi, o_acs);
            Serial.printf("[ADC Cal] Baseline zero offsets saved: ZMPT1=%.1fmV, ZMPT2=%.1fmV, ZMCT=%.1fmV, ACS758=%.1fmV\n", o1, o2, oi, o_acs);
        }

        // --- Read/Simulate Sensors ---
        const uint32_t cycleStartedAt = millis();
        float temp1 = 0.0f, temp2 = 0.0f, tempEsp = 0.0f;
        float acVoltage1, acRaw1, acVoltage2, acRaw2, acCurrent, acRawI, acPower;
        float invCurrent = 0.0f, invRawMv = 0.0f, invPower = 0.0f;
        float adsCh0Mv = 0.0f, adsCh1Mv = 0.0f, adsCh3Mv = 0.0f;
        float dcV1 = 0.0f, dcA1 = 0.0f, dcP1 = 0.0f;
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

            // Battery / MPPT Charging (INA226)
            dcV1 = 12.0f + 0.8f * sin(simStep * 0.4f);
            dcA1 = 1.5f + 1.2f * sin(simStep * 0.6f);
            if (dcA1 < 0.0f) dcA1 = 0.0f;
            dcP1 = dcV1 * dcA1;

            // Inverter DC discharge (ACS758 50A)
            invCurrent = 8.5f + 5.0f * sin(simStep * 0.45f);
            if (invCurrent < 0.0f) invCurrent = 0.0f;
            invPower = dcV1 * invCurrent;
            invRawMv = 2500.0f + (invCurrent * 40.0f);

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

            // --- Read Analog Sensors (ADS1115 for AC Sensors, GPIO 33 for ACS758) ---
            if (currentCfg.useAds1115 && ads != nullptr && ads->isEnabled()) {
                acVoltage1 = zmpt1.readRMSVoltage(ads, ADS1115_CH_ZMPT1); // ADS1115 Ch0 -> ZMPT1
                acRaw1     = zmpt1.readRawADC();
                acVoltage2 = zmpt2.readRMSVoltage(ads, ADS1115_CH_ZMPT2); // ADS1115 Ch1 -> ZMPT2
                acRaw2     = zmpt2.readRawADC();
                acCurrent  = zmct.readRMSCurrent(ads, ADS1115_CH_ZMCT);   // ADS1115 Ch2 -> ZMCT
                acRawI     = zmct.readRawADC();
                // ACS758 hardware output is routed to GPIO 33
                invCurrent = acs758.readCurrent(nullptr, -1);
                invRawMv   = acs758.readRawMilliVolts(nullptr, -1);
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

            // Also sample instantaneous ADS1115 channels for diagnostic comparison
            if (currentCfg.useAds1115 && ads != nullptr && ads->isEnabled()) {
                adsCh0Mv = ads->readMilliVolts(ADS1115_CH_ZMPT1);
                adsCh1Mv = ads->readMilliVolts(ADS1115_CH_ZMPT2);
                adsCh3Mv = ads->readMilliVolts(ADS1115_CH_ACS758);
            }

            // --- Calculate Inverter AC Output Power & Inverter DC Input Power ---
            // ZMPT1 (A0): Generator AC Voltage (before MPPT)
            // ZMPT2 (A1): Inverter AC Output Voltage (220V)
            // ZMCT  (A2): Inverter AC Load Current
            // ACS758(A3): Inverter DC Input Discharge Current
            acPower = acVoltage2 * acCurrent * currentCfg.pf; // Inverter AC Output Power (W)

            // --- Read DC Sensors (nullptr-safe) ---
            // INA226: Battery & MPPT Charging
            dcV1 = (ina1 != nullptr) ? ina1->readVoltage() : 0.0f;
            dcA1 = (ina1 != nullptr) ? ina1->readCurrent() : 0.0f;
            dcP1 = (ina1 != nullptr) ? ina1->readPower() : 0.0f;

            // ACS758 50A: Inverter DC Input Power = V_battery × I_inverter
            invPower = dcV1 * fabsf(invCurrent);

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
        frame.battery_wh = (frame.battery_soc / 100.0f) * (12.0f * 65.00f); // 780.00 Wh nominal (65Ah @ 100% SoH)
        frame.inverter_current = invCurrent; frame.inverter_power = invPower;
        frame.inverter_efficiency = (invPower > 5.0f && acPower > 0.0f) ? min(100.0f, (acPower / invPower) * 100.0f) : 0.0f;
        frame.temperature1 = temp1; frame.temperature2 = temp2; frame.temperature_esp = tempEsp;
        frame.rpm = static_cast<uint32_t>(rpm);
        frame.health = SensorData::HEALTH_AC_V1 | SensorData::HEALTH_AC_V2 |
                       SensorData::HEALTH_AC_I | SensorData::HEALTH_CPU_TEMP |
                       SensorData::HEALTH_RPM | SensorData::HEALTH_ACS758;
        if (temp1Valid) frame.health |= SensorData::HEALTH_TEMP1;
        if (temp2Valid) frame.health |= SensorData::HEALTH_TEMP2;
        if (currentCfg.useAds1115 && ads != nullptr && ads->isEnabled()) frame.health |= SensorData::HEALTH_ADS1115;
        if (currentCfg.dummyMode || (ina1 != nullptr && ina1->isEnabled())) frame.health |= SensorData::HEALTH_INA1;
        frame.sequence = ++sequence;

        // --- Serial Logging (Dynamic rate / CSV mode for SerialPlot) ---
#if ENABLE_SERIAL_LOG
        uint32_t now = millis();
        uint32_t targetLogInterval = serialCsvMode ? 100 : currentCfg.serialLogMs;
        if (now - lastSerialLog >= targetLogInterval) {
            lastSerialLog = now;

            if (serialCsvMode) {
                // Real-time CSV line for SerialPlot / Plotters:
                // Gen_AC_V, Inv_AC_V, Inv_AC_A, Inv_AC_W, Bat_DC_V, MPPT_DC_A, Inv_DC_A, Inv_DC_W, RPM, Temp_Gen, Temp_Box, Temp_ESP
                Serial.printf("%.1f,%.1f,%.2f,%.1f,%.2f,%.2f,%.2f,%.1f,%lu,%.1f,%.1f,%.1f\n",
                              acVoltage1, acVoltage2, acCurrent, acPower,
                              dcV1, dcA1, invCurrent, invPower,
                              (unsigned long)frame.rpm, temp1, temp2, tempEsp);
            } else {
                Serial.println("==================================================");
                Serial.println("            WIND & SOLAR SYSTEM MONITOR           ");
                Serial.println("==================================================");
                Serial.printf("  [Generator AC]   ZMPT1 (A0): %.1f V RMS | RPM: %d\n", acVoltage1, (int)rpm);
                Serial.printf("  [ZMPT1 Raw Diag] RMS Signal: %.1f mV | DC Offset: %.1f mV | Inst A0: %.1f mV | Read via: %s\n",
                              zmpt1.getRawRMSMilliVolts(), zmpt1.getOffset(), adsCh0Mv,
                              (currentCfg.useAds1115 && ads != nullptr && ads->isEnabled()) ? "ADS1115 (A0)" : "Internal ADC (GPIO 34)");
                Serial.printf("  [MPPT Battery]   INA1: %.2f V | Charge: %.2f A (%.1f W) | SoC: %.1f%% (%.1f Wh)\n",
                              dcV1, dcA1, dcP1, frame.battery_soc, frame.battery_wh);
                Serial.printf("  [Inverter DC In] ACS758 (GPIO 33): %.2f A | DC Input: %.1f W\n", invCurrent, invPower);
                Serial.printf("  [ACS758 Diag]    GPIO 33: %.1f mV (Δ: %.1f mV) | Baseline: %.1f mV | ADS1115 A3: %.1f mV\n",
                              invRawMv, invRawMv - acs758.getZeroOffset(), acs758.getZeroOffset(), adsCh3Mv);
                Serial.printf("  [Inverter AC Out]ZMPT2 (A1): %.1f V | ZMCT (A2): %.2f A | AC Power: %.1f W\n",
                              acVoltage2, acCurrent, acPower);
                Serial.printf("  [ZMPT2 Raw Diag] RMS Signal: %.1f mV | DC Offset: %.1f mV | Inst A1: %.1f mV | Read via: %s\n",
                              zmpt2.getRawRMSMilliVolts(), zmpt2.getOffset(), adsCh1Mv,
                              (currentCfg.useAds1115 && ads != nullptr && ads->isEnabled()) ? "ADS1115 (A1)" : "Internal ADC (GPIO 35)");
                Serial.printf("  [ZMCT Raw Diag]  RMS Signal: %.1f mV | DC Offset: %.1f mV | Read via: %s\n",
                              sqrtf(zmct.readRawADC()), zmct.getOffset(),
                              (currentCfg.useAds1115 && ads != nullptr && ads->isEnabled()) ? "ADS1115 (A2)" : "Internal ADC (GPIO 32)");
                if (frame.inverter_efficiency > 0.0f) {
                    Serial.printf("  [Inverter Eff]   Efficiency: %.1f %%\n", frame.inverter_efficiency);
                }
                Serial.printf("  [Temperature]    Gen/Box: %.1f°C / %.1f°C | ESP32 CPU: %.1f°C\n", temp1, temp2, tempEsp);
                Serial.println("==================================================");
            }
        }
#endif

        // --- Update LCD Display (with dynamic screen rotation) ---
        lcdDisplay.update(frame);

        // --- Power Switch Deep Sleep Monitoring ---
        static uint32_t switchLowStartTime = 0;
        if (currentCfg.enablePowerSwitch) {
            const int switchState = digitalRead(PIN_POWER_SWITCH);
            if (switchState == LOW) {
                if (switchLowStartTime == 0) {
                    switchLowStartTime = millis();
                    Serial.printf("[Power] Power switch turned OFF (LOW). Entering Deep Sleep in %u ms...\n",
                                  currentCfg.powerSwitchTimeoutMs);
                } else if (millis() - switchLowStartTime >= currentCfg.powerSwitchTimeoutMs) {
                    Serial.printf("[Power] Switch stayed LOW for %u ms. Entering Deep Sleep...\n",
                                  currentCfg.powerSwitchTimeoutMs);

                    // 1. Clear LCD and turn off backlight
                    lcdDisplay.shutdown();

                    // 2. Disconnect WiFi cleanly
                    WiFi.disconnect(true);
                    WiFi.mode(WIFI_OFF);

                    // 3. Flush serial buffer
                    Serial.println("[Power] Deep sleep active. Wake up trigger: Switch HIGH (GPIO 27).");
                    Serial.flush();

                    // 4. Configure RTC GPIO pull-down (stays LOW while switch is open)
                    rtc_gpio_pulldown_en((gpio_num_t)PIN_POWER_SWITCH);
                    rtc_gpio_pullup_dis((gpio_num_t)PIN_POWER_SWITCH);

                    // 5. Configure EXT0: Wake up when PIN_POWER_SWITCH goes HIGH (level 1)
                    esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_POWER_SWITCH, 1);

                    // 6. Enter Deep Sleep
                    esp_deep_sleep_start();
                }
            } else {
                if (switchLowStartTime != 0) {
                    Serial.println("[Power] Power switch turned back ON (HIGH). Sleep countdown canceled.");
                    switchLowStartTime = 0;
                }
            }
        }

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

    // Setup mDNS responder (http://WiM.local)
    if (MDNS.begin(MDNS_HOSTNAME)) {
        MDNS.addService("http", "tcp", 80);
        Serial.printf("[mDNS] Responder started: http://%s.local\n", MDNS_HOSTNAME);
    } else {
        Serial.println("[mDNS] Error setting up mDNS responder");
    }

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

        // Process Serial USB Commands (e.g. CAL, SCAN, REBOOT)
        if (Serial.available()) {
            String cmd = Serial.readStringUntil('\n');
            cmd.trim();
            cmd.toUpperCase();
            if (cmd == "CAL" || cmd == "CALIBRATE") {
                Serial.println("[Command] Received CALIBRATE command. Triggering zero baseline calibration...");
                Tasks::requestAdcCalibration();
            } else if (cmd == "SCAN" || cmd == "I2C") {
                Serial.println("[Command] Received I2C SCAN command. Scanning bus...");
                Tasks::requestI2CScan();
            } else if (cmd == "CSV" || cmd == "PLOT") {
                serialCsvMode = true;
                Serial.println("[Command] Serial output switched to CSV mode for SerialPlot.");
                Serial.println("CSV_HEADER:Gen_AC_V,Inv_AC_V,Inv_AC_A,Inv_AC_W,Bat_DC_V,MPPT_DC_A,Inv_DC_A,Inv_DC_W,Rotor_RPM,Temp_Gen,Temp_Box,Temp_ESP");
            } else if (cmd == "TEXT" || cmd == "LOG") {
                serialCsvMode = false;
                Serial.println("[Command] Serial output switched to formatted text log mode.");
            } else if (cmd == "REBOOT" || cmd == "RESTART") {
                Serial.println("[Command] Rebooting ESP32...");
                ESP.restart();
            } else if (cmd == "HELP" || cmd == "?") {
                Serial.println("[Commands] CAL (Calibrate zero), SCAN (Scan I2C), CSV/PLOT (SerialPlot stream), TEXT/LOG (Summary box), REBOOT");
            }
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
