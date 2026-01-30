#include "fram/fram_superblock.h"

#include "esp_check.h"
#include "fram_crc.h"
#include <stddef.h>
#include <string.h>

#define TAG "fram_superblock"

static uint32_t fram_superblock_offset(uint32_t base, uint32_t index) {
    return base + (uint32_t)(index * sizeof(fram_superblock_t));
}

uint32_t fram_superblock_crc(const fram_superblock_t *sb) {
    if (sb == NULL) {
        return 0;
    }
    return fram_crc32_le(0, sb, offsetof(fram_superblock_t, crc32));
}

static bool fram_superblock_valid(const fram_superblock_t *sb, uint32_t dev_size) {
    if (sb->magic != FRAM_SUPERBLOCK_MAGIC || sb->version != FRAM_SUPERBLOCK_VERSION) {
        return false;
    }
    if (sb->commit != FRAM_SUPERBLOCK_COMMIT) {
        return false;
    }
    if (sb->count > FRAM_PART_MAX) {
        return false;
    }
    if (sb->size_bytes != dev_size) {
        return false;
    }
    uint32_t crc = fram_superblock_crc(sb);
    return (crc == sb->crc32);
}

esp_err_t fram_superblock_read(fram_dev_t *dev, uint32_t base_offset, fram_superblock_t *out) {
    if (dev == NULL || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    fram_superblock_t a;
    fram_superblock_t b;

    esp_err_t err_a = fram_dev_read(dev, base_offset, &a, sizeof(a));
    esp_err_t err_b = fram_dev_read(dev, base_offset + sizeof(a), &b, sizeof(b));
    bool a_read_ok = (err_a == ESP_OK);
    bool b_read_ok = (err_b == ESP_OK);

    if (!a_read_ok && !b_read_ok) {
        return err_a;
    }

    uint32_t dev_size = fram_dev_get_size(dev);
    bool a_valid = a_read_ok && fram_superblock_valid(&a, dev_size);
    bool b_valid = b_read_ok && fram_superblock_valid(&b, dev_size);

    if (!a_valid && !b_valid) {
        return ESP_ERR_NOT_FOUND;
    }
    if (a_valid && (!b_valid || a.seq >= b.seq)) {
        *out = a;
    } else {
        *out = b;
    }

    return ESP_OK;
}

esp_err_t fram_superblock_write(fram_dev_t *dev, uint32_t base_offset, const fram_superblock_t *sb) {
    if (dev == NULL || sb == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t dev_size = fram_dev_get_size(dev);
    if (sb->magic != FRAM_SUPERBLOCK_MAGIC || sb->version != FRAM_SUPERBLOCK_VERSION) {
        return ESP_ERR_INVALID_ARG;
    }
    if (sb->size_bytes != dev_size || sb->count > FRAM_PART_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    fram_superblock_t a;
    fram_superblock_t b;
    bool a_valid = false;
    bool b_valid = false;

    if (fram_dev_read(dev, base_offset, &a, sizeof(a)) == ESP_OK) {
        a_valid = fram_superblock_valid(&a, dev_size);
    }
    if (fram_dev_read(dev, base_offset + sizeof(a), &b, sizeof(b)) == ESP_OK) {
        b_valid = fram_superblock_valid(&b, dev_size);
    }

    uint32_t target_index = 0;
    uint32_t next_seq = sb->seq;
    if (a_valid && b_valid) {
        if (a.seq <= b.seq) {
            target_index = 0;
            next_seq = b.seq + 1;
        } else {
            target_index = 1;
            next_seq = a.seq + 1;
        }
    } else if (a_valid) {
        target_index = 1;
        next_seq = a.seq + 1;
    } else if (b_valid) {
        target_index = 0;
        next_seq = b.seq + 1;
    } else {
        target_index = 0;
        next_seq = 1;
    }

    fram_superblock_t temp = *sb;
    temp.seq = next_seq;
    temp.commit = 0;
    temp.reserved[0] = 0;
    temp.reserved[1] = 0;
    temp.reserved[2] = 0;
    temp.crc32 = fram_superblock_crc(&temp);

    uint32_t offset = fram_superblock_offset(base_offset, target_index);
    esp_err_t err = fram_dev_write(dev, offset, &temp, sizeof(temp));
    if (err != ESP_OK) {
        return err;
    }
    uint8_t commit = FRAM_SUPERBLOCK_COMMIT;
    return fram_dev_write(dev, offset + offsetof(fram_superblock_t, commit), &commit, sizeof(commit));
}
