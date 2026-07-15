// =============================================================
//  main.cpp — ESP32 Wind Generator Monitoring System
//  Entry Point
//
//  This file only bootstraps FreeRTOS tasks, then deletes
//  the Arduino loop task. All work happens in dedicated tasks
//  pinned to specific CPU cores.
// =============================================================

#include <Arduino.h>
#include "config/config.h"
#include "system/config_manager.h"
#include "system/freertos_tasks.h"

void setup() {
    Serial.begin(115200);
    Serial.println();
    Serial.println("====================================");
    Serial.println("  ESP32 Wind Generator Monitor");
    Serial.print("  "); Serial.println(FW_VERSION);
    Serial.println("====================================");

    // Initialize dynamic configuration from LittleFS
    configManager.begin();

    // Start FreeRTOS tasks on both cores
    Tasks::startSensorTask();   // Core 1 — Measurements
    Tasks::startNetworkTask();  // Core 0 — Communication

    // Delete the Arduino loop task — we don't need it
    vTaskDelete(NULL);
}

void loop() {
    // Should never reach here due to vTaskDelete(NULL) above
}
