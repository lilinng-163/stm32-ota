#pragma once

typedef void (*Jump2Application_t)(void);

/* 粗检运行槽R像不像一个app: 看向量表头两个字(MSP, 复位入口) */
int app_is_valid(void);

/* 关中断/清外设后跳到运行槽R, 不返回 */
void jump2app(void);
