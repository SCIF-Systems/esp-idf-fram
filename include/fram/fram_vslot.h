#pragma once

#include "fram/fram_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint64_t ts_us;
    uint32_t len;
    uint32_t crc32;
} __attribute__((packed)) fram_vslot_header_t;

typedef struct {
    fram_pm_t *pm;
    const fram_partition_t *part;

    uint32_t slot_count;
    uint32_t max_payload;
    uint32_t slot_size;
    uint32_t magic;

    uint32_t active_slot;
    uint32_t active_version;
    bool has_data;

    SemaphoreHandle_t mutex;
    StaticSemaphore_t mutex_buf;
} fram_vslot_t;

typedef struct {
    fram_pm_t *pm;
    const char *partition_name;
    uint32_t max_payload;
    uint32_t slot_count; // 2 or 3
    uint32_t magic;
} fram_vslot_config_t;

esp_err_t fram_vslot_init(fram_vslot_t *vs, const fram_vslot_config_t *cfg);
esp_err_t fram_vslot_deinit(fram_vslot_t *vs);

esp_err_t fram_vslot_load(fram_vslot_t *vs, void *payload, size_t *len);
esp_err_t fram_vslot_save(fram_vslot_t *vs, const void *payload, size_t len);
esp_err_t fram_vslot_peek_len(fram_vslot_t *vs, size_t *len);

bool fram_vslot_has_data(const fram_vslot_t *vs);
uint32_t fram_vslot_get_version(const fram_vslot_t *vs);
esp_err_t fram_vslot_clear(fram_vslot_t *vs);
