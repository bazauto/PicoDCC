cmake_minimum_required(VERSION 3.13)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

set(TEST_BUILD OFF CACHE BOOL "Build for testing")

if(TEST_BUILD)

  project(PicoDCCTest
    VERSION 1.0
    DESCRIPTION "RP2040 Raspberry Pico DCC Command Station - Tests"
    LANGUAGES C CXX
  )

  include_directories(
    ${CMAKE_SOURCE_DIR}/test/mocks
    ${CMAKE_SOURCE_DIR}/lib
    ${CMAKE_SOURCE_DIR}/src
  )

  list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake/cmocka)

  # Ensure cmocka is installed and linked for the test build
  find_package(cmocka 1.1.0 REQUIRED)
  include(AddCMockaTest)

  # Suppress MSVC warnings for cross-platform compatibility
  if(MSVC)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
  endif()

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

  # Suppress GCC ABI compatibility warnings for cleaner output
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_compile_options(-Wno-psabi)
  endif()

  add_subdirectory(./lib)
  add_subdirectory(./src)

endif()
