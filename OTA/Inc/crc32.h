#pragma once

#include <stdint.h>

uint32_t crc32_calc(const void *data, uint32_t len);

uint32_t crc32_flash(uint32_t addr, uint32_t len);