cmake_minimum_required(VERSION 3.22)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

set(CMAKE_PROJECT_NAME ota_app)
project(${CMAKE_PROJECT_NAME} C ASM)

# 复用 bootloader 工程里的 CMSIS/HAL 源码
set(ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

set(APP_SRC
    Src/app_main.c
    Src/ota_client.c
    ${ROOT}/OTA/Src/crc32.c
    ${ROOT}/Core/Src/system_stm32f4xx.c
    ${ROOT}/Core/Src/usart.c
    ${ROOT}/Core/Src/gpio.c
    ${ROOT}/Core/Src/stm32f4xx_hal_msp.c
    ${ROOT}/Core/Src/syscalls.c
    ${ROOT}/Core/Src/sysmem.c
    ${ROOT}/startup_stm32f412zx.s
    ${ROOT}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_uart.c
    ${ROOT}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_rcc.c
    ${ROOT}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_rcc_ex.c
    ${ROOT}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_flash.c
    ${ROOT}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_flash_ex.c
    ${ROOT}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_flash_ramfunc.c
    ${ROOT}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_gpio.c
    ${ROOT}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_dma.c
    ${ROOT}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_dma_ex.c
    ${ROOT}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_pwr.c
    ${ROOT}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_pwr_ex.c
    ${ROOT}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_cortex.c
    ${ROOT}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal.c
    ${ROOT}/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_exti.c
)

add_executable(${CMAKE_PROJECT_NAME} ${APP_SRC})

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    Inc
    ${ROOT}/Core/Inc
    ${ROOT}/OTA/Inc
    ${ROOT}/Drivers/STM32F4xx_HAL_Driver/Inc
    ${ROOT}/Drivers/STM32F4xx_HAL_Driver/Inc/Legacy
    ${ROOT}/Drivers/CMSIS/Device/ST/STM32F4xx/Include
    ${ROOT}/Drivers/CMSIS/Include
)

# VECT_TAB_OFFSET: app 链接在 0x08020000, 向量表偏移 0x20000
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    USE_HAL_DRIVER
    STM32F412Zx
    $<$<CONFIG:Debug>:DEBUG>
    VECT_TAB_OFFSET=0x20000
)
