#pragma once

#include "fram/fram_partition.h"
#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

#define FRAM_SUPERBLOCK_MAGIC 0x4D415246U // "FRAM"
#define FRAM_SUPERBLOCK_VERSION 1U
#define FRAM_SUPERBLOCK_COMMIT 0xA5

// Requires 2 * sizeof(fram_superblock_t) bytes reserved for A/B copies.

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t count;
    uint32_t seq;
    uint32_t size_bytes;
    fram_partition_t parts[FRAM_PART_MAX];
    uint32_t crc32;
    uint8_t commit;
    uint8_t reserved[3];
} __attribute__((packed)) fram_superblock_t;

// Read the most recent valid copy (highest seq). Returns ESP_ERR_NOT_FOUND if neither is valid.
esp_err_t fram_superblock_read(fram_dev_t *dev, uint32_t base_offset, fram_superblock_t *out);

// Write a new superblock to the older/invalid copy and bump seq.
esp_err_t fram_superblock_write(fram_dev_t *dev, uint32_t base_offset, const fram_superblock_t *sb);

// Compute CRC32 for a superblock (excluding crc32 field).
uint32_t fram_superblock_crc(const fram_superblock_t *sb);

// Size reserved for A/B copies.
static inline size_t fram_superblock_storage_size(void) {
    return sizeof(fram_superblock_t) * 2U;
}
