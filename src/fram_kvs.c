#include "fram/fram_kvs.h"

#if CONFIG_FRAM_KVS_ENABLED

#include "esp_check.h"
#include "fram_crc.h"
#include "sdkconfig.h"
#include <stddef.h>
#include <string.h>

#define TAG "fram_kvs"

#define FRAM_KVS_COMMIT 0xA5
#define FRAM_KVS_FLAG_DELETED (1U << 0)
#define FRAM_KVS_CRC_CHUNK 64

typedef struct {
    uint32_t magic;
    uint32_t seq;
    uint16_t key_len;
    uint16_t value_len;
    uint8_t flags;
    uint8_t reserved[3];
    uint32_t crc32;
} __attribute__((packed)) fram_kvs_header_t;

static esp_err_t fram_kvs_lock(fram_kvs_t *kvs) {
    if (kvs == NULL || kvs->mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(kvs->mutex, pdMS_TO_TICKS(CONFIG_FRAM_DEFAULT_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

static void fram_kvs_unlock(fram_kvs_t *kvs) {
    if (kvs && kvs->mutex) {
        xSemaphoreGive(kvs->mutex);
    }
}

static esp_err_t fram_kvs_read_header(fram_kvs_t *kvs, uint32_t offset, fram_kvs_header_t *hdr) {
    uint8_t buf[sizeof(fram_kvs_header_t)];
    esp_err_t err = fram_pm_read(kvs->pm, kvs->part, offset, buf, sizeof(buf));
    if (err != ESP_OK) {
        return err;
    }
    memcpy(hdr, buf, sizeof(*hdr));
    return ESP_OK;
}

static esp_err_t fram_kvs_read_commit(fram_kvs_t *kvs, uint32_t offset, uint16_t key_len, uint16_t value_len, uint8_t *commit) {
    uint32_t commit_offset = offset + sizeof(fram_kvs_header_t) + key_len + value_len;
    return fram_pm_read(kvs->pm, kvs->part, commit_offset, commit, sizeof(*commit));
}

static esp_err_t fram_kvs_write_commit(fram_kvs_t *kvs, uint32_t offset, uint16_t key_len, uint16_t value_len, uint8_t commit) {
    uint32_t commit_offset = offset + sizeof(fram_kvs_header_t) + key_len + value_len;
    return fram_pm_write(kvs->pm, kvs->part, commit_offset, &commit, sizeof(commit));
}

static bool fram_kvs_header_valid(const fram_kvs_t *kvs, const fram_kvs_header_t *hdr) {
    if (hdr->magic != kvs->magic) {
        return false;
    }
    if (hdr->key_len == 0 || hdr->key_len > FRAM_KVS_KEY_MAX) {
        return false;
    }
    if (hdr->value_len > CONFIG_FRAM_KVS_MAX_VALUE) {
        return false;
    }
    return true;
}

static esp_err_t fram_kvs_compute_crc(fram_kvs_t *kvs, uint32_t offset,
                                     const fram_kvs_header_t *hdr,
                                     uint8_t *key_buf) {
    uint32_t crc = fram_crc32_le(0, hdr, offsetof(fram_kvs_header_t, crc32));

    if (hdr->key_len > 0) {
        esp_err_t err = fram_pm_read(kvs->pm, kvs->part,
                                     offset + sizeof(fram_kvs_header_t),
                                     key_buf, hdr->key_len);
        if (err != ESP_OK) {
            return err;
        }
        crc = fram_crc32_le(crc, key_buf, hdr->key_len);
    }

    uint32_t value_offset = offset + sizeof(fram_kvs_header_t) + hdr->key_len;
    uint32_t remaining = hdr->value_len;
    uint8_t buf[FRAM_KVS_CRC_CHUNK];
    while (remaining > 0) {
        uint32_t chunk = remaining > FRAM_KVS_CRC_CHUNK ? FRAM_KVS_CRC_CHUNK : remaining;
        esp_err_t err = fram_pm_read(kvs->pm, kvs->part, value_offset, buf, chunk);
        if (err != ESP_OK) {
            return err;
        }
        crc = fram_crc32_le(crc, buf, chunk);
        value_offset += chunk;
        remaining -= chunk;
    }

    if (crc != hdr->crc32) {
        return ESP_ERR_INVALID_CRC;
    }
    return ESP_OK;
}

static esp_err_t fram_kvs_scan(fram_kvs_t *kvs, const char *key,
                               fram_kvs_header_t *out_hdr, uint32_t *out_offset,
                               bool *out_deleted) {
    uint32_t offset = 0;
    bool found = false;
    bool deleted = false;
    fram_kvs_header_t last_hdr = {0};
    uint32_t last_offset = 0;

    size_t key_len_in = key ? strlen(key) : 0;
    uint8_t key_buf[FRAM_KVS_KEY_MAX];

    while (offset + sizeof(fram_kvs_header_t) + 1 <= kvs->part->size) {
        fram_kvs_header_t hdr;
        esp_err_t err = fram_kvs_read_header(kvs, offset, &hdr);
        if (err != ESP_OK) {
            return err;
        }
        if (!fram_kvs_header_valid(kvs, &hdr)) {
            break;
        }
        uint32_t record_size = sizeof(fram_kvs_header_t) + hdr.key_len + hdr.value_len + 1;
        if (offset > kvs->part->size || record_size > kvs->part->size || offset + record_size > kvs->part->size) {
            break;
        }

        uint8_t commit = 0;
        err = fram_kvs_read_commit(kvs, offset, hdr.key_len, hdr.value_len, &commit);
        if (err != ESP_OK) {
            return err;
        }
        if (commit != FRAM_KVS_COMMIT) {
            break;
        }

        err = fram_kvs_compute_crc(kvs, offset, &hdr, key_buf);
        if (err == ESP_ERR_INVALID_CRC) {
            break;
        }
        if (err != ESP_OK) {
            return err;
        }
        bool crc_ok = true;

        if (crc_ok && key && hdr.key_len == key_len_in &&
            memcmp(key_buf, key, key_len_in) == 0) {
            last_hdr = hdr;
            last_offset = offset;
            found = true;
            deleted = (hdr.flags & FRAM_KVS_FLAG_DELETED) != 0;
        }

        offset += record_size;
    }

    if (out_hdr && found) {
        *out_hdr = last_hdr;
    }
    if (out_offset && found) {
        *out_offset = last_offset;
    }
    if (out_deleted) {
        *out_deleted = deleted;
    }

    return found ? ESP_OK : ESP_ERR_NOT_FOUND;
}

static esp_err_t fram_kvs_find_end(fram_kvs_t *kvs, uint32_t *out_offset, uint32_t *out_next_seq) {
    uint32_t offset = 0;
    uint32_t next_seq = 0;
    uint8_t key_buf[FRAM_KVS_KEY_MAX];

    while (offset + sizeof(fram_kvs_header_t) + 1 <= kvs->part->size) {
        fram_kvs_header_t hdr;
        esp_err_t err = fram_kvs_read_header(kvs, offset, &hdr);
        if (err != ESP_OK) {
            return err;
        }
        if (!fram_kvs_header_valid(kvs, &hdr)) {
            break;
        }
        uint32_t record_size = sizeof(fram_kvs_header_t) + hdr.key_len + hdr.value_len + 1;
        if (offset > kvs->part->size || record_size > kvs->part->size || offset + record_size > kvs->part->size) {
            break;
        }

        uint8_t commit = 0;
        err = fram_kvs_read_commit(kvs, offset, hdr.key_len, hdr.value_len, &commit);
        if (err != ESP_OK) {
            return err;
        }
        if (commit != FRAM_KVS_COMMIT) {
            break;
        }

        err = fram_kvs_compute_crc(kvs, offset, &hdr, key_buf);
        if (err == ESP_ERR_INVALID_CRC) {
            break;
        }
        if (err != ESP_OK) {
            return err;
        }
        if (hdr.seq >= next_seq) {
            next_seq = hdr.seq + 1;
        }

        offset += record_size;
    }

    if (out_offset) {
        *out_offset = offset;
    }
    if (out_next_seq) {
        *out_next_seq = next_seq;
    }
    return ESP_OK;
}

esp_err_t fram_kvs_init(fram_kvs_t *kvs, const fram_kvs_config_t *cfg) {
    if (kvs == NULL || cfg == NULL || cfg->pm == NULL || cfg->partition_name == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(kvs, 0, sizeof(*kvs));
    kvs->pm = cfg->pm;
    kvs->part = fram_pm_find(cfg->pm, cfg->partition_name);
    if (kvs->part == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    kvs->magic = cfg->magic;

    kvs->mutex = xSemaphoreCreateMutexStatic(&kvs->mutex_buf);
    if (kvs->mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = fram_kvs_find_end(kvs, &kvs->write_offset, &kvs->next_seq);
    if (err != ESP_OK) {
        return err;
    }

    kvs->ready = true;
    return ESP_OK;
}

esp_err_t fram_kvs_deinit(fram_kvs_t *kvs) {
    if (kvs == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    kvs->ready = false;
    return ESP_OK;
}

esp_err_t fram_kvs_get(fram_kvs_t *kvs, const char *key, void *buf, size_t *len) {
    if (kvs == NULL || key == NULL || buf == NULL || len == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t key_len = strlen(key);
    if (key_len == 0 || key_len > FRAM_KVS_KEY_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = fram_kvs_lock(kvs);
    if (err != ESP_OK) {
        return err;
    }

    fram_kvs_header_t hdr;
    uint32_t offset = 0;
    bool deleted = false;
    err = fram_kvs_scan(kvs, key, &hdr, &offset, &deleted);
    if (err != ESP_OK || deleted) {
        fram_kvs_unlock(kvs);
        return ESP_ERR_NOT_FOUND;
    }

    if (*len < hdr.value_len) {
        *len = hdr.value_len;
        fram_kvs_unlock(kvs);
        return ESP_ERR_INVALID_SIZE;
    }

    if (hdr.value_len > 0) {
        err = fram_pm_read(kvs->pm, kvs->part,
                           offset + sizeof(fram_kvs_header_t) + hdr.key_len,
                           buf, hdr.value_len);
    }
    if (err == ESP_OK) {
        *len = hdr.value_len;
    }

    fram_kvs_unlock(kvs);
    return err;
}

esp_err_t fram_kvs_set(fram_kvs_t *kvs, const char *key, const void *buf, size_t len) {
    if (kvs == NULL || key == NULL || buf == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t key_len = strlen(key);
    if (key_len == 0 || key_len > FRAM_KVS_KEY_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    if (len > UINT16_MAX) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (len > CONFIG_FRAM_KVS_MAX_VALUE) {
        return ESP_ERR_INVALID_SIZE;
    }

    esp_err_t err = fram_kvs_lock(kvs);
    if (err != ESP_OK) {
        return err;
    }

    uint32_t record_size = sizeof(fram_kvs_header_t) + key_len + len + 1;
    if (kvs->write_offset + record_size > kvs->part->size) {
        fram_kvs_unlock(kvs);
        return ESP_ERR_NO_MEM;
    }

    fram_kvs_header_t hdr = {
        .magic = kvs->magic,
        .seq = kvs->next_seq,
        .key_len = (uint16_t)key_len,
        .value_len = (uint16_t)len,
        .flags = 0,
        .reserved = {0},
        .crc32 = 0,
    };

    uint32_t crc = fram_crc32_le(0, &hdr, offsetof(fram_kvs_header_t, crc32));
    crc = fram_crc32_le(crc, key, key_len);
    if (len > 0) {
        crc = fram_crc32_le(crc, buf, len);
    }
    hdr.crc32 = crc;

    err = fram_kvs_write_commit(kvs, kvs->write_offset, hdr.key_len, hdr.value_len, 0x00);
    if (err != ESP_OK) {
        fram_kvs_unlock(kvs);
        return err;
    }

    err = fram_pm_write(kvs->pm, kvs->part, kvs->write_offset, &hdr, sizeof(hdr));
    if (err == ESP_OK) {
        err = fram_pm_write(kvs->pm, kvs->part, kvs->write_offset + sizeof(hdr), key, key_len);
    }
    if (err == ESP_OK && len > 0) {
        err = fram_pm_write(kvs->pm, kvs->part,
                            kvs->write_offset + sizeof(hdr) + key_len, buf, len);
    }
    if (err == ESP_OK) {
        err = fram_kvs_write_commit(kvs, kvs->write_offset, hdr.key_len, hdr.value_len, FRAM_KVS_COMMIT);
    }

    if (err == ESP_OK) {
        kvs->write_offset += record_size;
        kvs->next_seq++;
    }

    fram_kvs_unlock(kvs);
    return err;
}

esp_err_t fram_kvs_delete(fram_kvs_t *kvs, const char *key) {
    if (kvs == NULL || key == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t key_len = strlen(key);
    if (key_len == 0 || key_len > FRAM_KVS_KEY_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = fram_kvs_lock(kvs);
    if (err != ESP_OK) {
        return err;
    }

    uint32_t record_size = sizeof(fram_kvs_header_t) + key_len + 0 + 1;
    if (kvs->write_offset + record_size > kvs->part->size) {
        fram_kvs_unlock(kvs);
        return ESP_ERR_NO_MEM;
    }

    fram_kvs_header_t hdr = {
        .magic = kvs->magic,
        .seq = kvs->next_seq,
        .key_len = (uint16_t)key_len,
        .value_len = 0,
        .flags = FRAM_KVS_FLAG_DELETED,
        .reserved = {0},
        .crc32 = 0,
    };

    uint32_t crc = fram_crc32_le(0, &hdr, offsetof(fram_kvs_header_t, crc32));
    crc = fram_crc32_le(crc, key, key_len);
    hdr.crc32 = crc;

    err = fram_kvs_write_commit(kvs, kvs->write_offset, hdr.key_len, hdr.value_len, 0x00);
    if (err != ESP_OK) {
        fram_kvs_unlock(kvs);
        return err;
    }

    err = fram_pm_write(kvs->pm, kvs->part, kvs->write_offset, &hdr, sizeof(hdr));
    if (err == ESP_OK) {
        err = fram_pm_write(kvs->pm, kvs->part, kvs->write_offset + sizeof(hdr), key, key_len);
    }
    if (err == ESP_OK) {
        err = fram_kvs_write_commit(kvs, kvs->write_offset, hdr.key_len, hdr.value_len, FRAM_KVS_COMMIT);
    }

    if (err == ESP_OK) {
        kvs->write_offset += record_size;
        kvs->next_seq++;
    }

    fram_kvs_unlock(kvs);
    return err;
}

bool fram_kvs_exists(fram_kvs_t *kvs, const char *key) {
    if (kvs == NULL || key == NULL) {
        return false;
    }
    size_t key_len = strlen(key);
    if (key_len == 0 || key_len > FRAM_KVS_KEY_MAX) {
        return false;
    }

    esp_err_t err = fram_kvs_lock(kvs);
    if (err != ESP_OK) {
        return false;
    }

    bool deleted = false;
    err = fram_kvs_scan(kvs, key, NULL, NULL, &deleted);
    fram_kvs_unlock(kvs);
    return (err == ESP_OK && !deleted);
}

esp_err_t fram_kvs_get_len(fram_kvs_t *kvs, const char *key, size_t *len) {
    if (kvs == NULL || key == NULL || len == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t key_len = strlen(key);
    if (key_len == 0 || key_len > FRAM_KVS_KEY_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = fram_kvs_lock(kvs);
    if (err != ESP_OK) {
        return err;
    }

    fram_kvs_header_t hdr;
    uint32_t offset = 0;
    bool deleted = false;
    err = fram_kvs_scan(kvs, key, &hdr, &offset, &deleted);
    if (err != ESP_OK || deleted) {
        fram_kvs_unlock(kvs);
        return ESP_ERR_NOT_FOUND;
    }

    *len = hdr.value_len;
    fram_kvs_unlock(kvs);
    return ESP_OK;
}

esp_err_t fram_kvs_get_u32(fram_kvs_t *kvs, const char *key, uint32_t *val) {
    if (val == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t len = sizeof(*val);
    return fram_kvs_get(kvs, key, val, &len);
}

esp_err_t fram_kvs_set_u32(fram_kvs_t *kvs, const char *key, uint32_t val) {
    return fram_kvs_set(kvs, key, &val, sizeof(val));
}

esp_err_t fram_kvs_get_str(fram_kvs_t *kvs, const char *key, char *buf, size_t *len) {
    if (buf == NULL || len == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t capacity = *len;
    esp_err_t err = fram_kvs_get(kvs, key, buf, len);
    if (err != ESP_OK) {
        return err;
    }
    if (*len < capacity) {
        buf[*len] = '\0';
    }
    return ESP_OK;
}

esp_err_t fram_kvs_set_str(fram_kvs_t *kvs, const char *key, const char *val) {
    if (val == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return fram_kvs_set(kvs, key, val, strlen(val));
}

#endif // CONFIG_FRAM_KVS_ENABLED
