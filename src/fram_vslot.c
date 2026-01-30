#include "fram/fram_vslot.h"

#include "esp_check.h"
#include "esp_timer.h"
#include "fram_crc.h"
#include "sdkconfig.h"
#include <stddef.h>
#include <string.h>

#define TAG "fram_vslot"

#define FRAM_VSLOT_COMMIT 0xA5

static uint32_t fram_vslot_slot_offset(const fram_vslot_t *vs, uint32_t slot) {
    return slot * vs->slot_size;
}

static esp_err_t fram_vslot_read_commit(const fram_vslot_t *vs, uint32_t slot, uint8_t *commit) {
    uint32_t offset = fram_vslot_slot_offset(vs, slot) + sizeof(fram_vslot_header_t) + vs->max_payload;
    return fram_pm_read(vs->pm, vs->part, offset, commit, sizeof(*commit));
}

static esp_err_t fram_vslot_write_commit(const fram_vslot_t *vs, uint32_t slot, uint8_t commit) {
    uint32_t offset = fram_vslot_slot_offset(vs, slot) + sizeof(fram_vslot_header_t) + vs->max_payload;
    return fram_pm_write(vs->pm, vs->part, offset, &commit, sizeof(commit));
}

static esp_err_t fram_vslot_read_header(const fram_vslot_t *vs, uint32_t slot, fram_vslot_header_t *hdr) {
    uint8_t buf[sizeof(fram_vslot_header_t)];
    esp_err_t err = fram_pm_read(vs->pm, vs->part, fram_vslot_slot_offset(vs, slot), buf, sizeof(buf));
    if (err != ESP_OK) {
        return err;
    }
    memcpy(hdr, buf, sizeof(*hdr));
    return ESP_OK;
}

