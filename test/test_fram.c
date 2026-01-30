/**
 * @file test_fram.c
 * @brief Component tests for FRAM (mock HAL)
 */

#include "unity.h"
#include "fram/fram.h"
#include <stddef.h>
#include <string.h>

#if CONFIG_FRAM_HAL_MOCK_ENABLED

#define FRAM_TEST_SIZE (32 * 1024)

static uint8_t s_fram_buf[FRAM_TEST_SIZE];
static fram_hal_t s_hal;
static fram_hal_mock_ctx_t s_mock_ctx;
static fram_dev_t s_dev;
static fram_pm_t s_pm;
static fram_partition_t s_parts[3];

static void setup_partitions(void) {
    uint32_t base = (uint32_t)fram_superblock_storage_size();
    s_parts[0] = (fram_partition_t){ .name = "ring", .offset = base, .size = 0x1000 };
    s_parts[1] = (fram_partition_t){ .name = "vslot", .offset = base + 0x1000, .size = 0x0800 };
    s_parts[2] = (fram_partition_t){ .name = "kvs", .offset = base + 0x1800, .size = 0x1000 };
}

void setUp(void) {
    fram_hal_mock_config_t cfg = {
        .buffer = s_fram_buf,
        .buffer_len = sizeof(s_fram_buf),
        .size_bytes = sizeof(s_fram_buf),
    };
    TEST_ASSERT_EQUAL(ESP_OK, fram_hal_mock_create(&s_hal, &s_mock_ctx, &cfg));
    fram_hal_mock_fill(&s_hal, 0xFF);

    fram_dev_config_t dev_cfg = {
        .hal = &s_hal,
        .error_threshold = 3,
        .mutex_timeout_ms = 1000,
    };
    TEST_ASSERT_EQUAL(ESP_OK, fram_dev_init(&s_dev, &dev_cfg));

    setup_partitions();
    TEST_ASSERT_EQUAL(ESP_OK, fram_pm_init(&s_pm, &s_dev, s_parts, 3));
}

void tearDown(void) {
    fram_dev_deinit(&s_dev);
}

TEST_CASE("fram_superblock_ab_commit_recovery", "[fram]") {
    fram_superblock_t sb = {0};
    sb.magic = FRAM_SUPERBLOCK_MAGIC;
    sb.version = FRAM_SUPERBLOCK_VERSION;
    sb.count = 1;
    sb.size_bytes = FRAM_TEST_SIZE;
    sb.parts[0] = s_parts[0];

    TEST_ASSERT_EQUAL(ESP_OK, fram_superblock_write(&s_dev, 0, &sb));
    TEST_ASSERT_EQUAL(ESP_OK, fram_superblock_write(&s_dev, 0, &sb));

    fram_superblock_t a = {0};
    fram_superblock_t b = {0};
    TEST_ASSERT_EQUAL(ESP_OK, fram_dev_read(&s_dev, 0, &a, sizeof(a)));
    TEST_ASSERT_EQUAL(ESP_OK, fram_dev_read(&s_dev, sizeof(a), &b, sizeof(b)));

    uint32_t newest_offset = (a.seq >= b.seq) ? 0 : (uint32_t)sizeof(a);
    uint32_t newest_seq = (a.seq >= b.seq) ? a.seq : b.seq;

    uint8_t *raw = fram_hal_mock_get_buffer(&s_hal);
    raw[newest_offset + offsetof(fram_superblock_t, commit)] = 0x00;

    fram_superblock_t out = {0};
    TEST_ASSERT_EQUAL(ESP_OK, fram_superblock_read(&s_dev, 0, &out));
    TEST_ASSERT_EQUAL_UINT32(newest_seq - 1, out.seq);
}

TEST_CASE("fram_ring_recovery_commit_missing", "[fram]") {
    fram_ring_t ring;
    fram_ring_config_t cfg = {
        .pm = &s_pm,
        .partition_name = "ring",
        .max_payload = 16,
        .magic = 0x52494E47,
    };
    TEST_ASSERT_EQUAL(ESP_OK, fram_ring_init(&ring, &cfg));

    uint32_t val = 0xA5A5A5A5;
    TEST_ASSERT_EQUAL(ESP_OK, fram_ring_append(&ring, &val, sizeof(val)));
    val++;
    TEST_ASSERT_EQUAL(ESP_OK, fram_ring_append(&ring, &val, sizeof(val)));
    val++;
    TEST_ASSERT_EQUAL(ESP_OK, fram_ring_append(&ring, &val, sizeof(val)));

    uint32_t last_slot = (ring.head_slot + ring.capacity - 1) % ring.capacity;
    uint32_t commit_offset = s_parts[0].offset + last_slot * ring.entry_size + sizeof(fram_ring_header_t) + ring.max_payload;
    uint8_t *raw = fram_hal_mock_get_buffer(&s_hal);
    raw[commit_offset] = 0x00;

    fram_ring_t recovered;
    TEST_ASSERT_EQUAL(ESP_OK, fram_ring_init(&recovered, &cfg));
    TEST_ASSERT_EQUAL_UINT32(2, fram_ring_count(&recovered));

    size_t len = 0;
    TEST_ASSERT_EQUAL(ESP_OK, fram_ring_peek_newest_len(&recovered, &len));
    TEST_ASSERT_EQUAL_UINT32(sizeof(uint32_t), len);
}

