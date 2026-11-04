cmake_minimum_required(VERSION 3.13)

# Pico SDK: -DPICO_SDK_PATH=..., $PICO_SDK_PATH, or lib/pico-sdk (needs SDK >= 2.0 for RP2350)
if (NOT DEFINED PICO_SDK_PATH AND NOT DEFINED ENV{PICO_SDK_PATH})
    set(PICO_SDK_PATH ${CMAKE_CURRENT_LIST_DIR}/lib/pico-sdk)
endif()

# FreeRTOS kernel: -DFREERTOS_KERNEL_PATH=..., $FREERTOS_KERNEL_PATH, or lib/freertos
if (NOT DEFINED FREERTOS_KERNEL_PATH)
    if (DEFINED ENV{FREERTOS_KERNEL_PATH})
        set(FREERTOS_KERNEL_PATH $ENV{FREERTOS_KERNEL_PATH})
    else()
        set(FREERTOS_KERNEL_PATH ${CMAKE_CURRENT_LIST_DIR}/lib/freertos)
    endif()
endif()
# Resolve ~ and relative paths, the same way pico_sdk_import.cmake does for the SDK
get_filename_component(FREERTOS_KERNEL_PATH "${FREERTOS_KERNEL_PATH}" REALPATH BASE_DIR "${CMAKE_BINARY_DIR}")

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# pico_w = Pico W (RP2040), also: pico, pico2, pico2_w. Override with -DPICO_BOARD=...
set(PICO_BOARD pico_w CACHE STRING "Board type")

include(pico_sdk_import.cmake)

# Pick the FreeRTOS port for the chip; the ports live in a different spot depending on the kernel version
if (PICO_PLATFORM MATCHES "^rp2040")
    set(FREERTOS_PORT_NAME RP2040)
else()
    set(FREERTOS_PORT_NAME RP2350_ARM_NTZ)
endif()
set(FREERTOS_PORT_DIRS
    ${FREERTOS_KERNEL_PATH}/portable/ThirdParty/GCC/${FREERTOS_PORT_NAME}
    ${FREERTOS_KERNEL_PATH}/portable/ThirdParty/Community-Supported-Ports/GCC/${FREERTOS_PORT_NAME}
)
foreach(dir ${FREERTOS_PORT_DIRS})
    if (EXISTS ${dir}/FreeRTOS_Kernel_import.cmake)
        set(FREERTOS_PORT_DIR ${dir})
        break()
    endif()
endforeach()
if (NOT FREERTOS_PORT_DIR)
    message(FATAL_ERROR "No ${FREERTOS_PORT_NAME} port found under FREERTOS_KERNEL_PATH (${FREERTOS_KERNEL_PATH})")
endif()
include(${FREERTOS_PORT_DIR}/FreeRTOS_Kernel_import.cmake)

project(motor_test C CXX ASM)

pico_sdk_init()

add_compile_definitions(
    configNUMBER_OF_CORES=1
)

add_executable(motor_test
    src/main.c
    src/stepper.c
)

target_include_directories(motor_test PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/include
)

target_link_libraries(motor_test PRIVATE
    pico_stdlib
    FreeRTOS-Kernel-Heap4
)

# Pico W boards: the onboard LED is driven through the CYW43 wireless chip
if (PICO_CYW43_SUPPORTED)
    target_link_libraries(motor_test PRIVATE pico_cyw43_arch_sys_freertos)
    target_compile_definitions(motor_test PRIVATE CYW43_LWIP=0)   # LED only, no network stack
endif()

pico_set_program_name(motor_test "motor_test")
pico_set_program_description(motor_test "SAA1042 stepper bench test with serial console (FreeRTOS)")

pico_enable_stdio_uart(motor_test 1)   # console output on GP4 (UART1 TX) for a USB-UART adapter, set up in main.c
pico_enable_stdio_usb(motor_test 1)    # USB serial: prints and commands, and lets picotool reboot into BOOTSEL

# Generates motor_test.uf2 alongside the .elf
pico_add_extra_outputs(motor_test)

# `cmake --build build --target flash` loads over USB (board must be in BOOTSEL or running a picotool-aware image)
find_program(PICOTOOL picotool)
if (PICOTOOL)
    add_custom_target(flash ${PICOTOOL} load -fx $<TARGET_FILE:motor_test> DEPENDS motor_test)
endif()
