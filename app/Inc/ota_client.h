#pragma once

#include <stdint.h>

/*
 * app 侧 OTA 客户端
 * 职责:
 *   1) 运行中接收新固件, 写入 N 槽, 校验后更新 config 并复位交给 bootloader 安装
 *   2) 启动时若 config.state==ready(刚被装上), 跑自检, 通过则置 valid
 * 与 bootloader 共用 address.h / check.h / crc32.h (同一份算法与结构)
 */

/* 开始接收: size=固件字节数, crc=期望CRC32(与发送端一致). 会擦除N槽. 返回0成功 */
int ota_client_begin(uint32_t size, uint32_t crc);

/* 顺序写入一段固件数据. 返回0成功 */
int ota_client_write(const uint8_t *data, uint32_t len);

/* 结束: 确认收够且N槽CRC匹配 -> 写config(state=new_app) -> 复位(不返回)
 * 校验失败返回 -1 */
int ota_client_finish(void);

/* app 启动时调用: 若 config.state==ready, 跑 selftest, 通过则置 valid+cnt=0
 * selftest 为 NULL 或返回 0 视为通过; 失败则不写, 由 bootloader 计数回滚 */
void ota_client_boot_check(int (*selftest)(void));
