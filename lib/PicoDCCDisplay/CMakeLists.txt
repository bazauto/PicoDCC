# lib/PicoDCCDisplay/CMakeLists.txt

# PicoDCCDisplay Library - LCD Integration
add_library(PicoDCCDisplay STATIC
    pico_dcc_display.cpp
    lcd_driver.cpp
    touch_driver.cpp
    touch_calibration.cpp
    ui/ui_screens.cpp
    ui/ui_diagnostic_screen.cpp
)

# Include directories
target_include_directories(PicoDCCDisplay PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_include_directories(PicoDCCDisplay PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/ui
)

# LVGL library configuration
if(NOT TEST_BUILD)
    # Hardware build - include LVGL
    add_subdirectory(${CMAKE_SOURCE_DIR}/lib/external/lvgl ${CMAKE_BINARY_DIR}/lvgl)
    
    target_link_libraries(PicoDCCDisplay PUBLIC
        lvgl
        pico_stdlib
        hardware_spi
        hardware_i2c
        hardware_gpio
    )
    
    # Add LVGL configuration path - LVGL needs to find our lv_conf.h
    target_compile_definitions(PicoDCCDisplay PUBLIC
        LV_CONF_INCLUDE_SIMPLE
    )
    
    # Make our lv_conf.h visible to LVGL
    target_include_directories(PicoDCCDisplay PUBLIC
        ${CMAKE_SOURCE_DIR}/lib/external/lvgl
    )
    
    # Critical: LVGL needs to find lv_conf.h in PicoDCCDisplay directory
    target_include_directories(lvgl PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
else()
    # Test build - use mocks instead of real drivers
    target_sources(PicoDCCDisplay PRIVATE
        mocks/lcd_driver_mock.cpp
        mocks/touch_driver_mock.cpp
    )
    
    target_include_directories(PicoDCCDisplay PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/mocks
    )
    
    # Mock LVGL for test compilation (minimal stubs)
    target_compile_definitions(PicoDCCDisplay PUBLIC
        TEST_BUILD
        LV_CONF_SKIP  # Skip LVGL config in test mode
    )
endif()

# Link diagnostic system
target_link_libraries(PicoDCCDisplay PUBLIC
    # Future: Link to diagnostic library when it's extracted
)
