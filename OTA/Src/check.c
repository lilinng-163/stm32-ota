#include <stdint.h>
#include <stdio.h>
#include "stm32f4xx_hal.h"
#include "address.h"
#include "crc32.h"
#include "jump2app.h"
#include "check.h"

/* ready 状态下最多给新app几次启动机会, 超过就回滚 */
#define OTA_BOOT_MAX_TRY  3U

/* 三个槽在flash里对应的起始扇区, 每槽占2个扇区(256K) */
#define SLOT_R_SECTOR  FLASH_SECTOR_5
#define SLOT_N_SECTOR  FLASH_SECTOR_7
#define SLOT_K_SECTOR  FLASH_SECTOR_9
#define SLOT_SECTORS   2U

ota_app_config_t *ota_read_config(void)
{
    return (ota_app_config_t *)CONFIG_DATA_BASE;
}

/* 校验一个槽: 从 base 起 size 字节的 CRC 是否等于 crc
 * 返回 0 表示完好, -1 表示 size 非法或 CRC 不符 */
static int verify_slot(uint32_t base, uint32_t size, uint32_t crc)
{
    if(size == 0 || size > SLOT_SIZE)
    {
        return -1;
    }
    if(crc32_flash(base, size) == crc)
    {
        return 0;
    }
    return -1;
}

