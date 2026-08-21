add_library(mocks STATIC mocks.cpp)
target_link_libraries(mocks PUBLIC "${CMOCKA_LIBRARIES}")

list(APPEND TEST_TARGETS pico_dcc_packet_tests)
list(APPEND TEST_TARGETS pico_dcc_dccex_tests)
list(APPEND TEST_TARGETS pico_dcc_loco_tests)
list(APPEND TEST_TARGETS pico_dcc_locos_tests)
list(APPEND TEST_TARGETS pico_dcc_controller_tests)
list(APPEND TEST_TARGETS pico_dcc_track_tests)
list(APPEND TEST_TARGETS pico_config_storage_tests)
list(APPEND TEST_TARGETS pico_dcc_display_tests)
list(APPEND TEST_TARGETS pico_diagnostic_tests)
list(APPEND TEST_TARGETS pico_dcc_wire_format_tests)

list(APPEND TEST_LIBS "${CMOCKA_LIBRARIES}")
list(APPEND TEST_LIBS mocks)
list(APPEND TEST_LIBS PicoDCCEX)
list(APPEND TEST_LIBS PicoDCCLoco)
list(APPEND TEST_LIBS PicoDCCTrack)
list(APPEND TEST_LIBS PicoDCCController)
list(APPEND TEST_LIBS PicoConfigStorage)
list(APPEND TEST_LIBS PicoDCCDisplay)
list(APPEND TEST_LIBS PicoDiagnostic)

# Ensure cmocka is linked to all test targets
list(APPEND TEST_LIBS cmocka::cmocka)

list(APPEND TEST_DIRS "${CMOCKA_INCLUDE_DIRS}")

# Enable CTest for VS Code Test Explorer integration
enable_testing()

foreach(TEST_TARGET IN LISTS TEST_TARGETS)
    # Display tests have different dependencies (no controller, no global mocks)
    if(TEST_TARGET STREQUAL "pico_dcc_display_tests")
        set(DISPLAY_TEST_LIBS "${CMOCKA_LIBRARIES}" PicoDCCDisplay cmocka::cmocka)
        add_cmocka_test(
            ${TEST_TARGET}
            SOURCES "${TEST_TARGET}.cpp"
            COMPILE_OPTIONS ${DEFAULT_C_COMPILE_FLAGS}
            LINK_LIBRARIES "${DISPLAY_TEST_LIBS}")
    # Diagnostic tests have different dependencies (no controller, no global mocks)
    elseif(TEST_TARGET STREQUAL "pico_diagnostic_tests")
        set(DIAGNOSTIC_TEST_LIBS "${CMOCKA_LIBRARIES}" PicoDiagnostic mocks cmocka::cmocka)
        add_cmocka_test(
            ${TEST_TARGET}
            SOURCES "${TEST_TARGET}.cpp"
            COMPILE_OPTIONS ${DEFAULT_C_COMPILE_FLAGS}
            LINK_LIBRARIES "${DIAGNOSTIC_TEST_LIBS}")
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
    
    # Set test properties for better output (test already registered by add_cmocka_test)
    set_tests_properties(${TEST_TARGET} PROPERTIES
        TIMEOUT 30
        LABELS "unit"
    )

    # add_cmocka_test_environment() above bakes a snapshot of the configure-time
    # PATH into each test and *replaces* PATH when the test runs, so whatever the
    # calling shell has on PATH is ignored. If that snapshot happens to carry
    # another toolchain's C++ runtime ahead of ours — Git Bash puts its own
    # mingw64 first, and its libstdc++ is a different build from msys64 ucrt64's —
    # every test dies with 0xc0000139 STATUS_ENTRYPOINT_NOT_FOUND before reaching
    # main(). Same binaries, passing or failing purely on which shell invoked
    # ctest, which is a deeply confusing failure to debug.
    #
    # ENVIRONMENT_MODIFICATION is applied after ENVIRONMENT, so this prepends the
    # compiler's own bin directory and the runtime the binaries were linked
    # against always wins. Requires CMake 3.22.
    if(WIN32 AND NOT CMAKE_VERSION VERSION_LESS 3.22)
        get_filename_component(TEST_CXX_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
        set_tests_properties(${TEST_TARGET} PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${TEST_CXX_BIN}"
        )
    endif()
endforeach()
