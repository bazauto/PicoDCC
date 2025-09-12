cmake_minimum_required(VERSION 3.13)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

set(TEST_BUILD OFF CACHE BOOL "Build for testing")
set(WITH_COVERAGE OFF CACHE BOOL "Include Test Coverage")

if(TEST_BUILD)

  enable_language(C)
  enable_language(CXX)

  include_directories(
    ${CMAKE_SOURCE_DIR}/test/mocks
    ${CMAKE_SOURCE_DIR}/lib
    ${CMAKE_SOURCE_DIR}/src
  )

  # Coverage
  if (WITH_COVERAGE)
    list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake/Modules)
    include(CodeCoverage)

    append_coverage_compiler_flags()

    setup_target_for_coverage_lcov(
        NAME "coverage"
        EXECUTABLE ctest -j10 -C Debug -T test --output-on-failure -R ^pico_dcc.*_tests$
        BASE_DIRECTORY "${PROJECT_SOURCE_DIR}"
        LCOV_ARGS --ignore-errors graph
        )

    set(GCOVR_ADDITIONAL_ARGS --xml-pretty --exclude-unreachable-branches --print-summary)

    setup_target_for_coverage_gcovr_xml(
        NAME "coverage_xml"
        EXECUTABLE ctest -j10 -C Debug -T test --output-on-failure -R ^pico_dcc.*_tests$
        BASE_DIRECTORY "${PROJECT_SOURCE_DIR}"
        LCOV_ARGS --ignore-errors graph
        )
  endif (WITH_COVERAGE)

  list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake/cmocka)

  project(PicoDCCTest
    VERSION 1.0
    DESCRIPTION "RP2040 Raspberry Pico DCC Command Station - Tests"
    LANGUAGES C CXX
  )

  # Ensure cmocka is installed and linked for the test build
  find_package(cmocka 1.1.0 REQUIRED)
  include(AddCMockaTest)

  add_subdirectory(./lib)
  add_subdirectory(./test)

else()

  set(PICO_CXX_ENABLE_EXCEPTIONS 1)

  if (NOT EXISTS $ENV{PICO_SDK_PATH})
    message(FATAL_ERROR "Pico Debug SDK Path not found, please set correct environment variable PICO_SDK_PATH")
  endif ()

  include($ENV{PICO_SDK_PATH}/pico_sdk_init.cmake)

  project(PicoDCC
    VERSION 1.0
    DESCRIPTION "RP2040 Raspberry Pico DCC Command Station"
    LANGUAGES C CXX ASM
  )

  pico_sdk_init()

  add_subdirectory(./lib)
  add_subdirectory(./src)

endif()