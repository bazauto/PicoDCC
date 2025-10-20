add_library(mocks STATIC mocks.cpp)
target_link_libraries(mocks PUBLIC "${CMOCKA_LIBRARIES}")

list(APPEND TEST_TARGETS pico_dcc_packet_tests)
list(APPEND TEST_TARGETS pico_dcc_dccex_tests)
list(APPEND TEST_TARGETS pico_dcc_loco_tests)
list(APPEND TEST_TARGETS pico_dcc_locos_tests)
list(APPEND TEST_TARGETS pico_dcc_controller_tests)
list(APPEND TEST_TARGETS pico_dcc_track_tests)
list(APPEND TEST_TARGETS pico_dcc_display_tests)

list(APPEND TEST_LIBS "${CMOCKA_LIBRARIES}")
list(APPEND TEST_LIBS mocks)
list(APPEND TEST_LIBS PicoDCCEX)
list(APPEND TEST_LIBS PicoDCCLoco)
list(APPEND TEST_LIBS PicoDCCTrack)
list(APPEND TEST_LIBS PicoDCCController)
list(APPEND TEST_LIBS PicoDCCDisplay)

# Ensure cmocka is linked to all test targets
list(APPEND TEST_LIBS cmocka::cmocka)

list(APPEND TEST_DIRS "${CMOCKA_INCLUDE_DIRS}")


foreach(TEST_TARGET IN LISTS TEST_TARGETS)
    # Display tests have different dependencies (no controller, no global mocks)
    if(TEST_TARGET STREQUAL "pico_dcc_display_tests")
        set(DISPLAY_TEST_LIBS "${CMOCKA_LIBRARIES}" PicoDCCDisplay cmocka::cmocka)
        add_cmocka_test(
            ${TEST_TARGET}
            SOURCES "${TEST_TARGET}.cpp"
            COMPILE_OPTIONS ${DEFAULT_C_COMPILE_FLAGS}
            LINK_LIBRARIES "${DISPLAY_TEST_LIBS}")
    else()
        add_cmocka_test(
            ${TEST_TARGET}
            SOURCES "${TEST_TARGET}.cpp" test_globals.cpp
            COMPILE_OPTIONS ${DEFAULT_C_COMPILE_FLAGS}
            LINK_LIBRARIES "${TEST_LIBS}")
    endif()
    
    add_cmocka_test_environment(${TEST_TARGET})
    target_include_directories(${TEST_TARGET} PUBLIC "${TEST_DIRS}")
    
    # Enable C4005 warning to identify inline macro issue
    if(MSVC)
        target_compile_options(${TEST_TARGET} PRIVATE /w14005)
    endif()
endforeach()
