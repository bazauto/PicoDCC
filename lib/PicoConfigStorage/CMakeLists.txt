if(TEST_BUILD)
    # Test mode: Use MSVC compiler, no Pico SDK dependencies
    add_library(PicoConfigStorage STATIC
        pico_config_storage.cpp
    )
    
    target_include_directories(PicoConfigStorage PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
    
    # Define TEST_BUILD for this library
    target_compile_definitions(PicoConfigStorage PRIVATE TEST_BUILD)
    
    target_link_libraries(PicoConfigStorage
        # No special dependencies for test mode
    )
else()
    # Hardware mode: Use Pico SDK
    add_library(PicoConfigStorage STATIC
        pico_config_storage.cpp
    )
    
    target_include_directories(PicoConfigStorage PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
    
    target_link_libraries(PicoConfigStorage
        hardware_flash
        hardware_sync
        pico_multicore
        pico_stdlib
    )
endif()
