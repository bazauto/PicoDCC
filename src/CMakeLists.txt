add_executable(${PROJECT_NAME} pico_dcc.cpp)
target_sources(${PROJECT_NAME} PRIVATE pico_dcc.cpp)

target_link_libraries(${PROJECT_NAME} PRIVATE
        PicoDCCController        
        PicoDCCEX
        PicoDCCTrack
        PicoDCCLoco
        PicoDCCDisplay
        PicoDiagnostic
        pico_stdlib
        hardware_pio
        hardware_adc
        pico_multicore)

pico_enable_stdio_usb(${PROJECT_NAME} 1)
pico_enable_stdio_uart(${PROJECT_NAME} 0)

# Use custom linker script to reserve last 4KB of flash for configuration storage
# Based on SDK's memmap_default.ld with only FLASH region modified to 2044k
# This prevents firmware updates from erasing the config sector (0x101FF000-0x101FFFFF)
pico_set_linker_script(${PROJECT_NAME} ${CMAKE_SOURCE_DIR}/memmap_picodcc.ld)

pico_add_extra_outputs(${PROJECT_NAME})
