# Add C source files here.
add_executable(step_demo
    Src/step_demo.c
    Src/stepper.c
    Src/stm32f0xx_hal_msp.c
    Src/stm32f0xx_it.c
)

# Add the map file to the list of files to be removed with 'clean' target
set_target_properties(step_demo PROPERTIES ADDITIONAL_CLEAN_FILES step_demo.map)

# Add user defined include paths
target_include_directories(step_demo PRIVATE
    Inc
)

# Add project symbols (macros)
target_compile_definitions(step_demo PRIVATE
)

# Add linked libraries
target_link_libraries(step_demo
    STM32_Drivers
)

flash_target(step_demo)
