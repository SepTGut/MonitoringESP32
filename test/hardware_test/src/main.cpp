// =============================================================
//  main.cpp — Standalone Hardware Sensor Diagnostic Test
//
//  Tests and measures all hardware sensors on the ESP32:
//  - I2C Bus Scanner (LCD, INA226 x2, ADS1115)
//  - Internal ESP32 eFuse Calibrated ADC (ZMPT101B x2, ZMCT103C)
//  - External 16-Bit ADS1115 I2C ADC (Channels A0, A1, A2, A3)
//  - INA226 DC Voltage, Current, and Power Sensors
//  - DS18B20 OneWire Temperature Sensors + ESP32 Internal CPU Temp
//  - RPM Pulse Counter with Hardware Interrupt
//  - I2C 16x2 Character LCD Display
//
//  Outputs formatted diagnostics to Serial at 115200 baud every 1 sec.
// =============================================================

#include <Arduino.h>
#include <Wire.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <INA226.h>
#include <ADS1X15.h>
#include <LiquidCrystal_I2C.h>

// --- Pin Definitions ---
#define PIN_ZMPT_1      34  // ADC1_CH6 (ZMPT101B #1)
#define PIN_ZMPT_2      35  // ADC1_CH7 (ZMPT101B #2)
#define PIN_ZMCT        32  // ADC1_CH4 (ZMCT103C)
#define PIN_ACS758_IN   33  // ADC1_CH5 (ACS758 Inverter Current Fallback)
#define PIN_I2C_SDA     21  // I2C SDA (GPIO 21)
#define PIN_I2C_SCL     22  // I2C SCL (GPIO 22)
#define PIN_DS18B20     4   // OneWire bus (GPIO 4)
#define PIN_RPM         16  // IR pulse sensor (GPIO 16)

// --- C-extern for built-in ESP32 CPU temp ---
#ifdef __cplusplus
extern "C" {
#endif
uint8_t temprature_sens_read();
#ifdef __cplusplus
}
#endif

// --- Sensor Objects ---
static OneWire           oneWire(PIN_DS18B20);
static DallasTemperature tempSensors(&oneWire);
static ADS1115           ads(0x48);
static INA226            ina1(0x44);
static INA226            ina2(0x45);
static LiquidCrystal_I2C lcd(0x27, 16, 2);

// --- RPM Interrupt Handling ---
static volatile uint32_t pulseCount = 0;
static volatile uint32_t lastPulseTime = 0;
static portMUX_TYPE rpmMux = portMUX_INITIALIZER_UNLOCKED;
static const uint32_t RPM_DEBOUNCE_US = 1500;

void IRAM_ATTR rpmISR() {
    uint32_t now = micros();
    portENTER_CRITICAL_ISR(&rpmMux);
    if (lastPulseTime == 0 || (now - lastPulseTime) >= RPM_DEBOUNCE_US) {
        pulseCount = pulseCount + 1;
        lastPulseTime = now;
    }
    portEXIT_CRITICAL_ISR(&rpmMux);
}

// Hardware state flags
static bool adsConnected  = false;
static bool ina1Connected = false;
static bool ina2Connected = false;
static bool lcdConnected  = false;
static uint8_t i2cFoundAddrs[16];
static uint8_t i2cFoundCount = 0;

