#include "esp_err.h"
#include "esp_log.h"
#include "fram/fram.h"

#define TAG "fram_basic"

#ifndef FRAM_SPI_HOST
#ifdef SPI2_HOST
#define FRAM_SPI_HOST SPI2_HOST
#else
#define FRAM_SPI_HOST SPI3_HOST
#endif
#endif

#ifndef FRAM_PIN_CS
#define FRAM_PIN_CS 5
#endif
#ifndef FRAM_PIN_MOSI
#define FRAM_PIN_MOSI 23
#endif
#ifndef FRAM_PIN_MISO
#define FRAM_PIN_MISO 19
#endif
#ifndef FRAM_PIN_SCLK
#define FRAM_PIN_SCLK 18
#endif

static fram_hal_t s_hal;
static fram_hal_spi_ctx_t s_hal_ctx;
static fram_dev_t s_dev;
static fram_pm_t s_pm;
static fram_ring_t s_ring;

static fram_partition_t s_parts[] = {
    { .name = "log",    .offset = 0x0400, .size = 0x2000 },
    { .name = "config", .offset = 0x2400, .size = 0x0800 },
};

static void fram_example_init(void) {
    fram_hal_spi_config_t hal_cfg = {
        .host = FRAM_SPI_HOST,
        .cs_pin = FRAM_PIN_CS,
        .mosi_pin = FRAM_PIN_MOSI,
        .miso_pin = FRAM_PIN_MISO,
        .sclk_pin = FRAM_PIN_SCLK,
        .spi_mode = 0,
        .freq_hz = 20000000,
        .size_bytes = 0,
        .max_transfer = 256,
        .powerup_delay_ms = 0,
        .init_bus = true,
        .deinit_bus = false,
    };

    ESP_ERROR_CHECK(fram_hal_spi_create(&s_hal, &s_hal_ctx, &hal_cfg));

    fram_dev_config_t dev_cfg = {
        .hal = &s_hal,
        .error_threshold = 3,
        .mutex_timeout_ms = 1000,
    };
    ESP_ERROR_CHECK(fram_dev_init(&s_dev, &dev_cfg));

    ESP_ERROR_CHECK(fram_pm_init(&s_pm, &s_dev, s_parts, sizeof(s_parts) / sizeof(s_parts[0])));

    fram_ring_config_t ring_cfg = {
        .pm = &s_pm,
        .partition_name = "log",
        .max_payload = 32,
        .magic = 0x4C4F4747, // "LOGG"
    };
    ESP_ERROR_CHECK(fram_ring_init(&s_ring, &ring_cfg));
}

void app_main(void) {
    fram_example_init();

    const char msg[] = "hello fram";
    ESP_ERROR_CHECK(fram_ring_append(&s_ring, msg, sizeof(msg)));

    char buf[32] = {0};
    size_t len = sizeof(buf);
    uint32_t seq = 0;
    uint64_t ts = 0;
    esp_err_t err = fram_ring_peek_newest(&s_ring, buf, &len, &seq, &ts);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "latest seq=%u len=%u payload='%s'", seq, (unsigned)len, buf);
    } else {
        ESP_LOGW(TAG, "peek failed: %s", esp_err_to_name(err));
    }
}
