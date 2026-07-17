#pragma once
// =============================================================
//  freertos_tasks.h — FreeRTOS Task Declarations
//  Core 0: Communication (WiFi, Web, Serial)
//  Core 1: Measurement (all sensors)
// =============================================================

#include <Arduino.h>

namespace Tasks {
    // Start sensor measurement task on Core 1 (App Core)
    void startSensorTask();

    // Start network/communication task on Core 0 (Protocol Core)
    void startNetworkTask();

    // Trigger an on-demand I2C scanner run safely on Core 1
    void requestI2CScan();
}
