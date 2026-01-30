#include "fram/fram_hal.h"

#if CONFIG_FRAM_HAL_SPI_ENABLED

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include <string.h>

#define TAG "fram_hal_spi"

#define FM25V02A_CMD_WREN  0x06
#define FM25V02A_CMD_WRDI  0x04
#define FM25V02A_CMD_RDSR  0x05
#define FM25V02A_CMD_WRSR  0x01
#define FM25V02A_CMD_READ  0x03
#define FM25V02A_CMD_WRITE 0x02
#define FM25V02A_CMD_RDID  0x9F
#define FM25V02A_CMD_SLEEP 0xB9

#define FM25V02A_SIZE_BYTES (32 * 1024U)
#define FM25V02A_MANUF_ID   0xC2
#define FM25V02A_RDID_LEN   9
#define FM25V02A_FAMILY_ID  0x22
#define FM25V02A_PROD_ID_A  0x08
#define FM25V02A_PROD_ID_B  0x48

#define FRAM_SPI_BUF_MAX ((CONFIG_FRAM_SPI_MAX_TRANSFER) > 0 ? (CONFIG_FRAM_SPI_MAX_TRANSFER) : 1)

static esp_err_t fram_hal_spi_noop_init(fram_hal_t *hal) {
    (void)hal;
    return ESP_OK;
}

