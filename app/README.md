# app 侧 OTA 例程

这是运行在**运行槽 R (0x08020000)** 的 app 示例，配合 bootloader 完成升级闭环。

## 分区回顾

| 区域 | 地址 | 大小 |
|---|---|---|
| bootloader | 0x08000000 | 64K |
| config | 0x08010000 | 64K |
| 运行槽 R | 0x08020000 | 256K ← **本 app 链接地址** |
| 新固件槽 N | 0x08060000 | 256K |
| 备份槽 K | 0x080A0000 | 256K |

app 必须 ≤256K，且链接到 `0x08020000`。

## 目录

```
app/
├── CMakeLists.txt          独立构建
├── STM32F412XX_FLASH.ld    FLASH ORIGIN=0x08020000, LENGTH=256K
├── Inc/ota_client.h
└── Src/
    ├── ota_client.c        OTA 客户端(写N槽 / 改config / 启动自检)
    └── app_main.c          示例 main(串口收固件)
```

`ota_client.c` 与 bootloader **共用** `OTA/Inc/address.h`、`check.h`、`crc32.h`，保证地址、
config 结构、CRC 算法完全一致。

## 构建

```sh
cmake -S app -B app/build -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake \
      -DCMAKE_BUILD_TYPE=Debug
cmake --build app/build
# 产物: app/build/ota_app.elf
```

要点：`CMakeLists.txt` 里定义了 `VECT_TAB_OFFSET=0x20000`，`SystemInit` 才会把
`SCB->VTOR` 设到 `0x08020000`。

## app 的两件事

**1. 运行中收到新固件**（`app_main.c` 里用 USART3 演示）
- 上位机先发 16 字节包头：`magic('AOLD') | size | crc32 | version`
- app 调 `ota_client_begin(size, crc)` → 擦 N 槽
- 分块 `HAL_UART_Receive` 收到数据 → `ota_client_write(buf, n)` 顺序写入 N 槽
- 收够后 `ota_client_finish()`：校验 N 槽 CRC → 写 config `n_size/n_crc/state=new_app` → `NVIC_SystemReset()`

复位后 bootloader 接手：`state==new_app` → 校验 N → 备份 R→K → 装 N→R → `ready`。

**2. 启动自检**（`main` 开头）
- `ota_client_boot_check(app_selftest)`：
  - `config.state != ready` → 什么都不做（正常启动）
  - `state == ready` 且自检通过 → 写 `state=valid, cnt=0`
  - 自检失败 → 什么都不写，bootloader 每次启动 `cnt++`，超过 3 次自动回滚到 K

## 上位机发包示例

```python
import zlib, struct, serial
fw = open("app.bin", "rb").read()
crc = zlib.crc32(fw) & 0xFFFFFFFF
ser = serial.Serial("COM7", 115200)
ser.write(struct.pack("<IIII", 0x444C4F41, len(fw), crc, 0x00010000))
ser.write(fw)
```

注意 CRC 用标准 zlib，和 `crc32.c` 一致。

## 首次安装（出厂/无 app 时）

空片第一次 bootloader 是 `state=fail`。要让它跑起来，需要：
1. 用编程器/OpenOCD 把 app 烧到 R 槽（0x08020000）；
2. 把 app 的 `size`、`crc32` 写进 config 的 `r_size/r_crc`，并置 `state=valid`。

（这一步可以用一个小脚本通过 OpenOCD 写 config 扇区完成，或后续加一个"出厂模式"。）
