#pragma once

#include <stdint.h>

/* config 有效性 magic, 命中才认为配置可用 */
#define OTA_APP_CONFIG_T_HEADER 0x20051129UL

/*
 * OTA 配置, 存在 CONFIG_DATA_BASE(sector4)
 * r_/n_/k_ 分别是 运行槽R / 新固件槽N / 备份槽K 的镜像信息
 */
typedef struct
{
    uint32_t header;   /* magic */
    uint32_t state;    /* ota_state */
    uint32_t cnt;      /* ready状态下连续启动次数, 超限回滚 */
    uint32_t r_size;   /* 运行槽R 镜像大小 */
    uint32_t r_crc;    /* 运行槽R 镜像 CRC32 */
    uint32_t n_size;   /* 新固件槽N 镜像大小 */
    uint32_t n_crc;    /* 新固件槽N 镜像 CRC32 */
    uint32_t k_size;   /* 备份槽K 镜像大小 */
    uint32_t k_crc;    /* 备份槽K 镜像 CRC32 */
}ota_app_config_t;

typedef enum
{
    valid = 0,   /* 运行槽R可用, 正常跑 */
    new_app,     /* 新固件已写N, 待备份+安装 */
    installing,  /* 已备份到K, 正在 N->R */
    ready,       /* R刚写入, 等app自检 */
    fail,        /* 无可用app */
}ota_state;

/* 上电调用一次: 校验app/升级/回滚, 正常会跳转或复位, 不返回 */
int ota_check_config(void);

ota_app_config_t *ota_read_config(void);

int ota_erase_config(void);

int ota_write_config(const ota_app_config_t *modify);