static esp_err_t fram_hal_spi_deinit(fram_hal_t *hal) {
    if (hal == NULL || hal->ctx == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    fram_hal_spi_ctx_t *ctx = (fram_hal_spi_ctx_t *)hal->ctx;
    if (ctx->dev != NULL) {
        spi_bus_remove_device(ctx->dev);
        ctx->dev = NULL;
    }
    if (ctx->deinit_bus && ctx->bus_inited) {
        spi_bus_free(ctx->host);
        ctx->bus_inited = false;
    }
    return ESP_OK;
}

static esp_err_t fram_hal_spi_write_enable(fram_hal_spi_ctx_t *ctx) {
    uint8_t cmd = FM25V02A_CMD_WREN;
    spi_transaction_t t = {
        .length = 8,
        .tx_buffer = &cmd,
    };
    return spi_device_transmit(ctx->dev, &t);
}

static esp_err_t fram_hal_spi_read_id(fram_hal_spi_ctx_t *ctx, uint8_t *id, size_t len) {
    if (len < FM25V02A_RDID_LEN) {
        return ESP_ERR_INVALID_SIZE;
    }

    uint8_t tx[1 + FM25V02A_RDID_LEN];
    uint8_t rx[1 + FM25V02A_RDID_LEN];
    memset(tx, 0, sizeof(tx));
    tx[0] = FM25V02A_CMD_RDID;

    spi_transaction_t t = {
        .length = (sizeof(tx) * 8),
        .tx_buffer = tx,
        .rx_buffer = rx,
    };

    esp_err_t err = spi_device_transmit(ctx->dev, &t);
    if (err != ESP_OK) {
        return err;
    }

    memcpy(id, &rx[1], FM25V02A_RDID_LEN);
    return ESP_OK;
}

static esp_err_t fram_hal_spi_probe(fram_hal_t *hal) {
    if (hal == NULL || hal->ctx == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    fram_hal_spi_ctx_t *ctx = (fram_hal_spi_ctx_t *)hal->ctx;
    uint8_t id[FM25V02A_RDID_LEN];
    esp_err_t err = fram_hal_spi_read_id(ctx, id, sizeof(id));
    if (err != ESP_OK) {
        return err;
    }

    if (id[6] != FM25V02A_MANUF_ID || id[7] != FM25V02A_FAMILY_ID) {
        ESP_LOGW(TAG, "unexpected RDID bytes: %02X %02X %02X", id[6], id[7], id[8]);
        return ESP_ERR_NOT_FOUND;
    }

    if (hal->size_bytes == 0) {
        hal->size_bytes = FM25V02A_SIZE_BYTES;
    }

    return ESP_OK;
}

static esp_err_t fram_hal_spi_read(fram_hal_t *hal, uint32_t addr, void *buf, size_t len) {
    if (hal == NULL || hal->ctx == NULL || buf == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (len == 0) {
        return ESP_OK;
    }
    if (hal->size_bytes == 0 || addr > hal->size_bytes || len > hal->size_bytes ||
        addr > hal->size_bytes - len) {
        return ESP_ERR_INVALID_SIZE;
    }

    fram_hal_spi_ctx_t *ctx = (fram_hal_spi_ctx_t *)hal->ctx;
    uint8_t *out = (uint8_t *)buf;
    size_t remaining = len;
    uint32_t offset = addr;

    while (remaining > 0) {
        size_t chunk = remaining;
        if (chunk > hal->max_transfer) {
            chunk = hal->max_transfer;
        }
        if (chunk > (size_t)FRAM_SPI_BUF_MAX) {
            chunk = FRAM_SPI_BUF_MAX;
        }

        uint8_t tx[3 + FRAM_SPI_BUF_MAX];
        uint8_t rx[3 + FRAM_SPI_BUF_MAX];
        tx[0] = FM25V02A_CMD_READ;
        tx[1] = (uint8_t)((offset >> 8) & 0xFF);
        tx[2] = (uint8_t)(offset & 0xFF);
        memset(&tx[3], 0, chunk);

        spi_transaction_t t = {
            .length = (3 + chunk) * 8,
            .tx_buffer = tx,
            .rx_buffer = rx,
        };

        esp_err_t err = spi_device_transmit(ctx->dev, &t);
        if (err != ESP_OK) {
            return err;
        }

        memcpy(out, &rx[3], chunk);
        out += chunk;
        offset += chunk;
        remaining -= chunk;
    }

    return ESP_OK;
}

static esp_err_t fram_hal_spi_write(fram_hal_t *hal, uint32_t addr, const void *buf, size_t len) {
    if (hal == NULL || hal->ctx == NULL || buf == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (len == 0) {
        return ESP_OK;
    }
    if (hal->size_bytes == 0 || addr > hal->size_bytes || len > hal->size_bytes ||
        addr > hal->size_bytes - len) {
        return ESP_ERR_INVALID_SIZE;
    }

    fram_hal_spi_ctx_t *ctx = (fram_hal_spi_ctx_t *)hal->ctx;
    const uint8_t *in = (const uint8_t *)buf;
    size_t remaining = len;
    uint32_t offset = addr;

    while (remaining > 0) {
        size_t chunk = remaining;
        if (chunk > hal->max_transfer) {
            chunk = hal->max_transfer;
        }
        if (chunk > (size_t)FRAM_SPI_BUF_MAX) {
            chunk = FRAM_SPI_BUF_MAX;
        }

        esp_err_t err = fram_hal_spi_write_enable(ctx);
        if (err != ESP_OK) {
            return err;
        }

        uint8_t tx[3 + FRAM_SPI_BUF_MAX];
        tx[0] = FM25V02A_CMD_WRITE;
        tx[1] = (uint8_t)((offset >> 8) & 0xFF);
        tx[2] = (uint8_t)(offset & 0xFF);
        memcpy(&tx[3], in, chunk);

        spi_transaction_t t = {
            .length = (3 + chunk) * 8,
            .tx_buffer = tx,
        };

        err = spi_device_transmit(ctx->dev, &t);
        if (err != ESP_OK) {
            return err;
        }

        in += chunk;
        offset += chunk;
        remaining -= chunk;
    }

    return ESP_OK;
}

esp_err_t fram_hal_spi_create(fram_hal_t *hal,
                              fram_hal_spi_ctx_t *ctx,
                              const fram_hal_spi_config_t *cfg) {
    if (hal == NULL || ctx == NULL || cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(hal, 0, sizeof(*hal));
    memset(ctx, 0, sizeof(*ctx));

    if (cfg->spi_mode != 0 && cfg->spi_mode != 3) {
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t freq_hz = cfg->freq_hz ? cfg->freq_hz : CONFIG_FRAM_SPI_DEFAULT_FREQ_HZ;
    uint32_t max_transfer = cfg->max_transfer ? cfg->max_transfer : CONFIG_FRAM_SPI_MAX_TRANSFER;
    if (max_transfer == 0) {
        max_transfer = 1;
    }

    if (cfg->init_bus) {
        spi_bus_config_t buscfg = {
            .mosi_io_num = cfg->mosi_pin,
            .miso_io_num = cfg->miso_pin,
            .sclk_io_num = cfg->sclk_pin,
            .quadwp_io_num = -1,
            .quadhd_io_num = -1,
            .max_transfer_sz = (int)(max_transfer + 3),
        };

        esp_err_t err = spi_bus_initialize(cfg->host, &buscfg, SPI_DMA_CH_AUTO);
        if (err == ESP_ERR_INVALID_STATE) {
            err = ESP_OK;
        } else if (err == ESP_OK) {
            ctx->bus_inited = true;
        }
        ESP_RETURN_ON_ERROR(err, TAG, "spi_bus_initialize failed");
    }

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = (int)freq_hz,
        .mode = cfg->spi_mode,
        .spics_io_num = cfg->cs_pin,
        .queue_size = 1,
        .flags = 0,
    };

    esp_err_t err = spi_bus_add_device(cfg->host, &devcfg, &ctx->dev);
    ESP_RETURN_ON_ERROR(err, TAG, "spi_bus_add_device failed");

    if (cfg->powerup_delay_ms > 0) {
        vTaskDelay(pdMS_TO_TICKS(cfg->powerup_delay_ms));
    }

    ctx->host = cfg->host;
    ctx->deinit_bus = cfg->deinit_bus;

    hal->init = fram_hal_spi_noop_init;
    hal->deinit = fram_hal_spi_deinit;
    hal->read = fram_hal_spi_read;
    hal->write = fram_hal_spi_write;
    hal->probe = fram_hal_spi_probe;
    hal->size_bytes = cfg->size_bytes;
    hal->max_transfer = max_transfer;
    hal->ctx = ctx;

    return ESP_OK;
}

#endif // CONFIG_FRAM_HAL_SPI_ENABLED
