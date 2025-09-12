add_library(mocks STATIC mocks.cpp)
target_link_libraries(mocks PUBLIC "${CMOCKA_LIBRARIES}")

list(APPEND TEST_TARGETS pico_dcc_packet_tests)
list(APPEND TEST_TARGETS pico_dcc_dccex_tests)
list(APPEND TEST_TARGETS pico_dcc_loco_tests)
list(APPEND TEST_TARGETS pico_dcc_locos_tests)

list(APPEND TEST_LIBS "${CMOCKA_LIBRARIES}")
list(APPEND TEST_LIBS mocks)
list(APPEND TEST_LIBS PicoDCCEX)
list(APPEND TEST_LIBS PicoDCCLoco)
list(APPEND TEST_LIBS PicoDCCTrack)
list(APPEND TEST_LIBS PicoDCCController)

# Ensure cmocka is linked to all test targets
list(APPEND TEST_LIBS cmocka::cmocka)

list(APPEND TEST_DIRS "${CMOCKA_INCLUDE_DIRS}")

foreach(TEST_TARGET IN LISTS TEST_TARGETS)
    add_cmocka_test(
        ${TEST_TARGET}
        SOURCES "${TEST_TARGET}.cpp"
        COMPILE_OPTIONS ${DEFAULT_C_COMPILE_FLAGS}
        LINK_LIBRARIES "${TEST_LIBS}")
    add_cmocka_test_environment(${TEST_TARGET})
    target_include_directories(${TEST_TARGET} PUBLIC "${TEST_DIRS}")
endforeach()

set_property(TARGET pico_dcc_locos_tests
             PROPERTY LINK_FLAGS "${DEFAULT_LINK_FLAGS} -Wl,--wrap=queue_add_blocking")

set_property(TARGET pico_dcc_dccex_tests
             PROPERTY LINK_FLAGS "${DEFAULT_LINK_FLAGS} -Wl,--wrap=setup_default_uart,--wrap=uart_puts")
