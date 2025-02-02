add_library(mocks STATIC mocks.cpp)

list(APPEND TEST_LIBS "${CMOCKA_LIBRARIES}")
list(APPEND TEST_LIBS mocks)
list(APPEND TEST_LIBS PicoDCCEX)
list(APPEND TEST_LIBS PicoDCCLoco)

list(APPEND TEST_DIRS "${CMOCKA_INCLUDE_DIRS}")


add_cmocka_test(pico_dcc_loco_tests
                SOURCES pico_dcc_loco_tests.cpp
                COMPILE_OPTIONS ${DEFAULT_C_COMPILE_FLAGS}
                LINK_LIBRARIES "${TEST_LIBS}")

add_cmocka_test_environment(pico_dcc_loco_tests)

target_include_directories(pico_dcc_loco_tests PUBLIC "${TEST_DIRS}")




add_cmocka_test(pico_dcc_packet_tests
                SOURCES pico_dcc_packet_tests.cpp
                COMPILE_OPTIONS ${DEFAULT_C_COMPILE_FLAGS}
                LINK_LIBRARIES "${TEST_LIBS}")

add_cmocka_test_environment(pico_dcc_packet_tests)

target_include_directories(pico_dcc_packet_tests PUBLIC "${TEST_DIRS}")