#include <stdint.h>
#include "stm32f4xx_hal.h"
#include "ota_layout.h"
#include "ota_config.h"

ota_app_config_t *ota_read_config(void)
{
    return (ota_app_config_t *)CONFIG_DATA_BASE;
}

/* 擦除 config 所在扇区(sector4) */
int ota_erase_config(void)
{
    uint32_t sec_err = 0;
    FLASH_EraseInitTypeDef erase =
    {
        .TypeErase    = FLASH_TYPEERASE_SECTORS,
        .VoltageRange = FLASH_VOLTAGE_RANGE_3,
        .Sector       = FLASH_SECTOR_4,
        .NbSectors    = 1,
    };

    HAL_FLASH_Unlock();
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                           FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);
    if(HAL_FLASHEx_Erase(&erase, &sec_err) != HAL_OK)
    {
        HAL_FLASH_Lock();
        return -1;
    }
    HAL_FLASH_Lock();
    return 0;
}

/* 写 config: 先写 body, 最后写 header, 保证 header 存在 = config 完整 */
int ota_write_config(const ota_app_config_t *modify)
{
    ota_app_config_t tmp = *modify;
    const uint32_t *word = (const uint32_t *)&tmp;
    uint32_t words = sizeof(ota_app_config_t) / 4U;

    if(ota_erase_config() != 0)
    {
        return -1;
    }

    HAL_FLASH_Unlock();
    for(uint32_t i = 1; i < words; i++)
    {
        if(HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, CONFIG_DATA_BASE + i * 4U, word[i]) != HAL_OK)
        {
            HAL_FLASH_Lock();
            return -1;
        }
    }
    if(HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, CONFIG_DATA_BASE, word[0]) != HAL_OK)
    {
        HAL_FLASH_Lock();
        return -1;
    }
    HAL_FLASH_Lock();
    return 0;
}
