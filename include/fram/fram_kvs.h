#pragma once

#include "fram/fram_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FRAM_KVS_KEY_MAX 15

typedef struct {
    fram_pm_t *pm;
    const fram_partition_t *part;
    uint32_t magic;
    uint32_t write_offset;
    uint32_t next_seq;
    bool ready;

    SemaphoreHandle_t mutex;
    StaticSemaphore_t mutex_buf;
} fram_kvs_t;

typedef struct {
    fram_pm_t *pm;
    const char *partition_name;
    uint32_t magic;
} fram_kvs_config_t;

esp_err_t fram_kvs_init(fram_kvs_t *kvs, const fram_kvs_config_t *cfg);
esp_err_t fram_kvs_deinit(fram_kvs_t *kvs);

esp_err_t fram_kvs_get(fram_kvs_t *kvs, const char *key, void *buf, size_t *len);
esp_err_t fram_kvs_set(fram_kvs_t *kvs, const char *key, const void *buf, size_t len);
esp_err_t fram_kvs_delete(fram_kvs_t *kvs, const char *key);
bool fram_kvs_exists(fram_kvs_t *kvs, const char *key);
esp_err_t fram_kvs_get_len(fram_kvs_t *kvs, const char *key, size_t *len);

esp_err_t fram_kvs_get_u32(fram_kvs_t *kvs, const char *key, uint32_t *val);
esp_err_t fram_kvs_set_u32(fram_kvs_t *kvs, const char *key, uint32_t val);
esp_err_t fram_kvs_get_str(fram_kvs_t *kvs, const char *key, char *buf, size_t *len);
esp_err_t fram_kvs_set_str(fram_kvs_t *kvs, const char *key, const char *val);
