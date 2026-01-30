#include "fram/fram_ring.h"

#include "esp_check.h"
#include "esp_timer.h"
#include "fram_crc.h"
#include "sdkconfig.h"
#include <stddef.h>
#include <string.h>

#define TAG "fram_ring"

#define FRAM_RING_COMMIT 0xA5

static uint32_t fram_ring_slot_offset(const fram_ring_t *ring, uint32_t slot) {
    return slot * ring->entry_size;
}

static esp_err_t fram_ring_read_commit(const fram_ring_t *ring, uint32_t slot, uint8_t *commit) {
    uint32_t offset = fram_ring_slot_offset(ring, slot) + sizeof(fram_ring_header_t) + ring->max_payload;
    return fram_pm_read(ring->pm, ring->part, offset, commit, sizeof(*commit));
}

static esp_err_t fram_ring_write_commit(const fram_ring_t *ring, uint32_t slot, uint8_t commit) {
    uint32_t offset = fram_ring_slot_offset(ring, slot) + sizeof(fram_ring_header_t) + ring->max_payload;
    return fram_pm_write(ring->pm, ring->part, offset, &commit, sizeof(commit));
}

static esp_err_t fram_ring_read_header(const fram_ring_t *ring, uint32_t slot, fram_ring_header_t *hdr) {
    uint8_t buf[sizeof(fram_ring_header_t)];
    esp_err_t err = fram_pm_read(ring->pm, ring->part,
                                 fram_ring_slot_offset(ring, slot), buf, sizeof(buf));
    if (err != ESP_OK) {
        return err;
    }
    memcpy(hdr, buf, sizeof(*hdr));
    return ESP_OK;
}

static esp_err_t fram_ring_validate_slot(const fram_ring_t *ring, uint32_t slot, fram_ring_header_t *hdr_out) {
    uint8_t commit = 0;
    esp_err_t err = fram_ring_read_commit(ring, slot, &commit);
    if (err != ESP_OK || commit != FRAM_RING_COMMIT) {
        return ESP_ERR_NOT_FOUND;
    }

    fram_ring_header_t hdr;
    err = fram_ring_read_header(ring, slot, &hdr);
    if (err != ESP_OK) {
        return err;
    }
    if (hdr.magic != ring->magic) {
        return ESP_ERR_NOT_FOUND;
    }
    if (hdr.len > ring->max_payload) {
        return ESP_ERR_INVALID_SIZE;
    }

    uint8_t payload_buf[CONFIG_FRAM_RING_MAX_PAYLOAD];
    if (hdr.len > 0) {
        err = fram_pm_read(ring->pm, ring->part,
                           fram_ring_slot_offset(ring, slot) + sizeof(fram_ring_header_t),
                           payload_buf, hdr.len);
        if (err != ESP_OK) {
            return err;
        }
    }

    uint32_t crc = fram_crc32_le(0, &hdr, offsetof(fram_ring_header_t, crc32));
    if (hdr.len > 0) {
        crc = fram_crc32_le(crc, payload_buf, hdr.len);
    }
    if (crc != hdr.crc32) {
        return ESP_ERR_INVALID_CRC;
    }

    if (hdr_out) {
        *hdr_out = hdr;
    }
    return ESP_OK;
}

