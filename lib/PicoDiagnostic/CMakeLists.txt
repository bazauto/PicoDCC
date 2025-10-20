# PicoDiagnostic Library
# Diagnostic logging infrastructure with circular buffer storage

if(TEST_BUILD)
    # Test mode build
    add_library(PicoDiagnostic STATIC
        pico_diagnostic.cpp
    )
    
    target_compile_definitions(PicoDiagnostic PRIVATE TEST_BUILD)
    
    target_include_directories(PicoDiagnostic PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
    )
    
else()
    # Hardware mode build
    add_library(PicoDiagnostic STATIC
        pico_diagnostic.cpp
    )
    
    target_link_libraries(PicoDiagnostic
        pico_stdlib
        pico_time
    )
    
    target_include_directories(PicoDiagnostic PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
    )
endif()
