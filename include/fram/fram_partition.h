#pragma once

#include "fram/fram_dev.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FRAM_PART_NAME_MAX 16
#define FRAM_PART_MAX 16

#define FRAM_PART_FLAG_READONLY (1U << 0)
#define FRAM_PART_FLAG_SYSTEM   (1U << 1)

typedef struct {
    char name[FRAM_PART_NAME_MAX];
    uint32_t offset;
    uint32_t size;
    uint32_t flags;
} fram_partition_t;

typedef struct {
    fram_dev_t *dev;
    fram_partition_t partitions[FRAM_PART_MAX];
    size_t partition_count;
    bool initialized;
} fram_pm_t;

esp_err_t fram_pm_init(fram_pm_t *pm, fram_dev_t *dev,
                       const fram_partition_t *parts, size_t count);
esp_err_t fram_pm_deinit(fram_pm_t *pm);

const fram_partition_t *fram_pm_find(const fram_pm_t *pm, const char *name);
const fram_partition_t *fram_pm_get(const fram_pm_t *pm, size_t index);
size_t fram_pm_count(const fram_pm_t *pm);

esp_err_t fram_pm_read(fram_pm_t *pm, const fram_partition_t *part,
                       uint32_t offset, void *buf, size_t len);
esp_err_t fram_pm_write(fram_pm_t *pm, const fram_partition_t *part,
                        uint32_t offset, const void *buf, size_t len);

esp_err_t fram_pm_erase(fram_pm_t *pm, const fram_partition_t *part);

bool fram_pm_is_valid_range(const fram_partition_t *part, uint32_t offset, size_t len);
