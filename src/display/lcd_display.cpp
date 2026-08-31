// =============================================================
//  lcd_display.cpp — Auto-Scanning I2C LCD 16x2 Display Controller
// =============================================================

#include "lcd_display.h"
#include "../config/config.h"
#include "../system/config_manager.h"
#include <Wire.h>
#include <WiFi.h>

// Candidate LCD I2C addresses (0x27 and 0x3F are standard PCF8574 backpacks)
static const uint8_t LCD_CANDIDATE_ADDRS[] = {
    0x27, 0x3F, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 
    0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E
};

LcdDisplay::LcdDisplay()
    : _lcd(nullptr),
      _addr(0),
      _enabled(false),
      _currentScreen(0),
      _lastRotateTime(0),
      _lastUpdateTime(0),
      _animPos(0.0f),
      _lastAnimTime(0) {
}

LcdDisplay::~LcdDisplay() {
    if (_lcd != nullptr) {
        delete _lcd;
    }
}

void LcdDisplay::begin(uint8_t addr) {
    if (addr == 0) {
        addr = 0x27; // Standard PCF8574 fallback address
        Serial.println("[LCD] No scanned I2C LCD address; attempting initialization at default 0x27...");
    }

    _addr = addr;
    Serial.printf("[LCD] Initializing I2C LCD display at address 0x%02X\n", _addr);
    
    // Safely free previous instance if re-initializing after bus rescan
    if (_lcd != nullptr) {
        delete _lcd;
        _lcd = nullptr;
    }

    // Send direct hardware backlight activation pulse to common PCF8574 / PCF8574A addresses
    for (uint8_t candidate : { (uint8_t)0x27, (uint8_t)0x3F, (uint8_t)0x20, (uint8_t)0x38 }) {
        Wire.beginTransmission(candidate);
        Wire.write(0x08); // P3 bit = 1 turns on backlight transistor
        Wire.endTransmission();
    }

    // Allocate the LiquidCrystal_I2C object dynamically with the target address
    _lcd = new LiquidCrystal_I2C(_addr, 16, 2);
    _lcd->init();
    _lcd->backlight();
    
    // Define and create custom icons for the LCD display
    uint8_t propeller0[8] = { 0b00100, 0b00100, 0b00100, 0b01110, 0b00100, 0b00100, 0b00100, 0b00000 };
    uint8_t propeller1[8] = { 0b00001, 0b00010, 0b00100, 0b01110, 0b00100, 0b01000, 0b10000, 0b00000 };
    uint8_t propeller2[8] = { 0b00000, 0b00000, 0b00000, 0b11111, 0b00000, 0b00000, 0b00000, 0b00000 };
    uint8_t propeller3[8] = { 0b10000, 0b01000, 0b00100, 0b01110, 0b00100, 0b00010, 0b00001, 0b00000 };
    uint8_t thermometer[8] = { 0b00100, 0b00100, 0b00100, 0b01010, 0b10001, 0b11111, 0b01110, 0b00000 };
    uint8_t battery[8] = { 0b01110, 0b11111, 0b10001, 0b10001, 0b11111, 0b11111, 0b11111, 0b11111 };
    uint8_t bolt[8] = { 0b00010, 0b00100, 0b01000, 0b11111, 0b00100, 0b01000, 0b10000, 0b00000 };

    _lcd->createChar(0, propeller0);
    _lcd->createChar(1, propeller1);
    _lcd->createChar(2, propeller2);
    _lcd->createChar(3, propeller3);
    _lcd->createChar(4, thermometer);
    _lcd->createChar(5, battery);
    _lcd->createChar(6, bolt);

    _lcd->clear();
    _enabled = true;

    // Show initial splash screen
    printLine(0, "  WIND MONITOR  ");
    printLine(1, "   Booting...   ");
    delay(1000);
}

