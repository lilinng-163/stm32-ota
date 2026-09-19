#pragma once

#include <stdint.h>

/*
 * 标准 CRC-32 (与 zlib.crc32 一致):
 * poly 0xEDB88320, 初值 0xFFFFFFFF, 结果异或 0xFFFFFFFF, 逐字节
 */

uint32_t crc32_calc(const void *data, uint32_t len);

uint32_t crc32_flash(uint32_t addr, uint32_t len);   /* 直接对 flash 地址计算 */
