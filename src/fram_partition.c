#include "fram/fram_partition.h"

#include "esp_check.h"
#include <string.h>

#define TAG "fram_pm"
#define FRAM_PM_ERASE_CHUNK 64

static bool fram_pm_ranges_overlap(uint32_t a_start, uint32_t a_end, uint32_t b_start, uint32_t b_end) {
    return (a_start < b_end) && (b_start < a_end);
}

esp_err_t fram_pm_init(fram_pm_t *pm, fram_dev_t *dev,
                       const fram_partition_t *parts, size_t count) {
    if (pm == NULL || dev == NULL || parts == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (count == 0 || count > FRAM_PART_MAX) {
        return ESP_ERR_INVALID_SIZE;
    }

    memset(pm, 0, sizeof(*pm));
    pm->dev = dev;

    uint32_t dev_size = fram_dev_get_size(dev);
    if (dev_size == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    for (size_t i = 0; i < count; i++) {
        const fram_partition_t *src = &parts[i];
        if (src->size == 0) {
            return ESP_ERR_INVALID_SIZE;
        }
        size_t name_len = strnlen(src->name, FRAM_PART_NAME_MAX);
        if (name_len == 0 || name_len >= FRAM_PART_NAME_MAX) {
            return ESP_ERR_INVALID_ARG;
        }
        if (src->offset > dev_size || src->size > dev_size || src->offset > dev_size - src->size) {
            return ESP_ERR_INVALID_SIZE;
        }
        pm->partitions[i] = *src;
    }

    for (size_t i = 0; i < count; i++) {
        uint32_t a_start = pm->partitions[i].offset;
        uint32_t a_end = a_start + pm->partitions[i].size;
        for (size_t j = i + 1; j < count; j++) {
            uint32_t b_start = pm->partitions[j].offset;
            uint32_t b_end = b_start + pm->partitions[j].size;
            if (fram_pm_ranges_overlap(a_start, a_end, b_start, b_end)) {
                return ESP_ERR_INVALID_STATE;
            }
        }
    }

    pm->partition_count = count;
    pm->initialized = true;
    return ESP_OK;
}

esp_err_t fram_pm_deinit(fram_pm_t *pm) {
    if (pm == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    pm->initialized = false;
    return ESP_OK;
}

const fram_partition_t *fram_pm_find(const fram_pm_t *pm, const char *name) {
    if (pm == NULL || name == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < pm->partition_count; i++) {
        if (strncmp(pm->partitions[i].name, name, FRAM_PART_NAME_MAX) == 0) {
            return &pm->partitions[i];
        }
    }
    return NULL;
}

const fram_partition_t *fram_pm_get(const fram_pm_t *pm, size_t index) {
    if (pm == NULL || index >= pm->partition_count) {
        return NULL;
    }
    return &pm->partitions[index];
}

size_t fram_pm_count(const fram_pm_t *pm) {
    return pm ? pm->partition_count : 0;
}

bool fram_pm_is_valid_range(const fram_partition_t *part, uint32_t offset, size_t len) {
    if (part == NULL) {
        return false;
    }
    if (len == 0) {
        return true;
    }
    if (offset > part->size || len > part->size || offset > part->size - len) {
        return false;
    }
    return true;
}

esp_err_t fram_pm_read(fram_pm_t *pm, const fram_partition_t *part,
                       uint32_t offset, void *buf, size_t len) {
    if (pm == NULL || part == NULL || buf == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!pm->initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!fram_pm_is_valid_range(part, offset, len)) {
        return ESP_ERR_INVALID_SIZE;
    }
    return fram_dev_read(pm->dev, part->offset + offset, buf, len);
}

esp_err_t fram_pm_write(fram_pm_t *pm, const fram_partition_t *part,
                        uint32_t offset, const void *buf, size_t len) {
    if (pm == NULL || part == NULL || buf == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!pm->initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (part->flags & FRAM_PART_FLAG_READONLY) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!fram_pm_is_valid_range(part, offset, len)) {
        return ESP_ERR_INVALID_SIZE;
    }
    return fram_dev_write(pm->dev, part->offset + offset, buf, len);
}

esp_err_t fram_pm_erase(fram_pm_t *pm, const fram_partition_t *part) {
    if (pm == NULL || part == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!pm->initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (part->flags & FRAM_PART_FLAG_READONLY) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t buf[FRAM_PM_ERASE_CHUNK];
    memset(buf, 0xFF, sizeof(buf));

    uint32_t offset = 0;
    uint32_t remaining = part->size;
    while (remaining > 0) {
        uint32_t chunk = remaining > FRAM_PM_ERASE_CHUNK ? FRAM_PM_ERASE_CHUNK : remaining;
        esp_err_t err = fram_pm_write(pm, part, offset, buf, chunk);
        if (err != ESP_OK) {
            return err;
        }
        offset += chunk;
        remaining -= chunk;
    }
    return ESP_OK;
}
