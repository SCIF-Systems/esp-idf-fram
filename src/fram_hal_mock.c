#include "fram/fram_hal.h"

#if CONFIG_FRAM_HAL_MOCK_ENABLED

#include "esp_check.h"
#include <string.h>

#define TAG "fram_hal_mock"

static bool fram_mock_should_fail(fram_hal_mock_ctx_t *ctx) {
    if (!ctx->fail_enabled) {
        return false;
    }
    if (ctx->op_count >= ctx->fail_after) {
        return true;
    }
    return false;
}

static esp_err_t fram_hal_mock_noop_init(fram_hal_t *hal) {
    (void)hal;
    return ESP_OK;
}

static esp_err_t fram_hal_mock_deinit(fram_hal_t *hal) {
    (void)hal;
    return ESP_OK;
}

static esp_err_t fram_hal_mock_probe(fram_hal_t *hal) {
    if (hal == NULL || hal->ctx == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

static esp_err_t fram_hal_mock_read(fram_hal_t *hal, uint32_t addr, void *buf, size_t len) {
    if (hal == NULL || hal->ctx == NULL || buf == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (len == 0) {
        return ESP_OK;
    }
    if (addr > hal->size_bytes || len > hal->size_bytes || addr > hal->size_bytes - len) {
        return ESP_ERR_INVALID_SIZE;
    }

    fram_hal_mock_ctx_t *ctx = (fram_hal_mock_ctx_t *)hal->ctx;
    ctx->op_count++;
    if (fram_mock_should_fail(ctx)) {
        return ESP_FAIL;
    }

    memcpy(buf, &ctx->buffer[addr], len);

    if (ctx->inject_enabled) {
        uint32_t start = addr;
        uint32_t end = addr + (uint32_t)len;
        uint32_t inj_start = ctx->inject_offset;
        uint32_t inj_end = ctx->inject_offset + (uint32_t)ctx->inject_len;

        if (end > inj_start && start < inj_end) {
            uint32_t overlap_start = start > inj_start ? start : inj_start;
            uint32_t overlap_end = end < inj_end ? end : inj_end;
            uint8_t *out = (uint8_t *)buf;
            for (uint32_t i = overlap_start; i < overlap_end; i++) {
                out[i - start] ^= 0xFF;
            }
        }
    }

    return ESP_OK;
}

static esp_err_t fram_hal_mock_write(fram_hal_t *hal, uint32_t addr, const void *buf, size_t len) {
    if (hal == NULL || hal->ctx == NULL || buf == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (len == 0) {
        return ESP_OK;
    }
    if (addr > hal->size_bytes || len > hal->size_bytes || addr > hal->size_bytes - len) {
        return ESP_ERR_INVALID_SIZE;
    }

    fram_hal_mock_ctx_t *ctx = (fram_hal_mock_ctx_t *)hal->ctx;
    ctx->op_count++;
    if (fram_mock_should_fail(ctx)) {
        return ESP_FAIL;
    }

    memcpy(&ctx->buffer[addr], buf, len);
    return ESP_OK;
}

esp_err_t fram_hal_mock_create(fram_hal_t *hal,
                               fram_hal_mock_ctx_t *ctx,
                               const fram_hal_mock_config_t *cfg) {
    if (hal == NULL || ctx == NULL || cfg == NULL || cfg->buffer == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (cfg->size_bytes == 0 || cfg->size_bytes > cfg->buffer_len) {
        return ESP_ERR_INVALID_SIZE;
    }

    memset(hal, 0, sizeof(*hal));
    memset(ctx, 0, sizeof(*ctx));

    ctx->buffer = cfg->buffer;
    ctx->size_bytes = cfg->size_bytes;
    ctx->fail_after = 0;
    ctx->fail_enabled = false;

    hal->init = fram_hal_mock_noop_init;
    hal->deinit = fram_hal_mock_deinit;
    hal->read = fram_hal_mock_read;
    hal->write = fram_hal_mock_write;
    hal->probe = fram_hal_mock_probe;
    hal->size_bytes = (uint32_t)cfg->size_bytes;
    hal->max_transfer = (uint32_t)cfg->size_bytes;
    hal->ctx = ctx;

    return ESP_OK;
}

uint8_t *fram_hal_mock_get_buffer(fram_hal_t *hal) {
    if (hal == NULL || hal->ctx == NULL) {
        return NULL;
    }
    fram_hal_mock_ctx_t *ctx = (fram_hal_mock_ctx_t *)hal->ctx;
    return ctx->buffer;
}

void fram_hal_mock_fill(fram_hal_t *hal, uint8_t value) {
    if (hal == NULL || hal->ctx == NULL) {
        return;
    }
    fram_hal_mock_ctx_t *ctx = (fram_hal_mock_ctx_t *)hal->ctx;
    memset(ctx->buffer, value, ctx->size_bytes);
}

void fram_hal_mock_set_fail_after(fram_hal_t *hal, uint32_t operations) {
    if (hal == NULL || hal->ctx == NULL) {
        return;
    }
    fram_hal_mock_ctx_t *ctx = (fram_hal_mock_ctx_t *)hal->ctx;
    ctx->fail_after = operations;
    ctx->fail_enabled = true;
}

void fram_hal_mock_inject_error(fram_hal_t *hal, uint32_t offset, size_t len) {
    if (hal == NULL || hal->ctx == NULL) {
        return;
    }
    fram_hal_mock_ctx_t *ctx = (fram_hal_mock_ctx_t *)hal->ctx;
    ctx->inject_offset = offset;
    ctx->inject_len = len;
    ctx->inject_enabled = true;
}

#endif // CONFIG_FRAM_HAL_MOCK_ENABLED
