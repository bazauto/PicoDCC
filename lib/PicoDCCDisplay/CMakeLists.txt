# lib/PicoDCCDisplay/CMakeLists.txt

# PicoDCCDisplay Library - LCD Integration
# Clean 3-layer architecture: Business Logic -> Renderer Facade -> Hardware Drivers

# Core business logic (no conditionals)
add_library(PicoDCCDisplay STATIC
    pico_dcc_display.cpp
)

# Include directories
target_include_directories(PicoDCCDisplay PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# LVGL renderer vs Mock renderer (conditional layer)
if(NOT TEST_BUILD)
    # Hardware build - use real LVGL renderer + hardware drivers
    add_subdirectory(${CMAKE_SOURCE_DIR}/lib/external/lvgl ${CMAKE_BINARY_DIR}/lvgl)
    
    target_sources(PicoDCCDisplay PRIVATE
        lvgl_renderer.cpp
        lcd_driver.cpp
        touch_driver.cpp
    )
    
    target_link_libraries(PicoDCCDisplay PUBLIC
        lvgl
        pico_stdlib
        hardware_spi
        hardware_i2c
        hardware_gpio
    )
    
    # LVGL configuration
    target_compile_definitions(PicoDCCDisplay PUBLIC
        LV_CONF_INCLUDE_SIMPLE
    )
    
    target_include_directories(PicoDCCDisplay PUBLIC
        ${CMAKE_SOURCE_DIR}/lib/external/lvgl
    )
    
    # Critical: LVGL needs to find lv_conf.h in PicoDCCDisplay directory
    target_include_directories(lvgl PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
else()
    # Test build - use mocks for all hardware layers
    target_sources(PicoDCCDisplay PRIVATE
        mocks/lcd_driver_mock.cpp
        mocks/touch_driver_mock.cpp
        mocks/mock_display_renderer.cpp
    )
    
    target_include_directories(PicoDCCDisplay PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/mocks
    )
    
    target_compile_definitions(PicoDCCDisplay PUBLIC
        TEST_BUILD
        LV_CONF_SKIP
    )
endif()
