cmake_minimum_required(VERSION 3.22)
# Enable CMake support for ASM and C languages
enable_language(C ASM)

add_library(SEGGER_RTT OBJECT)
target_include_directories(SEGGER_RTT PUBLIC
    Inc
)

target_sources(SEGGER_RTT PRIVATE
    Src/SEGGER_RTT.c
    Src/SEGGER_RTT_printf.c
)