void scanI2CBus() {
    i2cFoundCount = 0;
    for (uint8_t addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            if (i2cFoundCount < 16) {
                i2cFoundAddrs[i2cFoundCount++] = addr;
            }
            if (addr == 0x48 || addr == 0x49 || addr == 0x4A || addr == 0x4B) adsConnected = true;
            if (addr == 0x44 || addr == 0x40) ina1Connected = true;
            if (addr == 0x45 || addr == 0x41) ina2Connected = true;
            if (addr == 0x27 || addr == 0x3F) lcdConnected = true;
        }
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.println("============================================================");
    Serial.println("         ESP32 WIND MONITOR — HARDWARE DIAGNOSTIC TEST");
    Serial.println("============================================================");

    // Initialize I2C Bus
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    scanI2CBus();

    // Configure Internal ESP32 ADC
    pinMode(PIN_ZMPT_1, INPUT);
    pinMode(PIN_ZMPT_2, INPUT);
    pinMode(PIN_ZMCT, INPUT);
    analogSetAttenuation(ADC_11db);
    analogReadResolution(12);

    // Initialize ADS1115 if found
    if (adsConnected) {
        ads.begin();
        ads.setGain(1);     // +/- 4.096V (1 LSB = 0.125mV)
        ads.setDataRate(7); // 860 SPS
        Serial.println("[Init] ADS1115 16-bit ADC initialized @ 0x48");
    } else {
        Serial.println("[Init] ADS1115 not detected on I2C bus");
    }

    // Initialize INA226 modules if found
    if (ina1Connected && ina1.begin()) {
        ina1.setMaxCurrentShunt(0.80, 0.10);
        Serial.println("[Init] INA226 #1 initialized @ 0x44 (R100 = 0.10Ω)");
    }
    if (ina2Connected && ina2.begin()) {
        ina2.setMaxCurrentShunt(0.80, 0.10);
        Serial.println("[Init] INA226 #2 initialized @ 0x45 (R100 = 0.10Ω)");
    }

    // Initialize OneWire DS18B20
    tempSensors.begin();
    tempSensors.setWaitForConversion(false);
    tempSensors.requestTemperatures();
    Serial.printf("[Init] DS18B20 found %d sensor(s) on GPIO %d\n", tempSensors.getDeviceCount(), PIN_DS18B20);

    // Initialize RPM Sensor interrupt
    pinMode(PIN_RPM, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PIN_RPM), rpmISR, FALLING);
    Serial.printf("[Init] RPM pulse sensor attached on GPIO %d\n", PIN_RPM);

    // Initialize ACS758 Fallback Input pin (GPIO 33)
    pinMode(PIN_ACS758_IN, INPUT);
    analogSetPinAttenuation(PIN_ACS758_IN, ADC_11db);
    Serial.printf("[Init] ACS758 fallback analog input attached on GPIO %d\n", PIN_ACS758_IN);

    // Initialize LCD if found
    if (lcdConnected) {
        lcd.init();
        lcd.backlight();
        lcd.setCursor(0, 0); lcd.print("HARDWARE TEST");
        lcd.setCursor(0, 1); lcd.print("Serial Monitor..");
        Serial.println("[Init] 16x2 I2C LCD initialized");
    }

    Serial.println("============================================================");
    Serial.println("[Setup Complete] Starting 1Hz diagnostic measurement loop...\n");
}

