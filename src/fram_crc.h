#pragma once

#include <stddef.h>
#include <stdint.h>

uint32_t fram_crc32_le(uint32_t seed, const void *data, size_t len);
