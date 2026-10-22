cmake_minimum_required(VERSION 3.22)
# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Interface library for includes and symbols
add_library(STM32_Drivers OBJECT)
target_include_directories(STM32_Drivers PUBLIC
    ${CMAKE_SOURCE_DIR}/Drivers/STM32F0xx_HAL_Driver/Inc
#    ${CMAKE_SOURCE_DIR}/Drivers/STM32F0xx_HAL_Driver/Inc/Legacy
    ${CMAKE_SOURCE_DIR}/Drivers/CMSIS/Device/ST/STM32F0xx/Include
    ${CMAKE_SOURCE_DIR}/Drivers/CMSIS/Include
    ${CMAKE_SOURCE_DIR}/Core/Inc
)

target_compile_definitions(STM32_Drivers PUBLIC 
	USE_HAL_DRIVER 
	STM32F072xB
    $<$<CONFIG:Debug>:DEBUG>
)

target_sources(STM32_Drivers PRIVATE     
    ${CMAKE_SOURCE_DIR}/Drivers/STM32F0xx_HAL_Driver/Src/stm32f0xx_hal_rcc.c
    ${CMAKE_SOURCE_DIR}/Drivers/STM32F0xx_HAL_Driver/Src/stm32f0xx_hal_rcc_ex.c
    ${CMAKE_SOURCE_DIR}/Drivers/STM32F0xx_HAL_Driver/Src/stm32f0xx_hal.c
    ${CMAKE_SOURCE_DIR}/Drivers/STM32F0xx_HAL_Driver/Src/stm32f0xx_hal_i2c.c
    ${CMAKE_SOURCE_DIR}/Drivers/STM32F0xx_HAL_Driver/Src/stm32f0xx_hal_i2c_ex.c
    ${CMAKE_SOURCE_DIR}/Drivers/STM32F0xx_HAL_Driver/Src/stm32f0xx_hal_gpio.c
    ${CMAKE_SOURCE_DIR}/Drivers/STM32F0xx_HAL_Driver/Src/stm32f0xx_hal_dma.c
    ${CMAKE_SOURCE_DIR}/Drivers/STM32F0xx_HAL_Driver/Src/stm32f0xx_hal_cortex.c
    ${CMAKE_SOURCE_DIR}/Drivers/STM32F0xx_HAL_Driver/Src/stm32f0xx_hal_pwr.c
    ${CMAKE_SOURCE_DIR}/Drivers/STM32F0xx_HAL_Driver/Src/stm32f0xx_hal_pwr_ex.c
    ${CMAKE_SOURCE_DIR}/Drivers/STM32F0xx_HAL_Driver/Src/stm32f0xx_hal_flash.c
    ${CMAKE_SOURCE_DIR}/Drivers/STM32F0xx_HAL_Driver/Src/stm32f0xx_hal_flash_ex.c
    ${CMAKE_SOURCE_DIR}/Drivers/STM32F0xx_HAL_Driver/Src/stm32f0xx_hal_exti.c
    ${CMAKE_SOURCE_DIR}/Drivers/STM32F0xx_HAL_Driver/Src/stm32f0xx_hal_adc.c
    ${CMAKE_SOURCE_DIR}/Drivers/STM32F0xx_HAL_Driver/Src/stm32f0xx_hal_adc_ex.c
    ${CMAKE_SOURCE_DIR}/Drivers/STM32F0xx_HAL_Driver/Src/stm32f0xx_hal_tim.c
    ${CMAKE_SOURCE_DIR}/Drivers/STM32F0xx_HAL_Driver/Src/stm32f0xx_hal_tim_ex.c
    ${CMAKE_SOURCE_DIR}/Drivers/STM32F0xx_HAL_Driver/Src/stm32f0xx_hal_uart.c
    ${CMAKE_SOURCE_DIR}/Drivers/STM32F0xx_HAL_Driver/Src/stm32f0xx_hal_uart_ex.c
    ${CMAKE_SOURCE_DIR}/Drivers/STM32F0xx_HAL_Driver/Src/stm32f0xx_hal_usart.c
    ${CMAKE_SOURCE_DIR}/Drivers/STM32F0xx_HAL_Driver/Src/stm32f0xx_hal_usart_ex.c
    ${CMAKE_SOURCE_DIR}/Core/Src/syscalls.c
    ${CMAKE_SOURCE_DIR}/Core/Src/sysmem.c
    ${CMAKE_SOURCE_DIR}/Core/Src/system_stm32f0xx.c
    ${CMAKE_SOURCE_DIR}/startup_stm32f072xb.s
)

target_link_libraries(STM32_Drivers PRIVATE ${TOOLCHAIN_LINK_LIBRARIES})

# Validate that STM32CubeMX code is compatible with C standard
if((CMAKE_C_STANDARD EQUAL 90) OR (CMAKE_C_STANDARD EQUAL 99))
    message(ERROR "Generated code requires C11 or higher")
endif()

add_library(STM32_Discovery OBJECT)
target_include_directories(STM32_Discovery PUBLIC
    BSP/STM32F072B-Discovery
)
target_sources(STM32_Discovery PRIVATE
    ${CMAKE_SOURCE_DIR}/Drivers/BSP/STM32F072B-Discovery/stm32f072b_discovery.c
    ${CMAKE_SOURCE_DIR}/Drivers/BSP/STM32F072B-Discovery/stm32f072b_discovery_eeprom.c
    ${CMAKE_SOURCE_DIR}/Drivers/BSP/STM32F072B-Discovery/stm32f072b_discovery_gyroscope.c
)
target_link_libraries(STM32_Discovery PRIVATE STM32_Drivers)

add_subdirectory(SEGGER)
