set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
cmake_minimum_required(VERSION 3.20)

project(Digital-Padlock C)

# Add C source files here.
add_executable(keypad
    Core/Src/keypad.c
    Core/Src/hal_gpio.c
    Core/Src/stm32f0xx_hal_msp.c
    Core/Src/stm32f0xx_it.c
)

# Add the map file to the list of files to be removed with 'clean' target
set_target_properties(keypad PROPERTIES ADDITIONAL_CLEAN_FILES keypad.map)

# Add user defined include paths
target_include_directories(keypad PRIVATE
    Core/Inc
)

# Add project symbols (macros)
target_compile_definitions(keypad PRIVATE
)

# Add linked libraries
target_link_libraries(keypad
    STM32_Drivers
)

add_subdirectory(Drivers)

add_custom_target(flash_keypad
    COMMAND openocd
        -f interface/stlink.cfg
        -f target/stm32f0x.cfg
        -c "program ${CMAKE_BINARY_DIR}/keypad.elf verify reset exit"
    DEPENDS keypad
)