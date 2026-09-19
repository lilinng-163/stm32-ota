#include "stm32f4xx_hal.h"
#include "address.h"
#include "jump2app.h"

/* 粗看运行槽R像不像一个app: 只看向量表头两个字(MSP, 复位入口) */
int app_is_valid(void)
{
    uint32_t msp = *(volatile uint32_t *)(SLOT_R_BASE);
    uint32_t reset_irq = *(volatile uint32_t *)(SLOT_R_BASE + 4UL);

    if(msp < OTA_SRAM_BASE || msp >= OTA_SRAM_END)   /* MSP 必须落在SRAM内 */
    {
        return -1;
    }
    if((msp & 0x7) != 0)                             /* MSP 需8字节对齐 */
    {
        return -1;
    }
    if(reset_irq < SLOT_R_BASE || reset_irq > SLOT_R_END)   /* 复位入口须在R槽内 */
    {   
        return -1;
    }
    if((reset_irq & 0x1) == 0)                       /* Thumb地址最低位必须为1 */
    {
        return -1;
    }
    return 0;
}

/* 关中断/清外设后跳到运行槽R */
void jump2app(void)
{
    uint32_t msp = *(volatile uint32_t *)(SLOT_R_BASE);
    uint32_t reset_irq = *(volatile uint32_t *)(SLOT_R_BASE + 4UL);
    
    __disable_irq();

    /* 关SysTick */
    SysTick->CTRL = 0UL;
    SysTick->LOAD = 0UL;
    SysTick->VAL  = 0UL;

    /* 关所有NVIC中断并清pending */
    for(int i = 0; i < 8; i++)
    {
        NVIC->ICER[i] = 0xFFFFFFFFUL;
        NVIC->ICPR[i] = 0xFFFFFFFFUL;
    }

    SCB->VTOR = SLOT_R_BASE;   /* 向量表指向app */

    __set_MSP(msp);            /* 切到app的栈 */

    __DSB();
    __ISB();

    Jump2Application_t entry = (Jump2Application_t)(reset_irq);
    entry();                   /* 进入app复位入口 */
    while(1)
    {
        
    }
}