static esp_err_t fram_vslot_validate_slot(const fram_vslot_t *vs, uint32_t slot, fram_vslot_header_t *hdr_out) {
    uint8_t commit = 0;
    esp_err_t err = fram_vslot_read_commit(vs, slot, &commit);
    if (err != ESP_OK || commit != FRAM_VSLOT_COMMIT) {
        return ESP_ERR_NOT_FOUND;
    }

    fram_vslot_header_t hdr;
    err = fram_vslot_read_header(vs, slot, &hdr);
    if (err != ESP_OK) {
        return err;
    }
    if (hdr.magic != vs->magic) {
        return ESP_ERR_NOT_FOUND;
    }
    if (hdr.len > vs->max_payload) {
        return ESP_ERR_INVALID_SIZE;
    }

    uint8_t payload_buf[CONFIG_FRAM_VSLOT_MAX_PAYLOAD];
    if (hdr.len > 0) {
        err = fram_pm_read(vs->pm, vs->part,
                           fram_vslot_slot_offset(vs, slot) + sizeof(fram_vslot_header_t),
                           payload_buf, hdr.len);
        if (err != ESP_OK) {
            return err;
        }
    }

    uint32_t crc = fram_crc32_le(0, &hdr, offsetof(fram_vslot_header_t, crc32));
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

static esp_err_t fram_vslot_lock(fram_vslot_t *vs) {
    if (vs == NULL || vs->mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(vs->mutex, pdMS_TO_TICKS(CONFIG_FRAM_DEFAULT_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

static void fram_vslot_unlock(fram_vslot_t *vs) {
    if (vs && vs->mutex) {
        xSemaphoreGive(vs->mutex);
    }
}

esp_err_t fram_vslot_init(fram_vslot_t *vs, const fram_vslot_config_t *cfg) {
    if (vs == NULL || cfg == NULL || cfg->pm == NULL || cfg->partition_name == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (cfg->slot_count < 2 || cfg->slot_count > 3) {
        return ESP_ERR_INVALID_ARG;
    }
    if (cfg->max_payload == 0 || cfg->max_payload > CONFIG_FRAM_VSLOT_MAX_PAYLOAD) {
        return ESP_ERR_INVALID_SIZE;
    }

    memset(vs, 0, sizeof(*vs));
    vs->pm = cfg->pm;
    vs->part = fram_pm_find(cfg->pm, cfg->partition_name);
    if (vs->part == NULL) {
        return ESP_ERR_NOT_FOUND;
    }

    vs->slot_count = cfg->slot_count;
    vs->max_payload = cfg->max_payload;
    vs->slot_size = sizeof(fram_vslot_header_t) + vs->max_payload + 1;
    vs->magic = cfg->magic;

    if (vs->part->size < vs->slot_size * vs->slot_count) {
        return ESP_ERR_INVALID_SIZE;
    }

    vs->mutex = xSemaphoreCreateMutexStatic(&vs->mutex_buf);
    if (vs->mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    uint32_t best_version = 0;
    uint32_t best_slot = 0;
    bool found = false;

    for (uint32_t slot = 0; slot < vs->slot_count; slot++) {
        fram_vslot_header_t hdr;
        esp_err_t err = fram_vslot_validate_slot(vs, slot, &hdr);
        if (err == ESP_OK) {
            if (!found || hdr.version > best_version) {
                best_version = hdr.version;
                best_slot = slot;
                found = true;
            }
        }
    }

    vs->has_data = found;
    if (found) {
        vs->active_slot = best_slot;
        vs->active_version = best_version;
    } else {
        vs->active_slot = 0;
        vs->active_version = 0;
    }

    return ESP_OK;
}

esp_err_t fram_vslot_deinit(fram_vslot_t *vs) {
    if (vs == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    vs->has_data = false;
    return ESP_OK;
}

esp_err_t fram_vslot_load(fram_vslot_t *vs, void *payload, size_t *len) {
    if (vs == NULL || len == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!vs->has_data) {
        return ESP_ERR_NOT_FOUND;
    }

    esp_err_t err = fram_vslot_lock(vs);
    if (err != ESP_OK) {
        return err;
    }

    fram_vslot_header_t hdr;
    err = fram_vslot_validate_slot(vs, vs->active_slot, &hdr);
    if (err == ESP_OK) {
        if (*len < hdr.len) {
            *len = hdr.len;
            err = ESP_ERR_INVALID_SIZE;
        } else if (hdr.len > 0) {
            if (payload == NULL) {
                *len = hdr.len;
                err = ESP_ERR_INVALID_SIZE;
            } else {
                err = fram_pm_read(vs->pm, vs->part,
                                   fram_vslot_slot_offset(vs, vs->active_slot) + sizeof(fram_vslot_header_t),
                                   payload, hdr.len);
                if (err == ESP_OK) {
                    *len = hdr.len;
                }
            }
        } else {
            *len = 0;
        }
    }

    fram_vslot_unlock(vs);
    return err;
}

esp_err_t fram_vslot_save(fram_vslot_t *vs, const void *payload, size_t len) {
    if (vs == NULL || (payload == NULL && len > 0)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (len > vs->max_payload) {
        return ESP_ERR_INVALID_SIZE;
    }

    esp_err_t err = fram_vslot_lock(vs);
    if (err != ESP_OK) {
        return err;
    }

    uint32_t next_version = vs->has_data ? (vs->active_version + 1) : 1;
    uint32_t slot = vs->has_data ? ((vs->active_slot + 1) % vs->slot_count) : 0;

    err = fram_vslot_write_commit(vs, slot, 0x00);
    if (err != ESP_OK) {
        fram_vslot_unlock(vs);
        return err;
    }

    fram_vslot_header_t hdr = {
        .magic = vs->magic,
        .version = next_version,
        .ts_us = (uint64_t)esp_timer_get_time(),
        .len = (uint32_t)len,
        .crc32 = 0,
    };

    uint32_t crc = fram_crc32_le(0, &hdr, offsetof(fram_vslot_header_t, crc32));
    if (len > 0) {
        crc = fram_crc32_le(crc, payload, len);
    }
    hdr.crc32 = crc;

    err = fram_pm_write(vs->pm, vs->part, fram_vslot_slot_offset(vs, slot), &hdr, sizeof(hdr));
    if (err == ESP_OK && len > 0) {
        err = fram_pm_write(vs->pm, vs->part,
                            fram_vslot_slot_offset(vs, slot) + sizeof(hdr), payload, len);
    }
    if (err == ESP_OK) {
        err = fram_vslot_write_commit(vs, slot, FRAM_VSLOT_COMMIT);
    }

    if (err == ESP_OK) {
        vs->active_slot = slot;
        vs->active_version = next_version;
        vs->has_data = true;
    }

    fram_vslot_unlock(vs);
    return err;
}

esp_err_t fram_vslot_peek_len(fram_vslot_t *vs, size_t *len) {
    if (vs == NULL || len == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!vs->has_data) {
        return ESP_ERR_NOT_FOUND;
    }

    esp_err_t err = fram_vslot_lock(vs);
    if (err != ESP_OK) {
        return err;
    }

    fram_vslot_header_t hdr;
    err = fram_vslot_validate_slot(vs, vs->active_slot, &hdr);
    if (err == ESP_OK) {
        *len = hdr.len;
    }

    fram_vslot_unlock(vs);
    return err;
}

bool fram_vslot_has_data(const fram_vslot_t *vs) {
    return vs ? vs->has_data : false;
}

uint32_t fram_vslot_get_version(const fram_vslot_t *vs) {
    return vs ? vs->active_version : 0;
}

esp_err_t fram_vslot_clear(fram_vslot_t *vs) {
    if (vs == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = fram_vslot_lock(vs);
    if (err != ESP_OK) {
        return err;
    }

    err = fram_pm_erase(vs->pm, vs->part);
    if (err == ESP_OK) {
        vs->has_data = false;
        vs->active_version = 0;
        vs->active_slot = 0;
    }

    fram_vslot_unlock(vs);
    return err;
}