/* 擦除一个槽(从 first_sector 起连续 SLOT_SECTORS 个扇区) */
static int erase_slot(uint32_t first_sector)
{
    uint32_t sec_err = 0;
    FLASH_EraseInitTypeDef erase =
    {
        .TypeErase    = FLASH_TYPEERASE_SECTORS,
        .VoltageRange = FLASH_VOLTAGE_RANGE_3,
        .Sector       = first_sector,
        .NbSectors    = SLOT_SECTORS,
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

/* 把一个槽的内容拷到另一个槽(目标必须先擦除), size 需4字节对齐 */
static int copy_slot(uint32_t src, uint32_t dst, uint32_t size)
{
    HAL_FLASH_Unlock();
    for(uint32_t off = 0; off < size; off += 4)
    {
        uint32_t w = *(volatile uint32_t *)(src + off);
        if(HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, dst + off, w) != HAL_OK)
        {
            HAL_FLASH_Lock();
            return -1;
        }
    }
    HAL_FLASH_Lock();
    return 0;
}

/* 把备份槽K里的旧app回滚到运行槽R, 成功则更新config里R的信息 */
static int rollback_to_k(ota_app_config_t *cfg)
{
    if(verify_slot(SLOT_K_BASE, cfg->k_size, cfg->k_crc) != 0)
    {
        return -1;
    }
    if(erase_slot(SLOT_R_SECTOR) != 0)
    {
        return -1;
    }
    if(copy_slot(SLOT_K_BASE, SLOT_R_BASE, cfg->k_size) != 0)
    {
        return -1;
    }
    if(verify_slot(SLOT_R_BASE, cfg->k_size, cfg->k_crc) != 0)
    {
        return -1;
    }
    cfg->r_size = cfg->k_size;
    cfg->r_crc  = cfg->k_crc;
    return 0;
}

int ota_check_config(void)
{
    ota_app_config_t * const config = ota_read_config();
    ota_app_config_t tmp = *config;

    /* 第一次上电: config 里没有我们的 magic, 初始化一份并落盘 */
    if(tmp.header != OTA_APP_CONFIG_T_HEADER)
    {
        printf("[OTA] first boot, init config\r\n");
        tmp.header = OTA_APP_CONFIG_T_HEADER;
        tmp.state  = fail;          /* 空片没有app, 直接fail */
        tmp.cnt    = 0;
        tmp.r_size = 0; tmp.r_crc = 0;
        tmp.n_size = 0; tmp.n_crc = 0;
        tmp.k_size = 0; tmp.k_crc = 0;

        if(ota_write_config(&tmp) == 0)
        {
            return 0;
        }
        NVIC_SystemReset();
    }

    printf("[OTA] state=%lu cnt=%lu\r\n", (unsigned long)tmp.state, (unsigned long)tmp.cnt);

    switch(tmp.state)
    {
        /* 正常运行: 校验运行槽R, 好就跳, 坏就fail */
        case (valid):
        {
            if(app_is_valid() == 0 &&
               verify_slot(SLOT_R_BASE, tmp.r_size, tmp.r_crc) == 0)
            {
                printf("[OTA] R valid, jump to app\r\n");
                jump2app();
            }
            printf("[OTA] R invalid -> fail\r\n");
            tmp.state = fail;
            ota_write_config(&tmp);
            break;
        }
        /* 有固件在N: 先校验, 再备份R到K, 然后转installing */
        case (new_app):
        {
            if(verify_slot(SLOT_N_BASE, tmp.n_size, tmp.n_crc) != 0)
            {
                printf("[OTA] N verify fail -> fail\r\n");
                tmp.state = fail;
                ota_write_config(&tmp);
                break;
            }
            printf("[OTA] N ok, backup R -> K\r\n");
            if(erase_slot(SLOT_K_SECTOR) != 0 ||
               copy_slot(SLOT_R_BASE, SLOT_K_BASE, tmp.r_size) != 0)
            {
                printf("[OTA] backup R -> K fail\r\n");
                tmp.state = fail;
                ota_write_config(&tmp);
                break;
            }
            tmp.k_size = tmp.r_size;
            tmp.k_crc  = tmp.r_crc;
            /* 备份已提交, 转installing; 此处掉电可从这里续装 */
            tmp.state = installing;
            ota_write_config(&tmp);
            NVIC_SystemReset();
            break;
        }
        /* 已备份, 正在装: 把N拷到R并回读校验, 成功后转ready */
        case (installing):
        {
            if(verify_slot(SLOT_N_BASE, tmp.n_size, tmp.n_crc) != 0)
            {
                printf("[OTA] N verify fail -> fail\r\n");
                tmp.state = fail;
                ota_write_config(&tmp);
                break;
            }
            printf("[OTA] install N -> R\r\n");
            if(erase_slot(SLOT_R_SECTOR) != 0 ||
               copy_slot(SLOT_N_BASE, SLOT_R_BASE, tmp.n_size) != 0 ||
               verify_slot(SLOT_R_BASE, tmp.n_size, tmp.n_crc) != 0)
            {
                printf("[OTA] install N -> R fail\r\n");
                tmp.state = fail;
                ota_write_config(&tmp);
                break;
            }
            tmp.r_size = tmp.n_size;
            tmp.r_crc  = tmp.n_crc;
            tmp.state  = ready;         /* 等app自检 */
            tmp.cnt    = 0;
            ota_write_config(&tmp);
            NVIC_SystemReset();
            break;
        }
        /* 刚装好: R坏或自检次数超限就回滚到K, 否则给app一次启动机会 */
        case (ready):
        {
            if(verify_slot(SLOT_R_BASE, tmp.r_size, tmp.r_crc) != 0 ||
               tmp.cnt >= OTA_BOOT_MAX_TRY)
            {
                printf("[OTA] R bad or retry exhausted, rollback K -> R\r\n");
                if(rollback_to_k(&tmp) != 0)
                {
                    printf("[OTA] rollback fail -> fail\r\n");
                    tmp.state = fail;
                    ota_write_config(&tmp);
                    break;
                }
                tmp.state = valid;
                tmp.cnt   = 0;
                ota_write_config(&tmp);
                NVIC_SystemReset();
                break;
            }
            tmp.cnt++;
            ota_write_config(&tmp);
            printf("[OTA] try boot new app, cnt=%lu\r\n", (unsigned long)tmp.cnt);
            jump2app();
            break;
        }
        /* 没有可用app: 停在这里 */
        case (fail):
        default:
        {
            printf("[OTA] state fail, stop\r\n");
            break;
        }
    }
    return 0;
}

/* 擦除config所在扇区(sector4) */
int ota_erase_config(void)
{
    uint32_t sec_err = 0;

    HAL_FLASH_Unlock();
    
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR |
                           FLASH_FLAG_WRPERR | FLASH_FLAG_PGAERR |
                           FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);

    FLASH_EraseInitTypeDef erase = 
    {
        .TypeErase = FLASH_TYPEERASE_SECTORS,
        .VoltageRange = FLASH_VOLTAGE_RANGE_3,
        .Sector = FLASH_SECTOR_4,
        .NbSectors = 1,
    };
    if(HAL_FLASHEx_Erase(&erase, &sec_err) != HAL_OK)
    {
        HAL_FLASH_Lock();
        return -1;
    }
    HAL_FLASH_Lock();
    return 0;
}

/* 写config: 先写body, 最后写header(magic), 保证header在=config完整 */
int ota_write_config(const ota_app_config_t *modify)
{
    ota_app_config_t tmp = *modify;
    uint32_t *word = (uint32_t *)&tmp;
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
    // 最后写头
    if(HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, CONFIG_DATA_BASE, word[0]) != HAL_OK)
    {
        HAL_FLASH_Lock();
        return -1;
    }

    HAL_FLASH_Lock();
    return 0;
}
