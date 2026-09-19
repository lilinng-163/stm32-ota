#include <stddef.h>
#include <stdint.h>
#include "stm32f4xx_hal.h"
#include "ota_layout.h"
#include "ota_crc32.h"
#include "ota_config.h"
#include "ota_client.h"

/* N 槽在 sector7 起连续 2 个扇区(256K) */
#define SLOT_N_SECTOR   FLASH_SECTOR_7
#define SLOT_N_SECTORS  2U

/* 固件包: magic | size | crc32 | version, 各4字节小端 */
#define OTA_HDR_WORDS   4U

static uint32_t s_size;       /* 期望总字节数 */
static uint32_t s_crc;        /* 期望 CRC32 */
static uint32_t s_recv;       /* 已接收字节 */
static uint32_t s_prog;       /* 已写入N槽字节(4对齐) */
static uint8_t  s_pend[4];    /* 未满一个字的缓存 */
static uint32_t s_pend_len;

static uint32_t pack_word(const uint8_t *b)
{
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
           ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

static int flash_program_word(uint32_t addr, uint32_t word)
{
    HAL_FLASH_Unlock();
    HAL_StatusTypeDef st = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr, word);
    HAL_FLASH_Lock();
    return (st == HAL_OK) ? 0 : -1;
}

static int erase_slot_n(void)
{
    uint32_t sec_err = 0;
    FLASH_EraseInitTypeDef e =
    {
        .TypeErase    = FLASH_TYPEERASE_SECTORS,
        .VoltageRange = FLASH_VOLTAGE_RANGE_3,
        .Sector       = SLOT_N_SECTOR,
        .NbSectors    = SLOT_N_SECTORS,
    };
    HAL_FLASH_Unlock();
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                           FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);
    HAL_StatusTypeDef st = HAL_FLASHEx_Erase(&e, &sec_err);
    HAL_FLASH_Lock();
    return (st == HAL_OK) ? 0 : -1;
}

int ota_client_begin(uint32_t size, uint32_t crc)
{
    if(size == 0 || size > SLOT_SIZE)
    {
        return -1;
    }
    if(erase_slot_n() != 0)
    {
        return -1;
    }
    s_size = size;
    s_crc  = crc;
    s_recv = 0;
    s_prog = 0;
    s_pend_len = 0;
    return 0;
}

int ota_client_write(const uint8_t *data, uint32_t len)
{
    for(uint32_t i = 0; i < len; i++)
    {
        if(s_recv >= s_size)
        {
            return 0;               /* 收够了, 忽略多余数据 */
        }
        s_pend[s_pend_len++] = data[i];
        s_recv++;
        if(s_pend_len == 4U)
        {
            if(flash_program_word(SLOT_N_BASE + s_prog, pack_word(s_pend)) != 0)
            {
                return -1;
            }
            s_prog += 4U;
            s_pend_len = 0;
        }
    }
    return 0;
}

int ota_client_finish(void)
{
    /* 收尾: 不足一个字的部分补0xFF(与擦除态一致)写入 */
    if(s_pend_len)
    {
        for(uint32_t i = s_pend_len; i < 4U; i++)
        {
            s_pend[i] = 0xFF;
        }
        if(flash_program_word(SLOT_N_BASE + s_prog, pack_word(s_pend)) != 0)
        {
            return -1;
        }
        s_prog += 4U;
        s_pend_len = 0;
    }
    if(s_recv != s_size)
    {
        return -1;                  /* 没收够 */
    }
    if(crc32_flash(SLOT_N_BASE, s_size) != s_crc)
    {
        return -1;                  /* N槽内容校验失败 */
    }

    /* 记录新固件信息, 转 new_app (复位交给调用者) */
    ota_app_config_t cfg = *ota_read_config();
    cfg.n_size = s_size;
    cfg.n_crc  = s_crc;
    cfg.state  = new_app;
    return ota_write_config(&cfg);
}

int ota_client_run(const ota_io_t *io)
{
    uint32_t hdr[OTA_HDR_WORDS];
    uint8_t  buf[256];

    /* 1. 等包头(超时/内容不对就返回, 让调用者重试) */
    if(io->recv((uint8_t *)hdr, sizeof(hdr), 200) != (int)sizeof(hdr))
    {
        return -1;
    }
    if(hdr[0] != OTA_MAGIC)
    {
        return -2;
    }

    /* 2. 擦N槽 */
    if(ota_client_begin(hdr[1], hdr[2]) != 0)
    {
        io->send((const uint8_t *)OTA_ACK_ERR, sizeof(OTA_ACK_ERR) - 1U);
        return -3;
    }

    /* 3. 擦完通知上位机可以发数据了 */
    io->send((const uint8_t *)OTA_ACK_READY, sizeof(OTA_ACK_READY) - 1U);

    /* 4. 收数据, 顺序写N槽 */
    uint32_t remain = hdr[1];
    while(remain)
    {
        uint32_t n = (remain > sizeof(buf)) ? sizeof(buf) : remain;
        if(io->recv(buf, n, 5000) != (int)n)
        {
            io->send((const uint8_t *)OTA_ACK_ERR, sizeof(OTA_ACK_ERR) - 1U);
            return -4;
        }
        if(ota_client_write(buf, n) != 0)
        {
            io->send((const uint8_t *)OTA_ACK_ERR, sizeof(OTA_ACK_ERR) - 1U);
            return -5;
        }
        remain -= n;
    }

    /* 5. 校验N槽并写config(new_app) */
    if(ota_client_finish() != 0)
    {
        io->send((const uint8_t *)OTA_ACK_ERR, sizeof(OTA_ACK_ERR) - 1U);
        return -6;
    }

    io->send((const uint8_t *)OTA_ACK_OK, sizeof(OTA_ACK_OK) - 1U);
    NVIC_SystemReset();             /* 复位交给bootloader安装; 不返回 */
    return 0;
}

void ota_client_boot_check(int (*selftest)(void))
{
    ota_app_config_t *p = ota_read_config();

    if(p->state != ready)
    {
        return;                     /* 正常启动, 不用自检 */
    }

    if(selftest == NULL || selftest() == 0)
    {
        ota_app_config_t cfg = *p;
        cfg.state = valid;          /* 自检通过, 转正 */
        cfg.cnt   = 0;
        ota_write_config(&cfg);
    }
    /* 自检失败: 什么都不写, bootloader 每次启动 cnt++, 超限会回滚 */
}
