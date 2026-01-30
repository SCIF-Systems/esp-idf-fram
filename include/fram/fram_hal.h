#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct fram_hal fram_hal_t;

// HAL operations
typedef esp_err_t (*fram_hal_init_fn)(fram_hal_t *hal);
typedef esp_err_t (*fram_hal_deinit_fn)(fram_hal_t *hal);
typedef esp_err_t (*fram_hal_read_fn)(fram_hal_t *hal, uint32_t addr, void *buf, size_t len);
typedef esp_err_t (*fram_hal_write_fn)(fram_hal_t *hal, uint32_t addr, const void *buf, size_t len);
typedef esp_err_t (*fram_hal_probe_fn)(fram_hal_t *hal);

struct fram_hal {
    fram_hal_init_fn   init;
    fram_hal_deinit_fn deinit;
    fram_hal_read_fn   read;
    fram_hal_write_fn  write;
    fram_hal_probe_fn  probe;

    uint32_t size_bytes;   // Total capacity
    uint32_t max_transfer; // Max data bytes per transaction

    void *ctx;
};

// SPI HAL (FM25V02A)
#if CONFIG_FRAM_HAL_SPI_ENABLED
#include "driver/spi_master.h"

typedef struct {
    spi_host_device_t host;
    int cs_pin;
    int mosi_pin;
    int miso_pin;
    int sclk_pin;
    int spi_mode;           // 0 or 3
    uint32_t freq_hz;
    uint32_t size_bytes;    // 0 = auto-detect via RDID
    size_t max_transfer;    // max data bytes per transaction
    uint32_t powerup_delay_ms;
    bool init_bus;          // initialize SPI bus
    bool deinit_bus;        // free SPI bus on deinit
} fram_hal_spi_config_t;

typedef struct {
    spi_host_device_t host;
    spi_device_handle_t dev;
    bool bus_inited;
    bool deinit_bus;
} fram_hal_spi_ctx_t;

esp_err_t fram_hal_spi_create(fram_hal_t *hal,
                              fram_hal_spi_ctx_t *ctx,
                              const fram_hal_spi_config_t *cfg);
#endif

// Mock HAL (tests)
#if CONFIG_FRAM_HAL_MOCK_ENABLED

typedef struct {
    uint8_t *buffer;     // backing store
    size_t buffer_len;   // size of backing buffer
    size_t size_bytes;   // usable size (<= buffer_len)
} fram_hal_mock_config_t;

typedef struct {
    uint8_t *buffer;
    size_t size_bytes;
    uint32_t op_count;
    uint32_t fail_after;
    bool fail_enabled;
    uint32_t inject_offset;
    size_t inject_len;
    bool inject_enabled;
} fram_hal_mock_ctx_t;

esp_err_t fram_hal_mock_create(fram_hal_t *hal,
                               fram_hal_mock_ctx_t *ctx,
                               const fram_hal_mock_config_t *cfg);
uint8_t *fram_hal_mock_get_buffer(fram_hal_t *hal);
void fram_hal_mock_fill(fram_hal_t *hal, uint8_t value);
void fram_hal_mock_set_fail_after(fram_hal_t *hal, uint32_t operations);
void fram_hal_mock_inject_error(fram_hal_t *hal, uint32_t offset, size_t len);
#endif
