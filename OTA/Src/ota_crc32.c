#include <stdint.h>
#include "ota_crc32.h"

static uint32_t s_table[256];
static uint8_t  s_ready = 0;

static void build_table(void)
{
    for(uint32_t i = 0; i < 256U; i++)
    {
        uint32_t c = i;
        for(uint32_t k = 0; k < 8U; k++)
        {
            c = (c & 1U) ? (0xEDB88320UL ^ (c >> 1)) : (c >> 1);
        }
        s_table[i] = c;
    }
    s_ready = 1;
}

uint32_t crc32_calc(const void *data, uint32_t len)
{
    if(!s_ready)
    {
        build_table();
    }

    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFUL;
    for(uint32_t i = 0; i < len; i++)
    {
        crc = s_table[(crc ^ p[i]) & 0xFFUL] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFUL;
}

uint32_t crc32_flash(uint32_t addr, uint32_t len)
{
    return crc32_calc((const void *)addr, len);
}
