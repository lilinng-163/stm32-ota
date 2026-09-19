#pragma once

/*
 * Flash / RAM 布局 (STM32F412ZG, 1MB Flash / 256KB RAM)
 *
 * 0x08000000  bootloader   64K   S0~S3
 * 0x08010000  config       64K   S4       OTA 配置
 * 0x08020000  运行槽 R     256K  S5~S6    <- app 固定链接地址
 * 0x08060000  新固件槽 N   256K  S7~S8
 * 0x080A0000  备份槽 K     256K  S9~S10   旧 app 备份, 用于回滚
 * 0x080E0000  备用         128K  S11
 */

#define OTA_SRAM_BASE 0x20000000UL

#define OTA_SRAM_END 0x20040000UL

#define CONFIG_DATA_BASE 0x08010000UL

#define SLOT_R_BASE 0x08020000UL

#define SLOT_N_BASE 0x08060000UL

#define SLOT_K_BASE 0x080A0000UL

#define SLOT_SIZE 0x00040000UL

#define SLOT_R_END (SLOT_R_BASE + SLOT_SIZE - 1UL)

#define SLOT_N_END (SLOT_N_BASE + SLOT_SIZE - 1UL)

#define SLOT_K_END (SLOT_K_BASE + SLOT_SIZE - 1UL)
