#pragma once

#include <stdint.h>

/*
 * app 侧 OTA 客户端
 * - OTA 逻辑与传输无关: 串口/网口只需实现 ota_io_t 的 recv/send
 * - 与 bootloader 共用 ota_layout.h / ota_config.h / ota_crc32.h
 */

/* 固件包 magic, 上位机需一致 */
#define OTA_MAGIC   0x444C4F41UL   /* 'AOLD' */

/* 流程应答(单行文本, 上位机据此决定何时发数据) */
#define OTA_ACK_READY  "READY\n"   /* 已擦好N槽, 可以发固件数据 */
#define OTA_ACK_OK     "OK\n"      /* 收完并校验通过, 即将复位安装 */
#define OTA_ACK_ERR    "ERR\n"     /* 出错 */

/* 传输层接口: 串口/网口各自实现 */
typedef struct
{
    /* 收满 len 字节: 成功返回 len, 超时返回 0, 出错返回 <0 */
    int (*recv)(uint8_t *buf, uint32_t len, uint32_t timeout_ms);
    /* 发送 len 字节: 成功返回 >=0, 失败返回 <0 */
    int (*send)(const uint8_t *buf, uint32_t len);
} ota_io_t;

/* 高层: 跑完整流程(等包头 -> 擦N槽 -> 应答READY -> 收数据写N槽 -> 校验 -> 置new_app -> 应答OK -> 复位)
 * 依赖 io 的收发; 成功不返回(复位), 失败返回 <0 */
int ota_client_run(const ota_io_t *io);

/* 低层(需要自己处理传输时用) */
int ota_client_begin(uint32_t size, uint32_t crc);   /* 擦除N槽, 记下 size/crc */
int ota_client_write(const uint8_t *data, uint32_t len);
int ota_client_finish(void);   /* 校验收齐且N槽CRC匹配 -> 写config(state=new_app); 不复位 */

/* app 启动时调用: 若 config.state==ready, 跑 selftest, 通过则置 valid+cnt=0
 * selftest 为 NULL 或返回 0 视为通过; 失败则不写, 由 bootloader 计数回滚 */
void ota_client_boot_check(int (*selftest)(void));
