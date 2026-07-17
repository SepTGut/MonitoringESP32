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
    : _lcd(nullptr), _addr(0), _enabled(false), _currentScreen(0), _lastRotateTime(0), _lastUpdateTime(0) {
}

LcdDisplay::~LcdDisplay() {
    if (_lcd != nullptr) {
        delete _lcd;
    }
}

void LcdDisplay::begin(uint8_t addr) {
    if (addr == 0) {
        Serial.println("[LCD] No I2C LCD display address provided. LCD display is DISABLED.");
        _enabled = false;
        return;
    }

    _addr = addr;
    Serial.printf("[LCD] Initializing I2C LCD display at address 0x%02X\n", _addr);
    
    // Allocate the LiquidCrystal_I2C object dynamically with the scanned address
    _lcd = new LiquidCrystal_I2C(_addr, 16, 2);
    _lcd->init();
    _lcd->backlight();
    _lcd->clear();
    _enabled = true;

    // Show initial splash screen
    printLine(0, "  WIND MONITOR  ", "");
    printLine(1, "   Booting...   ", "");
    delay(1000);
}

void LcdDisplay::update(const SensorData& data) {
    if (!_enabled || _lcd == nullptr) return;

    uint32_t now = millis();
    bool screenChanged = false;
    
    // Check if it's time to rotate screen
    if (now - _lastRotateTime >= LCD_ROTATION_MS) {
        _currentScreen = (_currentScreen + 1) % 4; // Rotate 4 screens
        _lastRotateTime = now;
        _lcd->clear(); // Clear residues during transition
        screenChanged = true;
    }

    // Only redraw the screen when transition occurs OR every 500ms to eliminate screen flickering
    if (screenChanged || (now - _lastUpdateTime >= 500)) {
        _lastUpdateTime = now;
        drawScreen(_currentScreen, data);
    }
}

void LcdDisplay::printLine(uint8_t row, const String& label, const String& value) {
    if (!_enabled || _lcd == nullptr) return;

    String line = label + value;
    
    // Pad to exactly 16 characters to wipe out any residue
    while (line.length() < 16) {
        line += " ";
    }
    
    if (line.length() > 16) {
        line = line.substring(0, 16);
    }

    _lcd->setCursor(0, row);
    _lcd->print(line);
}

void LcdDisplay::drawScreen(uint8_t screen, const SensorData& data) {
    char buf[17];

    switch (screen) {
        case 0:
            // Screen 0: Overview & AC Power
            snprintf(buf, sizeof(buf), "%5.1fV %4.2fA", data.ac_voltage, data.ac_current);
            printLine(0, "AC: ", buf);
            
            snprintf(buf, sizeof(buf), "%4.0fW PF%0.2f", data.ac_power, configManager.getConfig().pf);
            printLine(1, "Pwr:", buf);
            break;
 
        case 1:
            // Screen 1: DC Battery Channels Power
            snprintf(buf, sizeof(buf), "%5.2fV %5.2fA", data.ina1_voltage, data.ina1_current);
            printLine(0, "DC1:", buf);
            
            snprintf(buf, sizeof(buf), "%5.2fV %5.2fA", data.ina2_voltage, data.ina2_current);
            printLine(1, "DC2:", buf);
            break;
 
        case 2:
            // Screen 2: Speed and Temperatures
            snprintf(buf, sizeof(buf), "%5d RPM", data.rpm);
            printLine(0, "Speed:", buf);
            
            snprintf(buf, sizeof(buf), "%4.1fC / %4.1fC", data.temperature1, data.temperature2);
            printLine(1, "T: ", buf);
            break;

        case 3:
            // Screen 3: WiFi Details (AP/STA dynamic SSID & IP)
            if (WiFi.status() == WL_CONNECTED) {
                printLine(0, "STA: ", WiFi.SSID());
                printLine(1, "IP : ", WiFi.localIP().toString());
            } else {
                printLine(0, "AP : ", configManager.getConfig().apSSID);
                printLine(1, "IP : ", WiFi.softAPIP().toString());
            }
            break;

        default:
            break;
    }
}
