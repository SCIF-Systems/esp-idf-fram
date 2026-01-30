#pragma once

#include "fram/fram_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t magic;
    uint32_t seq;
    uint64_t ts_us;
    uint16_t len;
    uint16_t reserved;
    uint32_t crc32;
} __attribute__((packed)) fram_ring_header_t;

typedef struct {
    fram_pm_t *pm;
    const fram_partition_t *part;

    uint32_t entry_size;
    uint32_t max_payload;
    uint32_t capacity;
    uint32_t magic;

    uint32_t head_slot;
    uint32_t tail_slot;
    uint32_t head_seq;
    uint32_t count;

    SemaphoreHandle_t mutex;
    StaticSemaphore_t mutex_buf;
    bool ready;
} fram_ring_t;

typedef struct {
    fram_pm_t *pm;
    const char *partition_name;
    uint32_t max_payload;
    uint32_t magic;
} fram_ring_config_t;

esp_err_t fram_ring_init(fram_ring_t *ring, const fram_ring_config_t *cfg);
esp_err_t fram_ring_deinit(fram_ring_t *ring);

esp_err_t fram_ring_append(fram_ring_t *ring, const void *payload, size_t len);

esp_err_t fram_ring_peek_oldest(fram_ring_t *ring, void *payload, size_t *len,
                                uint32_t *seq, uint64_t *ts_us);
esp_err_t fram_ring_peek_newest(fram_ring_t *ring, void *payload, size_t *len,
                                uint32_t *seq, uint64_t *ts_us);
esp_err_t fram_ring_peek_oldest_len(fram_ring_t *ring, size_t *len);
esp_err_t fram_ring_peek_newest_len(fram_ring_t *ring, size_t *len);

typedef esp_err_t (*fram_ring_iter_fn)(uint32_t seq, uint64_t ts_us,
                                       const void *payload, size_t len, void *ctx);
esp_err_t fram_ring_iterate(fram_ring_t *ring, fram_ring_iter_fn cb, void *ctx);

esp_err_t fram_ring_clear(fram_ring_t *ring);
uint32_t fram_ring_count(const fram_ring_t *ring);
uint32_t fram_ring_capacity(const fram_ring_t *ring);
bool fram_ring_is_full(const fram_ring_t *ring);
bool fram_ring_is_empty(const fram_ring_t *ring);
