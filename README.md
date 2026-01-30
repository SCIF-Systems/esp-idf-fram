# FRAM Component (FM25V02A, ESP-IDF)

Production-grade FRAM storage for ESP-IDF with a clean layered architecture:

- **HAL**: FM25V02A SPI backend + mock backend (tests)
- **Device**: mutex + chunking + stats + health
- **Partitions**: named ranges with bounds checks
- **Primitives**: ring log, versioned slots, KVS, optional A/B superblock

## Status

This component is experimental and currently untested. Expect rough edges and
possible API changes.

## Design Principles

- **No heap**: all objects are caller-owned; mutexes are static.
- **Crash-safe**: records use CRC + commit byte written last.
- **Simple**: small APIs that map to FRAM semantics.

## Supported ESP-IDF

- **Tested with**: ESP-IDF v5.5.1

## Quick Start (FM25V02A)

```c
#include "fram/fram.h"

static fram_hal_t s_hal;
static fram_hal_spi_ctx_t s_hal_ctx;
static fram_dev_t s_dev;
static fram_pm_t s_pm;

static fram_partition_t s_parts[] = {
    { .name = "events", .offset = 0x0400, .size = 0x2000 },
    { .name = "config", .offset = 0x2400, .size = 0x0800 },
    { .name = "kvs",    .offset = 0x2C00, .size = 0x0400 },
};

void fram_init(void) {
    fram_hal_spi_config_t hal_cfg = {
        .host = SPI2_HOST,
        .cs_pin = 5,
        .mosi_pin = 23,
        .miso_pin = 19,
        .sclk_pin = 18,
        .spi_mode = 0,
        .freq_hz = 20000000,
        .size_bytes = 0,          // auto-detect (RDID)
        .max_transfer = 256,
        .powerup_delay_ms = 0,
        .init_bus = true,
        .deinit_bus = false,
    };

    fram_hal_spi_create(&s_hal, &s_hal_ctx, &hal_cfg);

    fram_dev_config_t dev_cfg = {
        .hal = &s_hal,
        .error_threshold = 3,
        .mutex_timeout_ms = 1000,
    };
    fram_dev_init(&s_dev, &dev_cfg);

    fram_pm_init(&s_pm, &s_dev, s_parts, sizeof(s_parts) / sizeof(s_parts[0]));
}
```

## Optional Superblock (A/B)

Use `fram_superblock_write()` to persist a self-describing partition table. Reserve
`fram_superblock_storage_size()` bytes at a fixed offset (commonly 0). Reads select
the newest valid copy by sequence.

## Ring / VSlot / KVS

- **Ring**: append-only circular log with crash recovery
- **VSlot**: 2–3 slot versioned buffer (latest wins)
- **KVS**: append-only key/value log, linear scan, tombstones

For ring/vslot length queries, use `fram_ring_peek_oldest_len`,
`fram_ring_peek_newest_len`, and `fram_vslot_peek_len`.

## Tests

Component tests live in `test/` and use the mock HAL. Enable
`CONFIG_FRAM_HAL_MOCK_ENABLED=y` when running tests.

## Examples

A minimal example project is available in `examples/fram_basic`.

## Configuration (Kconfig)

Key options:

- `CONFIG_FRAM_HAL_SPI_ENABLED`
- `CONFIG_FRAM_HAL_MOCK_ENABLED`
- `CONFIG_FRAM_SPI_MAX_TRANSFER`
- `CONFIG_FRAM_RING_MAX_PAYLOAD`
- `CONFIG_FRAM_VSLOT_MAX_PAYLOAD`
- `CONFIG_FRAM_KVS_MAX_VALUE`