void loop() {
    static uint32_t lastLoopTime = 0;
    static uint32_t lastPulses = 0;
    uint32_t now = millis();

    if (now - lastLoopTime < 1000) {
        delay(10);
        return;
    }
    uint32_t dt = now - lastLoopTime;
    lastLoopTime = now;

    // --- 1. Read Internal ESP32 eFuse Calibrated ADC (mV) ---
    float zmpt1_mV = (float)analogReadMilliVolts(PIN_ZMPT_1);
    float zmpt2_mV = (float)analogReadMilliVolts(PIN_ZMPT_2);
    float zmct_mV  = (float)analogReadMilliVolts(PIN_ZMCT);

    // --- 2. Read ADS1115 (mV) if connected ---
    float ads0_mV = 0.0f, ads1_mV = 0.0f, ads2_mV = 0.0f, ads3_mV = 0.0f;
    if (adsConnected) {
        ads0_mV = ads.readADC(0) * 0.125f;
        ads1_mV = ads.readADC(1) * 0.125f;
        ads2_mV = ads.readADC(2) * 0.125f;
        ads3_mV = ads.readADC(3) * 0.125f;
    }

    // --- 3. Read INA226 DC Power Sensors ---
    float ina1_V = ina1Connected ? ina1.getBusVoltage() : 0.0f;
    float ina1_A = ina1Connected ? ((ina1.getShuntVoltage_mV() / 1000.0f) / 0.10f) : 0.0f;
    float ina1_W = ina1_V * fabsf(ina1_A);
    
    float ina2_V = ina2Connected ? ina2.getBusVoltage() : 0.0f;
    float ina2_A = ina2Connected ? ((ina2.getShuntVoltage_mV() / 1000.0f) / 0.10f) : 0.0f;
    float ina2_W = ina2_V * fabsf(ina2_A);

    // --- 4. Read DS18B20 Temperatures ---
    float temp1 = tempSensors.getTempCByIndex(0);
    float temp2 = tempSensors.getTempCByIndex(1);
    tempSensors.requestTemperatures(); // request next conversion
    float cpuTemp = (temprature_sens_read() - 32.0f) / 1.8f;

    // --- 5. Read RPM ---
    uint32_t currentPulses;
    uint32_t currentPulseTime;
    portENTER_CRITICAL(&rpmMux);
    currentPulses = pulseCount;
    currentPulseTime = lastPulseTime;
    portEXIT_CRITICAL(&rpmMux);

    uint32_t pDelta = currentPulses - lastPulses;
    lastPulses = currentPulses;
    float rpm = ((float)pDelta / ((float)dt / 1000.0f)) * 60.0f;
    if ((micros() - currentPulseTime) > 1000000UL) rpm = 0.0f; // Timeout if stopped

    // --- 6. Read ACS758 50A Inverter DC Sensor (Fallback GPIO 33 & ADS1115 A3) ---
    uint16_t raw_acs = analogRead(PIN_ACS758_IN);
    float acs_fallback_mV = (float)analogReadMilliVolts(PIN_ACS758_IN);
    float acs_ads_A = (adsConnected) ? ((ads3_mV - 2500.0f) / 40.0f) : 0.0f; // 40mV/A sensitivity
    float acs_fallback_A = (acs_fallback_mV - 2500.0f) / 40.0f;

    // --- 7. Formatted Serial Report ---
    Serial.println("┌───────────────────────────────────────────────────────────┐");
    Serial.println("│              HARDWARE SENSOR DIAGNOSTIC REPORT            │");
    Serial.println("├───────────────────────────────────────────────────────────┤");

    // I2C Map
    Serial.print("│ I2C Bus Scan: ");
    if (i2cFoundCount == 0) {
        Serial.println("NO DEVICES FOUND                                │");
    } else {
        for (uint8_t i = 0; i < i2cFoundCount; i++) {
            Serial.printf("0x%02X ", i2cFoundAddrs[i]);
        }
        Serial.println("                                      │");
    }

    // Read raw 12-bit ADC counts
    uint16_t raw_z1 = analogRead(PIN_ZMPT_1);
    uint16_t raw_z2 = analogRead(PIN_ZMPT_2);
    uint16_t raw_zi = analogRead(PIN_ZMCT);

    // Internal ADC
    Serial.println("├─ [1] Internal ESP32 ADC (eFuse Vref Calibrated) ─────────┤");
    Serial.println("│  (Note: Unconnected/floating pins float ~1400-1800mV with noise) │");
    Serial.printf("│  ZMPT101B #1 (GPIO 34): Raw:%4u | eFuse:%6.1fmV | Offset:1650mV│\n", raw_z1, zmpt1_mV);
    Serial.printf("│  ZMPT101B #2 (GPIO 35): Raw:%4u | eFuse:%6.1fmV | Offset:1650mV│\n", raw_z2, zmpt2_mV);
    Serial.printf("│  ZMCT103C    (GPIO 32): Raw:%4u | eFuse:%6.1fmV | Offset:1650mV│\n", raw_zi, zmct_mV);
    Serial.printf("│  ACS758 50A  (GPIO 33): Raw:%4u | eFuse:%6.1fmV | Calc:%5.2fA  │\n", raw_acs, acs_fallback_mV, acs_fallback_A);

    // ADS1115
    Serial.println("├─ [2] External ADS1115 16-Bit ADC (I2C) ──────────────────┤");
    if (adsConnected) {
        Serial.printf("│  Channel A0 (ZMPT1)       : %8.3f mV                     │\n", ads0_mV);
        Serial.printf("│  Channel A1 (ZMPT2)       : %8.3f mV                     │\n", ads1_mV);
        Serial.printf("│  Channel A2 (ZMCT)        : %8.3f mV                     │\n", ads2_mV);
        Serial.printf("│  Channel A3 (ACS758 50A)  : %8.3f mV (Calc: %5.2f A)     │\n", ads3_mV, acs_ads_A);
    } else {
        Serial.println("│  Status: NOT CONNECTED                                    │");
    }

    // INA226 DC Power
    Serial.println("├─ [3] INA226 DC Power Sensors ─────────────────────────────┤");
    Serial.printf("│  INA226 #1 (0x44): %5.2fV | %5.2fA | %6.2fW [%s]        │\n",
                  ina1_V, ina1_A, ina1_W, ina1Connected ? "OK" : "MISSING");
    Serial.printf("│  INA226 #2 (0x45): %5.2fV | %5.2fA | %6.2fW [%s]        │\n",
                  ina2_V, ina2_A, ina2_W, ina2Connected ? "OK" : "MISSING");

    // Temperatures
    Serial.println("├─ [4] Temperature Sensors ─────────────────────────────────┤");
    Serial.printf("│  DS18B20 #1 : %5.1f °C [%s]                              │\n",
                  temp1 > -100 ? temp1 : 0.0f, temp1 > -100 ? "OK" : "DISCONNECTED");
    Serial.printf("│  DS18B20 #2 : %5.1f °C [%s]                              │\n",
                  temp2 > -100 ? temp2 : 0.0f, temp2 > -100 ? "OK" : "DISCONNECTED");
    Serial.printf("│  ESP32 CPU  : %5.1f °C                                   │\n", cpuTemp);

    // RPM
    Serial.println("├─ [5] RPM Rotor Pulse Counter (GPIO 16) ───────────────────┤");
    Serial.printf("│  Total Pulses: %-8u | Speed: %5.0f RPM                    │\n",
                  currentPulses, rpm);

    Serial.println("└───────────────────────────────────────────────────────────┘");
    
    // Machine-readable serial plot stream for log_ploter.py
    Serial.printf("RAW_PLOT:raw_z1=%u,raw_z2=%u,raw_zi=%u,zmpt1_mv=%.1f,zmpt2_mv=%.1f,zmct_mv=%.1f,ads0=%.3f,ads1=%.3f,ads2=%.3f,ads3=%.3f,acs_a=%.2f,ina1_v=%.2f,ina1_a=%.2f,ina1_w=%.2f,ina2_v=%.2f,ina2_a=%.2f,ina2_w=%.2f,rpm=%.0f,temp1=%.1f,temp2=%.1f,temp_esp=%.1f,raw_acs=%u,acs_mv=%.1f\n",
                  raw_z1, raw_z2, raw_zi, zmpt1_mV, zmpt2_mV, zmct_mV,
                  ads0_mV, ads1_mV, ads2_mV, ads3_mV, acs_ads_A,
                  ina1_V, ina1_A, ina1_W, ina2_V, ina2_A, ina2_W,
                  rpm, (temp1 > -100 ? temp1 : 0.0f), (temp2 > -100 ? temp2 : 0.0f), cpuTemp,
                  raw_acs, acs_fallback_mV);
    Serial.println();

    // Update LCD
    if (lcdConnected) {
        char line0[17], line1[17];
        snprintf(line0, sizeof(line0), "V:%4.1fV A:%4.2fA", ina1_V, ina1_A);
        snprintf(line1, sizeof(line1), "RPM:%4.0f T:%2.0fC", rpm, cpuTemp);
        lcd.setCursor(0, 0); lcd.print(line0);
        lcd.setCursor(0, 1); lcd.print(line1);
    }
}