void LcdDisplay::update(const SensorData& data) {
    if (!_enabled || _lcd == nullptr) return;

    uint32_t now = millis();
    bool screenChanged = false;
    
    // Calculate animation step proportional to RPM
    if (_lastAnimTime == 0) {
        _lastAnimTime = now;
    }
    uint32_t dt = now - _lastAnimTime;
    _lastAnimTime = now;
    
    if (data.rpm > 0) {
        _animPos += (data.rpm / 60.0f) * 4.0f * (dt / 1000.0f);
    }

    // Check if it's time to rotate screen (4 screens total)
    if (now - _lastRotateTime >= LCD_ROTATION_MS) {
        _currentScreen = (_currentScreen + 1) % 4; 
        _lastRotateTime = now;
        _lcd->clear(); // Clear residues during transition
        screenChanged = true;
    }

    // Only redraw the screen when transition occurs OR every 500ms to eliminate screen flickering
    if (screenChanged || (now - _lastUpdateTime >= 500)) {
        _lastUpdateTime = now;
        drawScreen(_currentScreen, data);
    }

    // Update the propeller frame character at row 0, column 0 dynamically every 100ms for smooth spin (active on Screen 2)
    static uint32_t lastRotorDrawTime = 0;
    if (_currentScreen == 2 && (now - lastRotorDrawTime >= 100)) {
        lastRotorDrawTime = now;
        uint8_t propellerFrame = ((int)_animPos) % 4;
        _lcd->setCursor(0, 0);
        _lcd->write(propellerFrame);
    }
}

void LcdDisplay::printLine(uint8_t row, const char* line) {
    if (!_enabled || _lcd == nullptr) return;

    // Pad or truncate to exactly 16 characters to wipe residue
    char padded[17];
    size_t len = strlen(line);
    if (len > 16) len = 16;
    memcpy(padded, line, len);
    for (size_t i = len; i < 16; i++) padded[i] = ' ';
    padded[16] = '\0';

    _lcd->setCursor(0, row);
    _lcd->print(padded);
}

void LcdDisplay::drawScreen(uint8_t screen, const SensorData& data) {
    char buf[17];

    switch (screen) {
        case 0: {
            // Screen 0: Inverter AC Output Overview
            // Line 0: [bolt]AC: [inv_ac_voltage]V [inv_ac_current]A
            snprintf(buf, sizeof(buf), "\x06" "AC:%5.1fV %4.2fA", data.inv_ac_voltage, data.inv_ac_current);
            printLine(0, buf);
            
            // Line 1: Pwr: [inv_ac_power]W PF[pf]
            snprintf(buf, sizeof(buf), "Pwr:%4.0fW PF%0.2f", data.inv_ac_power, configManager.getConfig().pf);
            printLine(1, buf);
            break;
        }
 
        case 1: {
            // Screen 1: DC Power Overview (Battery INA226 & Inverter ACS758)
            // Line 0: [battery]BAT:[ina1_voltage]V [battery_soc]%
            snprintf(buf, sizeof(buf), "\x05" "BAT:%5.2fV %3.0f%%", data.ina1_voltage, data.battery_soc);
            printLine(0, buf);
            
            // Line 1: [bolt]INV:[inverter_current]A [inverter_power]W
            snprintf(buf, sizeof(buf), "\x06" "INV:%5.2fA %4.0fW", fabsf(data.inverter_current), data.inverter_power);
            printLine(1, buf);
            break;
        }

        case 2: {
            // Screen 2: Rotor & Temperatures (RPM + 3 Temperatures)
            // Line 0: [propeller placeholder] [rpm] RPM
            snprintf(buf, sizeof(buf), "  %4lu RPM", (unsigned long)data.rpm);
            printLine(0, buf);
            
            // Line 1: [thermometer] [tEsp]C Ext: [t1]/[t2]C
            snprintf(buf, sizeof(buf), "\x04" "%2.0fC Ext:%2.0f/%2.0fC", data.temperature_esp, data.temperature1, data.temperature2);
            printLine(1, buf);
            break;
        }

        case 3: {
            // Screen 3: WiFi Details (AP/STA dynamic SSID & IP)
            char line[17];
            if (WiFi.status() == WL_CONNECTED) {
                snprintf(line, sizeof(line), "STA: %-10.10s", WiFi.SSID().c_str());
                printLine(0, line);
                snprintf(line, sizeof(line), "IP : %-10.10s", WiFi.localIP().toString().c_str());
                printLine(1, line);
            } else {
                snprintf(line, sizeof(line), "AP : %-10.10s", configManager.getConfig().apSSID);
                printLine(0, line);
                snprintf(line, sizeof(line), "IP : %-10.10s", WiFi.softAPIP().toString().c_str());
                printLine(1, line);
            }
            break;
        }

        default:
            break;
    }
}

void LcdDisplay::shutdown() {
    if (_enabled && _lcd != nullptr) {
        _lcd->clear();
        _lcd->noBacklight();
    }
}

