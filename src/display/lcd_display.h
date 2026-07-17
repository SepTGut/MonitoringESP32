#pragma once
// =============================================================
//  lcd_display.h — Auto-Scanning I2C LCD 16x2 Display Controller
//  Displays real-time status and rotates screens periodically.
//  Automatically scans the I2C bus at startup to discover the LCD address.
// =============================================================

#include <Arduino.h>
#include <LiquidCrystal_I2C.h>
#include "../system/data_manager.h"

class LcdDisplay {
public:
    LcdDisplay();
    ~LcdDisplay();

    // Initialize hardware with pre-scanned I2C address
    void begin(uint8_t addr);

    // Call periodically to handle screen rotations and redraws (if LCD is connected)
    void update(const SensorData& data);

    // Returns true if an LCD was found and initialized
    bool isEnabled() const { return _enabled; }

private:
    LiquidCrystal_I2C* _lcd;
    uint8_t            _addr;
    bool               _enabled;
    uint8_t            _currentScreen;
    uint32_t           _lastRotateTime;
    uint32_t           _lastUpdateTime;

    // Redraw screen contents based on active rotation state
    void drawScreen(uint8_t screen, const SensorData& data);

    // Cleans and prints text to prevent screen residues
    void printLine(uint8_t row, const String& label, const String& value);
};
