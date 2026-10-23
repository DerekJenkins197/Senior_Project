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

project(pico_demo C CXX ASM)

pico_sdk_init()

add_compile_definitions(
    configNUMBER_OF_CORES=1
)

add_executable(pico_demo
    src/main.c
    src/stepper.c
    src/rfid.c
)

# The 10 data chars of the valid tag, e.g. -DRFID_VALID_TAG=BEAD101000 (default is in include/rfid.h)
set(RFID_VALID_TAG "" CACHE STRING "10 hex data chars of the valid RFID tag")
if (RFID_VALID_TAG)
    target_compile_definitions(pico_demo PRIVATE RFID_VALID_TAG="${RFID_VALID_TAG}")
endif()

# RFID self-test without a card: 0 = off, 1 = internal loopback, 2 = jumper GP0 -> GP1 (see include/rfid.h)
set(RFID_SELFTEST 0 CACHE STRING "RFID self-test mode (0, 1 or 2)")
target_compile_definitions(pico_demo PRIVATE RFID_SELFTEST=${RFID_SELFTEST})

target_include_directories(pico_demo PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/include
)

target_link_libraries(pico_demo PRIVATE
    pico_stdlib
    hardware_uart
    FreeRTOS-Kernel-Heap4
)

# Pico W boards: the onboard LED is driven through the CYW43 wireless chip
if (PICO_CYW43_SUPPORTED)
    target_link_libraries(pico_demo PRIVATE pico_cyw43_arch_sys_freertos)
    target_compile_definitions(pico_demo PRIVATE CYW43_LWIP=0)   # LED only, no network stack
endif()

pico_set_program_name(pico_demo "pico_demo")
pico_set_program_description(pico_demo "SAA1042 stepper stepped by RFID tag (FreeRTOS)")

pico_enable_stdio_uart(pico_demo 1)   # console on GP4 (UART1 TX) for a USB-UART adapter, set up in main.c
pico_enable_stdio_usb(pico_demo 1)    # USB serial: same prints, and lets picotool reboot into BOOTSEL

# Generates pico_demo.uf2 alongside the .elf
pico_add_extra_outputs(pico_demo)

# `make flash` loads over USB (board must be in BOOTSEL or running a picotool-aware image)
find_program(PICOTOOL picotool)
if (PICOTOOL)
    add_custom_target(flash ${PICOTOOL} load -fx $<TARGET_FILE:pico_demo> DEPENDS pico_demo)
endif()
