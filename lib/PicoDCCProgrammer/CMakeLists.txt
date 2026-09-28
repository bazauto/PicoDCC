cmake_minimum_required(VERSION 3.13)

if(TEST_BUILD)
    # Test mode - create static library for test linking
    add_library(PicoDccProgrammer STATIC
        ${CMAKE_CURRENT_LIST_DIR}/pico_dcc_programmer.cpp
    )
    
    target_compile_definitions(PicoDccProgrammer PRIVATE TEST_BUILD)
    
    target_include_directories(PicoDccProgrammer PUBLIC 
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/..
    )
else()
    # Hardware mode - create static library
    add_library(PicoDCCProgrammer STATIC
        ${CMAKE_CURRENT_LIST_DIR}/pico_dcc_programmer.cpp
    )
    
    target_include_directories(PicoDCCProgrammer PUBLIC 
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/..
    )
    
    # Link with required libraries
    target_link_libraries(PicoDCCProgrammer PUBLIC
        pico_stdlib
        hardware_adc
    )
endif()
