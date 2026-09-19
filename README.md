# STM32F412 OTA Bootloader / App

STM32F412ZG（1MB Flash / 256KB RAM）上的三槽 OTA：bootloader + app，带 CRC 校验、
升级失败自动回滚、传输无关的固件接收接口。

仓库分两个分支：

| 分支 | 内容 |
|---|---|
| `bootloader` | bootloader：上电校验 app、安装新固件、回滚 |
| `app` | 运行在 slot R 的 app：接收新固件、启动自检转正 |

## Flash 分区（1MB）

| 区域 | 地址 | 扇区 | 大小 | 说明 |
|---|---|---|---|---|
| bootloader | 0x08000000 | S0~S3 | 64K | 上电先跑 |
| config | 0x08010000 | S4 | 64K | OTA 配置（扇区内追加日志） |
| 运行槽 R | 0x08020000 | S5~S6 | 256K | **app 固定链接地址** |
| 新固件槽 N | 0x08060000 | S7~S8 | 256K | 收到的新固件写这里 |
| 备份槽 K | 0x080A0000 | S9~S10 | 256K | 旧 app 备份，用于回滚 |
| 备用 | 0x080E0000 | S11 | 128K | 预留 |

> 为什么是"固定运行槽 + 备份"而不是真 A/B 切槽：同一份 app 二进制没法在两个不同基地址直接运行
> （向量表、`_sidata` 都是绝对地址），切槽需要 app 做重定位。故采用固定运行槽 R，
> 升级时把 N 拷到 R；升级前把旧 R 备份到 K，自检失败可用 K 回滚。

## 工作流程

**上电（bootloader）**
- `valid`：校验 R（向量表粗检 + 整镜像 CRC）→ 通过则跳转；否则 `fail`
- `new_app`：校验 N → 备份 R→K → 置 `installing`
- `installing`：擦 R、N→R、回读校验 → 置 `ready`
- `ready`：R 坏或 `cnt` 超限 → 回滚 K→R；否则 `cnt++` 后跳转，让 app 自检
- `fail`：停在 bootloader

**升级（app 运行中）**
1. app 收到固件包（`magic | size | crc32 | version`）→ 擦 N 槽 → 回 `READY`
2. 收数据写 N 槽 → 校验 CRC → 写 config `n_size/n_crc/state=new_app` → 回 `OK` → 复位
3. bootloader 安装 N→R，跳转新 app
4. 新 app 自检通过 → `state=valid`；失败则 bootloader 计数超限后回滚到 K

## config：扇区内追加日志

- 每条 `ota_app_config_t`（36 字节）占一个槽，从槽 0 顺序追加；
- 每条记录**先写 body、最后写 header(magic)**，header 命中才算有效；
- 读 = 扫描取最后一条有效记录；写 = 追加到下一个空白槽；
- **写满整扇区才擦一次**，擦前保留最后有效记录并重写，掉电安全。

## 传输抽象

`ota_client` 与传输无关，只依赖：

```c
typedef struct {
    int (*recv)(uint8_t *buf, uint32_t len, uint32_t timeout_ms);
    int (*send)(const uint8_t *buf, uint32_t len);
} ota_io_t;
```

串口（`uart_recv/uart_send`）、网口各实现一份即可。

## 目录（两分支共用结构）

```
OTA/
├── Inc|Src/ota_layout.h        分区/地址
├── Inc|Src/ota_config.h/.c     config 结构 + 追加日志读写
├── Inc|Src/ota_crc32.h/.c      CRC-32 (与 zlib 一致)
├── Inc|Src/ota_boot.h/.c       状态机 (bootloader 分支)
├── Inc|Src/ota_jump.h/.c       app_is_valid / jump2app (bootloader 分支)
└── Inc|Src/ota_client.h/.c     传输无关的客户端 (app 分支)
Core/Src/main.c                 bootloader 入口 / app 入口
```

## 构建

```sh
# bootloader 分支
cmake -S . -B build/Debug -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake -DCMAKE_BUILD_TYPE=Debug
cmake --build build/Debug

# app 分支: 链接到 0x08020000; app2 用 -DAPP_ID=2 选另一个指示灯
cmake -S . -B build/App1 ... -DAPP_ID=1
cmake -S . -B build/App2 ... -DAPP_ID=2
```

## 上位机发包示例

```python
import zlib, struct, serial
fw = open("app.bin", "rb").read()          # arm-none-eabi-objcopy -O binary app.elf app.bin
crc = zlib.crc32(fw) & 0xFFFFFFFF
ser = serial.Serial("COM7", 115200, timeout=10)
ser.write(struct.pack("<IIII", 0x444C4F41, len(fw), crc, 0x00010000))  # 包头
assert ser.read(6) == b"READY\n"           # 等擦除完成(擦N槽要几秒)
ser.write(fw)                              # 发数据; 之后等 OK 并复位
```

## 指示灯（示例）

boot→LD1(PB0)，app1→LD2(PB7)，app2→LD3(PB14)。

## TODO

- 固件版本比较 / 防降级（`version` 目前只打印）
- `fail` 状态的兜底接收通道
- 拷贝/擦写期间喂看门狗

## License

MIT，见 [LICENSE](LICENSE)。