static esp_err_t fram_ring_lock(fram_ring_t *ring) {
    if (ring == NULL || ring->mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(ring->mutex, pdMS_TO_TICKS(CONFIG_FRAM_DEFAULT_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

static void fram_ring_unlock(fram_ring_t *ring) {
    if (ring && ring->mutex) {
        xSemaphoreGive(ring->mutex);
    }
}

esp_err_t fram_ring_init(fram_ring_t *ring, const fram_ring_config_t *cfg) {
    if (ring == NULL || cfg == NULL || cfg->pm == NULL || cfg->partition_name == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (cfg->max_payload == 0 || cfg->max_payload > CONFIG_FRAM_RING_MAX_PAYLOAD) {
        return ESP_ERR_INVALID_SIZE;
    }

    memset(ring, 0, sizeof(*ring));
    ring->pm = cfg->pm;
    ring->part = fram_pm_find(cfg->pm, cfg->partition_name);
    if (ring->part == NULL) {
        return ESP_ERR_NOT_FOUND;
    }

    ring->max_payload = cfg->max_payload;
    ring->entry_size = sizeof(fram_ring_header_t) + ring->max_payload + 1;
    ring->capacity = ring->part->size / ring->entry_size;
    ring->magic = cfg->magic;

    if (ring->capacity == 0) {
        return ESP_ERR_INVALID_SIZE;
    }

    ring->mutex = xSemaphoreCreateMutexStatic(&ring->mutex_buf);
    if (ring->mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    // Recovery
    uint32_t highest_seq = 0;
    uint32_t highest_slot = 0;
    bool found = false;

    for (uint32_t slot = 0; slot < ring->capacity; slot++) {
        fram_ring_header_t hdr;
        esp_err_t err = fram_ring_validate_slot(ring, slot, &hdr);
        if (err == ESP_OK) {
            if (!found || hdr.seq > highest_seq) {
                highest_seq = hdr.seq;
                highest_slot = slot;
                found = true;
            }
        }
    }

    if (!found) {
        ring->head_slot = 0;
        ring->tail_slot = 0;
        ring->head_seq = 0;
        ring->count = 0;
        ring->ready = true;
        return ESP_OK;
    }

    uint32_t run_len = 0;
    uint32_t expected_seq = highest_seq;
    uint32_t slot = highest_slot;

    while (run_len < ring->capacity) {
        fram_ring_header_t hdr;
        esp_err_t err = fram_ring_validate_slot(ring, slot, &hdr);
        if (err != ESP_OK || hdr.seq != expected_seq) {
            break;
        }
        run_len++;
        if (run_len >= ring->capacity) {
            break;
        }
        expected_seq--;
        slot = (slot + ring->capacity - 1) % ring->capacity;
    }

    ring->count = run_len;
    ring->head_slot = (highest_slot + 1) % ring->capacity;
    ring->head_seq = highest_seq + 1;
    ring->tail_slot = (ring->head_slot + ring->capacity - ring->count) % ring->capacity;
    ring->ready = true;

    return ESP_OK;
}

esp_err_t fram_ring_deinit(fram_ring_t *ring) {
    if (ring == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    ring->ready = false;
    return ESP_OK;
}

esp_err_t fram_ring_append(fram_ring_t *ring, const void *payload, size_t len) {
    if (ring == NULL || (payload == NULL && len > 0)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!ring->ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (len > ring->max_payload || len > UINT16_MAX) {
        return ESP_ERR_INVALID_SIZE;
    }

    esp_err_t err = fram_ring_lock(ring);
    if (err != ESP_OK) {
        return err;
    }

    uint32_t slot = ring->head_slot;

    // Clear commit first to avoid stale-valid entries
    err = fram_ring_write_commit(ring, slot, 0x00);
    if (err != ESP_OK) {
        fram_ring_unlock(ring);
        return err;
    }

    fram_ring_header_t hdr = {
        .magic = ring->magic,
        .seq = ring->head_seq,
        .ts_us = (uint64_t)esp_timer_get_time(),
        .len = (uint16_t)len,
        .reserved = 0,
        .crc32 = 0,
    };

    uint32_t crc = fram_crc32_le(0, &hdr, offsetof(fram_ring_header_t, crc32));
    if (len > 0) {
        crc = fram_crc32_le(crc, payload, len);
    }
    hdr.crc32 = crc;

    err = fram_pm_write(ring->pm, ring->part, fram_ring_slot_offset(ring, slot), &hdr, sizeof(hdr));
    if (err == ESP_OK && len > 0) {
        err = fram_pm_write(ring->pm, ring->part,
                            fram_ring_slot_offset(ring, slot) + sizeof(hdr), payload, len);
    }
    if (err == ESP_OK) {
        err = fram_ring_write_commit(ring, slot, FRAM_RING_COMMIT);
    }

    if (err != ESP_OK) {
        fram_ring_unlock(ring);
        return err;
    }

    ring->head_seq++;
    ring->head_slot = (ring->head_slot + 1) % ring->capacity;
    if (ring->count < ring->capacity) {
        ring->count++;
    } else {
        ring->tail_slot = (ring->tail_slot + 1) % ring->capacity;
    }

    fram_ring_unlock(ring);
    return ESP_OK;
}

static esp_err_t fram_ring_read_slot_payload(fram_ring_t *ring, uint32_t slot,
                                             void *payload, size_t *len,
                                             uint32_t *seq, uint64_t *ts_us) {
    fram_ring_header_t hdr;
    esp_err_t err = fram_ring_validate_slot(ring, slot, &hdr);
    if (err != ESP_OK) {
        return err;
    }

    if (len) {
        if (payload && *len < hdr.len) {
            *len = hdr.len;
            return ESP_ERR_INVALID_SIZE;
        }
        if (payload && hdr.len > 0) {
            err = fram_pm_read(ring->pm, ring->part,
                               fram_ring_slot_offset(ring, slot) + sizeof(fram_ring_header_t),
                               payload, hdr.len);
            if (err != ESP_OK) {
                return err;
            }
        }
        *len = hdr.len;
    }
    if (seq) {
        *seq = hdr.seq;
    }
    if (ts_us) {
        *ts_us = hdr.ts_us;
    }
    return ESP_OK;
}

esp_err_t fram_ring_peek_oldest(fram_ring_t *ring, void *payload, size_t *len,
                                uint32_t *seq, uint64_t *ts_us) {
    if (ring == NULL || len == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!ring->ready || ring->count == 0) {
        return ESP_ERR_NOT_FOUND;
    }

    esp_err_t err = fram_ring_lock(ring);
    if (err != ESP_OK) {
        return err;
    }
    err = fram_ring_read_slot_payload(ring, ring->tail_slot, payload, len, seq, ts_us);
    fram_ring_unlock(ring);
    return err;
}

esp_err_t fram_ring_peek_newest(fram_ring_t *ring, void *payload, size_t *len,
                                uint32_t *seq, uint64_t *ts_us) {
    if (ring == NULL || len == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!ring->ready || ring->count == 0) {
        return ESP_ERR_NOT_FOUND;
    }

    esp_err_t err = fram_ring_lock(ring);
    if (err != ESP_OK) {
        return err;
    }
    uint32_t newest_slot = (ring->head_slot + ring->capacity - 1) % ring->capacity;
    err = fram_ring_read_slot_payload(ring, newest_slot, payload, len, seq, ts_us);
    fram_ring_unlock(ring);
    return err;
}

esp_err_t fram_ring_peek_oldest_len(fram_ring_t *ring, size_t *len) {
    if (len == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return fram_ring_peek_oldest(ring, NULL, len, NULL, NULL);
}

esp_err_t fram_ring_peek_newest_len(fram_ring_t *ring, size_t *len) {
    if (len == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return fram_ring_peek_newest(ring, NULL, len, NULL, NULL);
}

esp_err_t fram_ring_iterate(fram_ring_t *ring, fram_ring_iter_fn cb, void *ctx) {
    if (ring == NULL || cb == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!ring->ready || ring->count == 0) {
        return ESP_OK;
    }

    esp_err_t err = fram_ring_lock(ring);
    if (err != ESP_OK) {
        return err;
    }

    uint32_t slot = ring->tail_slot;
    uint32_t remaining = ring->count;
    uint8_t payload_buf[CONFIG_FRAM_RING_MAX_PAYLOAD];

    while (remaining > 0) {
        size_t len = sizeof(payload_buf);
        uint32_t seq = 0;
        uint64_t ts_us = 0;
        err = fram_ring_read_slot_payload(ring, slot, payload_buf, &len, &seq, &ts_us);
        if (err == ESP_ERR_INVALID_SIZE) {
            // Skip if payload larger than buffer (should not happen due to max_payload check)
            break;
        }
        if (err != ESP_OK) {
            break;
        }
        err = cb(seq, ts_us, payload_buf, len, ctx);
        if (err != ESP_OK) {
            break;
        }
        slot = (slot + 1) % ring->capacity;
        remaining--;
    }

    fram_ring_unlock(ring);
    return err;
}

esp_err_t fram_ring_clear(fram_ring_t *ring) {
    if (ring == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!ring->ready) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = fram_ring_lock(ring);
    if (err != ESP_OK) {
        return err;
    }

    err = fram_pm_erase(ring->pm, ring->part);
    if (err == ESP_OK) {
        ring->head_slot = 0;
        ring->tail_slot = 0;
        ring->head_seq = 0;
        ring->count = 0;
    }

    fram_ring_unlock(ring);
    return err;
}

uint32_t fram_ring_count(const fram_ring_t *ring) {
    return ring ? ring->count : 0;
}

uint32_t fram_ring_capacity(const fram_ring_t *ring) {
    return ring ? ring->capacity : 0;
}

bool fram_ring_is_full(const fram_ring_t *ring) {
    return ring && ring->count == ring->capacity;
}

bool fram_ring_is_empty(const fram_ring_t *ring) {
    return ring == NULL || ring->count == 0;
}
