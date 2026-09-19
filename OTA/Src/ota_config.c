#include <stdint.h>
#include "stm32f4xx_hal.h"
#include "ota_layout.h"
#include "ota_config.h"

/*
 * config 采用"扇区内追加日志":
 *  - 每条 ota_app_config_t 占一个固定槽(36字节), 从槽0起顺序追加;
 *  - 每条记录先写 body、最后写 header(magic), header 命中才算有效;
 *  - 读 = 顺序扫描, 取最后一条有效记录;
 *  - 写 = 追加到下一条空白槽; 整扇区写满才擦一次.
 * 这样一次状态变更不再擦整个扇区, 擦除次数大幅下降.
 */
#define CONFIG_SECTOR_SIZE 0x10000U                    /* sector4 = 64K */
#define CONFIG_REC_SIZE    (sizeof(ota_app_config_t))  /* 一条记录大小 */
#ifndef CONFIG_REC_MAX
#define CONFIG_REC_MAX     (CONFIG_SECTOR_SIZE / CONFIG_REC_SIZE)
#endif

static ota_app_config_t *config_rec(uint32_t idx)
{
    return (ota_app_config_t *)(CONFIG_DATA_BASE + idx * CONFIG_REC_SIZE);
}

/* 整条记录是否还是擦除态(全0xFF), 即可以写 */
static int config_rec_blank(const ota_app_config_t *rec)
{
    const uint32_t *w = (const uint32_t *)rec;
    for(uint32_t i = 0; i < CONFIG_REC_SIZE / 4U; i++)
    {
        if(w[i] != 0xFFFFFFFFU)
        {
            return 0;
        }
    }
    return 1;
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

/* 把一条记录写进空白槽: 先body, 最后header */
static int config_rec_program(const ota_app_config_t *rec, const ota_app_config_t *cfg)
{
    const uint32_t *word = (const uint32_t *)cfg;
    uint32_t words = CONFIG_REC_SIZE / 4U;
    uint32_t addr = (uint32_t)rec;

    HAL_FLASH_Unlock();
    for(uint32_t i = 1; i < words; i++)
    {
        if(HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr + i * 4U, word[i]) != HAL_OK)
        {
            HAL_FLASH_Lock();
            return -1;
        }
    }
    if(HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr, word[0]) != HAL_OK)
    {
        HAL_FLASH_Lock();
        return -1;
    }
    HAL_FLASH_Lock();
    return 0;
}

/* 读: 顺序扫描, 返回最后一条有效记录(header命中)的地址; 若无则返回槽0 */
ota_app_config_t *ota_read_config(void)
{
    ota_app_config_t *last = config_rec(0);

    for(uint32_t i = 0; i < CONFIG_REC_MAX; i++)
    {
        ota_app_config_t *rec = config_rec(i);
        if(config_rec_blank(rec))
        {
            break;                          /* 到空白, 日志结束 */
        }
        if(rec->header == OTA_APP_CONFIG_T_HEADER)
        {
            last = rec;                     /* 有效记录, 取最后一条 */
        }
        /* 否则是半写坏的记录, 跳过 */
    }
    return last;
}

/* 写: 追加一条; 扇区满则保住最后有效记录, 擦除后重写它再写新记录 */
int ota_write_config(const ota_app_config_t *modify)
{
    ota_app_config_t *last = NULL;
    uint32_t write_idx = 0;
    int found_blank = 0;

    for(uint32_t i = 0; i < CONFIG_REC_MAX; i++)
    {
        ota_app_config_t *rec = config_rec(i);
        if(config_rec_blank(rec))
        {
            write_idx = i;
            found_blank = 1;
            break;
        }
        if(rec->header == OTA_APP_CONFIG_T_HEADER)
        {
            last = rec;
        }
    }

    if(found_blank)
    {
        return config_rec_program(config_rec(write_idx), modify);
    }

    /* 扇区满了: 先把最后一条有效记录保存到RAM */
    ota_app_config_t keep;
    int has_keep = (last != NULL);
    if(has_keep)
    {
        keep = *last;
    }
    if(ota_erase_config() != 0)
    {
        return -1;
    }
    /* 擦完把有效记录挪到槽0, 新记录写槽1 */
    uint32_t idx = 0;
    if(has_keep)
    {
        if(config_rec_program(config_rec(0), &keep) != 0)
        {
            return -1;
        }
        idx = 1;
    }
    return config_rec_program(config_rec(idx), modify);
}
