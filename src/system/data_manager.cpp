// =============================================================
//  data_manager.cpp — Thread-Safe Central Data Store
// =============================================================

#include "data_manager.h"

// Global singleton instance
DataManager dataManager;

DataManager::DataManager() {
    _mutex = xSemaphoreCreateMutex();
    memset(&_data, 0, sizeof(SensorData));
}

DataManager::~DataManager() {
    if (_mutex != NULL) {
        vSemaphoreDelete(_mutex);
    }
}

SensorData DataManager::getData() {
    SensorData copy;
    memset(&copy, 0, sizeof(SensorData));
    if (xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
        copy = _data;
        xSemaphoreGive(_mutex);
    }
    return copy;
}

void DataManager::updateACVoltage(float voltage, float rawAdc) {
    if (xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
        _data.ac_voltage = voltage;
        _data.zmpt1_adc = rawAdc;
        xSemaphoreGive(_mutex);
    }
}

void DataManager::updateACVoltage2(float voltage, float rawAdc) {
    if (xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
        _data.ac_voltage2 = voltage;
        _data.zmpt2_adc = rawAdc;
        xSemaphoreGive(_mutex);
    }
}

void DataManager::updateACCurrent(float current, float rawAdc) {
    if (xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
        _data.ac_current = current;
        _data.zmct_adc = rawAdc;
        xSemaphoreGive(_mutex);
    }
}

void DataManager::updateACPower(float power) {
    if (xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
        _data.ac_power = power;
        xSemaphoreGive(_mutex);
    }
}

void DataManager::updateDC1(float voltage, float current, float power) {
    if (xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
        _data.ina1_voltage = voltage;
        _data.ina1_current = current;
        _data.ina1_power = power;
        xSemaphoreGive(_mutex);
    }
}

void DataManager::updateDC2(float voltage, float current, float power) {
    if (xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
        _data.ina2_voltage = voltage;
        _data.ina2_current = current;
        _data.ina2_power = power;
        xSemaphoreGive(_mutex);
    }
}

void DataManager::updateTemperature1(float tempC) {
    if (xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
        _data.temperature1 = tempC;
        xSemaphoreGive(_mutex);
    }
}

void DataManager::updateTemperature2(float tempC) {
    if (xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
        _data.temperature2 = tempC;
        xSemaphoreGive(_mutex);
    }
}

void DataManager::updateRPM(uint32_t rpm) {
    if (xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
        _data.rpm = rpm;
        xSemaphoreGive(_mutex);
    }
}
