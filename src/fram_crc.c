#include "fram_crc.h"
#include "esp_rom_crc.h"

uint32_t fram_crc32_le(uint32_t seed, const void *data, size_t len) {
    return esp_rom_crc32_le(seed, data, len);
}
