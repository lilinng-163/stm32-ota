#pragma once

#include "ota_config.h"

/* 上电调用一次: 校验app / 升级 / 回滚; 正常会跳转或复位, 不返回 */
int ota_check_config(void);
