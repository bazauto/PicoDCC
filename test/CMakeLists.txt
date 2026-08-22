add_library(mocks STATIC mocks.cpp)
target_link_libraries(mocks PUBLIC "${CMOCKA_LIBRARIES}")

# ---------------------------------------------------------------------------
# PIO program for pico_dcc_pio_tests
#
# The emulator executes the *assembled* dcc.pio rather than a transcription of
# it. dcc_pio_program.hex is pioasm's output, committed so the host build and CI
# work without the Pico SDK. Where pioasm is available we re-assemble and compare,
# so the committed copy cannot quietly fall behind dcc.pio.
# ---------------------------------------------------------------------------
set(DCC_PIO_SOURCE "${CMAKE_SOURCE_DIR}/lib/PicoDCCTrack/dcc.pio")
set(DCC_PIO_HEX    "${CMAKE_CURRENT_SOURCE_DIR}/dcc_pio_program.hex")

# Look for pioasm: an explicit override, then PATH, then the SDK layouts we use.
if(NOT PICODCC_PIOASM)
    file(GLOB _pioasm_candidates
         "$ENV{PICO_SDK_PATH}/../../tools/*/pioasm/pioasm*"
         "$ENV{HOME}/.pico-sdk/tools/*/pioasm/pioasm*"
         "$ENV{USERPROFILE}/.pico-sdk/tools/*/pioasm/pioasm*")
    find_program(PICODCC_PIOASM
                 NAMES pioasm
                 HINTS ${_pioasm_candidates}
                 PATHS ${_pioasm_candidates})
    if(NOT PICODCC_PIOASM AND _pioasm_candidates)
        list(GET _pioasm_candidates 0 PICODCC_PIOASM)
    endif()
endif()

if(PICODCC_PIOASM AND EXISTS "${PICODCC_PIOASM}")
    execute_process(
        COMMAND "${PICODCC_PIOASM}" -o hex "${DCC_PIO_SOURCE}"
        OUTPUT_VARIABLE _fresh_hex
        RESULT_VARIABLE _pioasm_rc
        ERROR_VARIABLE  _pioasm_err)
    if(NOT _pioasm_rc EQUAL 0)
        message(FATAL_ERROR "pioasm failed on ${DCC_PIO_SOURCE}:\n${_pioasm_err}")
    endif()

    file(READ "${DCC_PIO_HEX}" _committed_hex)
    string(REGEX REPLACE "[ \t\r\n]+" ";" _fresh_list     "${_fresh_hex}")
    string(REGEX REPLACE "[ \t\r\n]+" ";" _committed_list "${_committed_hex}")
    list(REMOVE_ITEM _fresh_list "")
    list(REMOVE_ITEM _committed_list "")

    if(NOT _fresh_list STREQUAL _committed_list)
        message(FATAL_ERROR
            "test/dcc_pio_program.hex is out of date with lib/PicoDCCTrack/dcc.pio.\n"
            "The PIO emulator would be testing a program that is no longer the one\n"
            "that runs on the board. Regenerate it with:\n"
            "    \"${PICODCC_PIOASM}\" -o hex ${DCC_PIO_SOURCE} ${DCC_PIO_HEX}")
    endif()

    # The emulator assumes the whole program sits inside .wrap. Confirm rather
    # than trust it, so adding an instruction outside the wrap cannot go unnoticed.
    execute_process(COMMAND "${PICODCC_PIOASM}" -o c-sdk "${DCC_PIO_SOURCE}"
                    OUTPUT_VARIABLE _csdk)
    string(REGEX MATCH "#define dcc_wrap_target ([0-9]+)" _m "${_csdk}")
    set(_real_wrap_target "${CMAKE_MATCH_1}")
    string(REGEX MATCH "#define dcc_wrap ([0-9]+)" _m "${_csdk}")
    set(_real_wrap "${CMAKE_MATCH_1}")
    list(LENGTH _fresh_list _n)
    math(EXPR _last "${_n} - 1")
    if(NOT _real_wrap_target EQUAL 0 OR NOT _real_wrap EQUAL _last)
        message(FATAL_ERROR
            "dcc.pio no longer wraps around its whole program "
            "(wrap_target=${_real_wrap_target}, wrap=${_real_wrap}, last=${_last}).\n"
            "test/pio_emulator.cpp assumes it does; update both together.")
    endif()
    message(STATUS "dcc.pio: verified test/dcc_pio_program.hex against ${PICODCC_PIOASM}")
else()
    message(STATUS "dcc.pio: pioasm not found, using committed test/dcc_pio_program.hex unverified")
endif()

# Turn the hex into a header the test can include.
file(STRINGS "${DCC_PIO_HEX}" _hex_words)
set(DCC_PIO_INSTRUCTIONS "")
foreach(_w IN LISTS _hex_words)
    string(STRIP "${_w}" _w)
    if(_w)
        string(APPEND DCC_PIO_INSTRUCTIONS "    0x${_w},\n")
    endif()
endforeach()
list(LENGTH _hex_words DCC_PIO_LENGTH)
set(DCC_PIO_WRAP_TARGET 0)
math(EXPR DCC_PIO_WRAP "${DCC_PIO_LENGTH} - 1")
configure_file(dcc_pio_program.h.in "${CMAKE_CURRENT_BINARY_DIR}/dcc_pio_program.h" @ONLY)

# Reconfigure when either the program or its committed assembly changes.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             "${DCC_PIO_SOURCE}" "${DCC_PIO_HEX}")

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
list(APPEND TEST_TARGETS pico_dcc_pio_tests)

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
# for the generated dcc_pio_program.h
list(APPEND TEST_DIRS "${CMAKE_CURRENT_BINARY_DIR}")

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
    # PIO tests additionally compile the instruction-set emulator
    elseif(TEST_TARGET STREQUAL "pico_dcc_pio_tests")
        add_cmocka_test(
            ${TEST_TARGET}
            SOURCES "${TEST_TARGET}.cpp" pio_emulator.cpp test_globals.cpp
            COMPILE_OPTIONS ${DEFAULT_C_COMPILE_FLAGS}
            LINK_LIBRARIES "${TEST_LIBS}")
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
