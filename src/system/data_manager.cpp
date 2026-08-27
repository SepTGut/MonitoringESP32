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
    if (_mutex == NULL) {
        _mutex = xSemaphoreCreateMutex();
    }
    if (_mutex != NULL && xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
        copy = _data;
        xSemaphoreGive(_mutex);
    }
    return copy;
}

void DataManager::publish(const SensorData& data) {
    if (_mutex == NULL) {
        _mutex = xSemaphoreCreateMutex();
    }
    if (_mutex != NULL && xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
        const uint8_t i2cCount = _data.i2c_count;
        uint8_t i2cAddresses[16];
        memcpy(i2cAddresses, _data.i2c_addresses, sizeof(i2cAddresses));
        _data = data;
        _data.i2c_count = i2cCount;
        memcpy(_data.i2c_addresses, i2cAddresses, sizeof(i2cAddresses));
        xSemaphoreGive(_mutex);
    }
}

void DataManager::updateI2CAddresses(const uint8_t* addresses, uint8_t count) {
    if (addresses == nullptr && count > 0) return;
    if (_mutex == NULL) {
        _mutex = xSemaphoreCreateMutex();
    }
    if (_mutex != NULL && xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
        _data.i2c_count = count < 16 ? count : 16;
        for (uint8_t i = 0; i < _data.i2c_count; i++) {
            _data.i2c_addresses[i] = addresses[i];
        }
        xSemaphoreGive(_mutex);
    }
}
