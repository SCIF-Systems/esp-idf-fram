#include "fram/fram_dev.h"

#include "esp_check.h"
#include "sdkconfig.h"
#include <string.h>

#define TAG "fram_dev"

static esp_err_t fram_dev_lock(fram_dev_t *dev) {
    if (dev == NULL || dev->mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(dev->mutex, pdMS_TO_TICKS(dev->mutex_timeout_ms)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

static void fram_dev_unlock(fram_dev_t *dev) {
    if (dev && dev->mutex) {
        xSemaphoreGive(dev->mutex);
    }
}

static void fram_dev_record_error(fram_dev_t *dev) {
    dev->error_count++;
    dev->consecutive_errors++;
    if (dev->consecutive_errors >= dev->error_threshold) {
        dev->healthy = false;
    }
}

static void fram_dev_record_success(fram_dev_t *dev) {
    dev->consecutive_errors = 0;
}

esp_err_t fram_dev_init(fram_dev_t *dev, const fram_dev_config_t *cfg) {
    if (dev == NULL || cfg == NULL || cfg->hal == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(dev, 0, sizeof(*dev));
    dev->hal = cfg->hal;
    dev->error_threshold = cfg->error_threshold ? cfg->error_threshold : CONFIG_FRAM_DEFAULT_ERROR_THRESHOLD;
    dev->mutex_timeout_ms = cfg->mutex_timeout_ms ? cfg->mutex_timeout_ms : CONFIG_FRAM_DEFAULT_MUTEX_TIMEOUT_MS;
    dev->healthy = true;

    dev->mutex = xSemaphoreCreateMutexStatic(&dev->mutex_buf);
    if (dev->mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    if (dev->hal->init) {
        ESP_RETURN_ON_ERROR(dev->hal->init(dev->hal), TAG, "HAL init failed");
    }
    if (dev->hal->probe) {
        ESP_RETURN_ON_ERROR(dev->hal->probe(dev->hal), TAG, "HAL probe failed");
    }

    if (dev->hal->size_bytes == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    return ESP_OK;
}

esp_err_t fram_dev_deinit(fram_dev_t *dev) {
    if (dev == NULL || dev->hal == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (dev->hal->deinit) {
        dev->hal->deinit(dev->hal);
    }
    dev->healthy = false;
    return ESP_OK;
}

esp_err_t fram_dev_read(fram_dev_t *dev, uint32_t offset, void *buf, size_t len) {
    if (dev == NULL || dev->hal == NULL || buf == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (len == 0) {
        return ESP_OK;
    }
    uint32_t size_bytes = dev->hal->size_bytes;
    if (size_bytes == 0 || offset > size_bytes || len > size_bytes || offset > size_bytes - len) {
        return ESP_ERR_INVALID_SIZE;
    }

    esp_err_t err = fram_dev_lock(dev);
    if (err != ESP_OK) {
        fram_dev_record_error(dev);
        return err;
    }

    size_t remaining = len;
    uint8_t *out = (uint8_t *)buf;
    uint32_t addr = offset;
    uint32_t max_transfer = dev->hal->max_transfer ? dev->hal->max_transfer : size_bytes;

    while (remaining > 0) {
        size_t chunk = remaining > max_transfer ? max_transfer : remaining;
        err = dev->hal->read(dev->hal, addr, out, chunk);
        if (err != ESP_OK) {
            fram_dev_record_error(dev);
            break;
        }
        dev->read_count++;
        fram_dev_record_success(dev);
        out += chunk;
        addr += chunk;
        remaining -= chunk;
    }

    fram_dev_unlock(dev);
    return err;
}

esp_err_t fram_dev_write(fram_dev_t *dev, uint32_t offset, const void *buf, size_t len) {
    if (dev == NULL || dev->hal == NULL || buf == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (len == 0) {
        return ESP_OK;
    }
    uint32_t size_bytes = dev->hal->size_bytes;
    if (size_bytes == 0 || offset > size_bytes || len > size_bytes || offset > size_bytes - len) {
        return ESP_ERR_INVALID_SIZE;
    }

    esp_err_t err = fram_dev_lock(dev);
    if (err != ESP_OK) {
        fram_dev_record_error(dev);
        return err;
    }

    size_t remaining = len;
    const uint8_t *in = (const uint8_t *)buf;
    uint32_t addr = offset;
    uint32_t max_transfer = dev->hal->max_transfer ? dev->hal->max_transfer : size_bytes;

    while (remaining > 0) {
        size_t chunk = remaining > max_transfer ? max_transfer : remaining;
        err = dev->hal->write(dev->hal, addr, in, chunk);
        if (err != ESP_OK) {
            fram_dev_record_error(dev);
            break;
        }
        dev->write_count++;
        fram_dev_record_success(dev);
        in += chunk;
        addr += chunk;
        remaining -= chunk;
    }

    fram_dev_unlock(dev);
    return err;
}

esp_err_t fram_dev_read_u8(fram_dev_t *dev, uint32_t offset, uint8_t *val) {
    return fram_dev_read(dev, offset, val, sizeof(*val));
}

esp_err_t fram_dev_read_u16(fram_dev_t *dev, uint32_t offset, uint16_t *val) {
    return fram_dev_read(dev, offset, val, sizeof(*val));
}

esp_err_t fram_dev_read_u32(fram_dev_t *dev, uint32_t offset, uint32_t *val) {
    return fram_dev_read(dev, offset, val, sizeof(*val));
}

esp_err_t fram_dev_read_u64(fram_dev_t *dev, uint32_t offset, uint64_t *val) {
    return fram_dev_read(dev, offset, val, sizeof(*val));
}

esp_err_t fram_dev_write_u8(fram_dev_t *dev, uint32_t offset, uint8_t val) {
    return fram_dev_write(dev, offset, &val, sizeof(val));
}

esp_err_t fram_dev_write_u16(fram_dev_t *dev, uint32_t offset, uint16_t val) {
    return fram_dev_write(dev, offset, &val, sizeof(val));
}

esp_err_t fram_dev_write_u32(fram_dev_t *dev, uint32_t offset, uint32_t val) {
    return fram_dev_write(dev, offset, &val, sizeof(val));
}

esp_err_t fram_dev_write_u64(fram_dev_t *dev, uint32_t offset, uint64_t val) {
    return fram_dev_write(dev, offset, &val, sizeof(val));
}

bool fram_dev_is_healthy(const fram_dev_t *dev) {
    return dev ? dev->healthy : false;
}

uint32_t fram_dev_get_size(const fram_dev_t *dev) {
    if (dev == NULL || dev->hal == NULL) {
        return 0;
    }
    return dev->hal->size_bytes;
}

void fram_dev_get_stats(const fram_dev_t *dev, fram_dev_stats_t *stats) {
    if (dev == NULL || stats == NULL) {
        return;
    }
    stats->read_count = dev->read_count;
    stats->write_count = dev->write_count;
    stats->error_count = dev->error_count;
    stats->size_bytes = dev->hal ? dev->hal->size_bytes : 0;
    stats->healthy = dev->healthy;
}

void fram_dev_reset_stats(fram_dev_t *dev) {
    if (dev == NULL) {
        return;
    }
    dev->read_count = 0;
    dev->write_count = 0;
    dev->error_count = 0;
    dev->consecutive_errors = 0;
    dev->healthy = true;
}
