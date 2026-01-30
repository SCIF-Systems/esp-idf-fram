#pragma once

#include "fram/fram_hal.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    fram_hal_t *hal;
    SemaphoreHandle_t mutex;
    StaticSemaphore_t mutex_buf;

    uint32_t read_count;
    uint32_t write_count;
    uint32_t error_count;
    uint32_t consecutive_errors;
    bool healthy;

    uint32_t error_threshold;
    uint32_t mutex_timeout_ms;
} fram_dev_t;

typedef struct {
    fram_hal_t *hal;
    uint32_t error_threshold;  // default: CONFIG_FRAM_DEFAULT_ERROR_THRESHOLD
    uint32_t mutex_timeout_ms; // default: CONFIG_FRAM_DEFAULT_MUTEX_TIMEOUT_MS
} fram_dev_config_t;

esp_err_t fram_dev_init(fram_dev_t *dev, const fram_dev_config_t *cfg);
esp_err_t fram_dev_deinit(fram_dev_t *dev);

esp_err_t fram_dev_read(fram_dev_t *dev, uint32_t offset, void *buf, size_t len);
esp_err_t fram_dev_write(fram_dev_t *dev, uint32_t offset, const void *buf, size_t len);

esp_err_t fram_dev_read_u8(fram_dev_t *dev, uint32_t offset, uint8_t *val);
esp_err_t fram_dev_read_u16(fram_dev_t *dev, uint32_t offset, uint16_t *val);
esp_err_t fram_dev_read_u32(fram_dev_t *dev, uint32_t offset, uint32_t *val);
esp_err_t fram_dev_read_u64(fram_dev_t *dev, uint32_t offset, uint64_t *val);
esp_err_t fram_dev_write_u8(fram_dev_t *dev, uint32_t offset, uint8_t val);
esp_err_t fram_dev_write_u16(fram_dev_t *dev, uint32_t offset, uint16_t val);
esp_err_t fram_dev_write_u32(fram_dev_t *dev, uint32_t offset, uint32_t val);
esp_err_t fram_dev_write_u64(fram_dev_t *dev, uint32_t offset, uint64_t val);

bool fram_dev_is_healthy(const fram_dev_t *dev);
uint32_t fram_dev_get_size(const fram_dev_t *dev);

typedef struct {
    uint32_t read_count;
    uint32_t write_count;
    uint32_t error_count;
    uint32_t size_bytes;
    bool healthy;
} fram_dev_stats_t;

void fram_dev_get_stats(const fram_dev_t *dev, fram_dev_stats_t *stats);
void fram_dev_reset_stats(fram_dev_t *dev);