TEST_CASE("fram_vslot_recovery_commit_missing", "[fram]") {
    fram_vslot_t vs;
    fram_vslot_config_t cfg = {
        .pm = &s_pm,
        .partition_name = "vslot",
        .max_payload = 16,
        .slot_count = 2,
        .magic = 0x56534C54,
    };
    TEST_ASSERT_EQUAL(ESP_OK, fram_vslot_init(&vs, &cfg));

    uint32_t v1 = 0x11111111;
    uint32_t v2 = 0x22222222;
    TEST_ASSERT_EQUAL(ESP_OK, fram_vslot_save(&vs, &v1, sizeof(v1)));
    TEST_ASSERT_EQUAL(ESP_OK, fram_vslot_save(&vs, &v2, sizeof(v2)));

    uint32_t corrupt_slot = 1;
    uint32_t commit_offset = s_parts[1].offset + corrupt_slot * vs.slot_size + sizeof(fram_vslot_header_t) + vs.max_payload;
    uint8_t *raw = fram_hal_mock_get_buffer(&s_hal);
    raw[commit_offset] = 0x00;

    fram_vslot_t recovered;
    TEST_ASSERT_EQUAL(ESP_OK, fram_vslot_init(&recovered, &cfg));

    uint32_t out = 0;
    size_t len = sizeof(out);
    TEST_ASSERT_EQUAL(ESP_OK, fram_vslot_load(&recovered, &out, &len));
    TEST_ASSERT_EQUAL_UINT32(v1, out);

    size_t peek_len = 0;
    TEST_ASSERT_EQUAL(ESP_OK, fram_vslot_peek_len(&recovered, &peek_len));
    TEST_ASSERT_EQUAL_UINT32(sizeof(uint32_t), peek_len);
}

typedef struct {
    uint32_t magic;
    uint32_t seq;
    uint16_t key_len;
    uint16_t value_len;
    uint8_t flags;
    uint8_t reserved[3];
    uint32_t crc32;
} __attribute__((packed)) fram_kvs_header_test_t;

TEST_CASE("fram_kvs_crc_stop_and_len", "[fram]") {
    fram_kvs_t kvs;
    fram_kvs_config_t cfg = {
        .pm = &s_pm,
        .partition_name = "kvs",
        .magic = 0x4B56534D,
    };
    TEST_ASSERT_EQUAL(ESP_OK, fram_kvs_init(&kvs, &cfg));

    TEST_ASSERT_EQUAL(ESP_OK, fram_kvs_set(&kvs, "a", "one", 3));
    uint32_t offset_b = kvs.write_offset;
    TEST_ASSERT_EQUAL(ESP_OK, fram_kvs_set(&kvs, "b", "two", 3));

    uint8_t *raw = fram_hal_mock_get_buffer(&s_hal);
    uint32_t crc = 0;
    size_t crc_offset = s_parts[2].offset + offset_b + offsetof(fram_kvs_header_test_t, crc32);
    memcpy(&crc, raw + crc_offset, sizeof(crc));
    crc ^= 0xFFFFFFFF;
    memcpy(raw + crc_offset, &crc, sizeof(crc));

    char buf[8] = {0};
    size_t len = sizeof(buf);
    TEST_ASSERT_EQUAL(ESP_OK, fram_kvs_get(&kvs, "a", buf, &len));
    TEST_ASSERT_EQUAL_UINT32(3, len);

    len = sizeof(buf);
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, fram_kvs_get(&kvs, "b", buf, &len));

    size_t val_len = 0;
    TEST_ASSERT_EQUAL(ESP_OK, fram_kvs_get_len(&kvs, "a", &val_len));
    TEST_ASSERT_EQUAL_UINT32(3, val_len);
}

#else

TEST_CASE("fram_tests_skipped", "[fram]") {
    TEST_IGNORE_MESSAGE("CONFIG_FRAM_HAL_MOCK_ENABLED is disabled");
}

#endif
