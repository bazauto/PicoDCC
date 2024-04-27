add_executable(${PROJECT_NAME} pico_dcc.cpp dcc_track_queue.cpp)
target_sources(${PROJECT_NAME} PRIVATE pico_dcc.cpp)

pico_generate_pio_header(${PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/dcc.pio)

target_link_libraries(${PROJECT_NAME} PRIVATE
        PicoDCCEX
        pico_stdlib
        hardware_pio
        hardware_adc
        pico_multicore)

pico_enable_stdio_usb(${PROJECT_NAME} 1)
pico_enable_stdio_uart(${PROJECT_NAME} 0)

pico_add_extra_outputs(${PROJECT_NAME})
        