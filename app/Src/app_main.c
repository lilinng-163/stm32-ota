#include <stdio.h>
#include "stm32f4xx_hal.h"
#include "usart.h"
#include "gpio.h"
#include "address.h"
#include "check.h"
#include "crc32.h"
#include "ota_client.h"

/* 与上位机约定的固件包头 magic */
#define OTA_MAGIC   0x444C4F41UL   /* 'AOLD' */

/* printf 重定向到 USART3 */
int __io_putchar(int ch)
{
    uint8_t c = (uint8_t)ch;
    HAL_UART_Transmit(&huart3, &c, 1U, HAL_MAX_DELAY);
    return ch;
}

void SystemClock_Config(void);

void Error_Handler(void)
{
    __disable_irq();
    while(1)
    {
    }
}

/* app 自检: 关键外设都正常就返回0. 这里只是示例 */
static int app_selftest(void)
{
    if(huart3.Instance == NULL)
    {
        return -1;
    }
    return 0;
}

int main(void)
{
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_USART3_UART_Init();

    /* 若刚被 bootloader 装上, 自检并转正为 valid */
    ota_client_boot_check(app_selftest);

    printf("[APP] running, waiting for firmware...\r\n");

    while(1)
    {
        uint32_t hdr[4];   /* magic, size, crc32, version */

        if(HAL_UART_Receive(&huart3, (uint8_t *)hdr, sizeof(hdr), 100) != HAL_OK)
        {
            continue;
        }
        if(hdr[0] != OTA_MAGIC)
        {
            printf("[APP] bad magic: %08lx\r\n", (unsigned long)hdr[0]);
            continue;
        }

        printf("[APP] recv fw: size=%lu crc=%08lx ver=%08lx\r\n",
               (unsigned long)hdr[1], (unsigned long)hdr[2], (unsigned long)hdr[3]);

        if(ota_client_begin(hdr[1], hdr[2]) != 0)
        {
            printf("[APP] begin fail\r\n");
            continue;
        }

        uint8_t  buf[256];
        uint32_t remain = hdr[1];
        int      err = 0;

        while(remain)
        {
            uint32_t n = (remain > sizeof(buf)) ? sizeof(buf) : remain;
            if(HAL_UART_Receive(&huart3, buf, n, 5000) != HAL_OK)
            {
                err = 1;
                break;
            }
            if(ota_client_write(buf, n) != 0)
            {
                err = 1;
                break;
            }
            remain -= n;
        }
        if(err)
        {
            printf("[APP] recv fail\r\n");
            continue;
        }

        /* 成功: 校验N槽, 写config, 复位交给bootloader; 不会返回 */
        if(ota_client_finish() != 0)
        {
            printf("[APP] finish fail\r\n");
        }
    }
}

/* 与 bootloader 相同的时钟配置 */
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState = RCC_HSE_BYPASS;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLM = 8;
    RCC_OscInitStruct.PLL.PLLN = 384;
    RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV4;
    RCC_OscInitStruct.PLL.PLLQ = 8;
    RCC_OscInitStruct.PLL.PLLR = 2;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    {
        Error_Handler();
    }

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_3) != HAL_OK)
    {
        Error_Handler();
    }
}
