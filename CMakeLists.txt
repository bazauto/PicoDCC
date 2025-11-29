# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
cmake_minimum_required(VERSION 3.13)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

set(PICO_BOARD pico2 CACHE STRING "Board type")
set(PICO_PLATFORM rp2350)

set(TEST_BUILD OFF CACHE BOOL "Build for testing")

# Generate version header with build timestamp and git hash (for all build modes)
add_custom_target(generate_version ALL
  COMMAND ${CMAKE_COMMAND}
    -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
    -DBINARY_DIR=${CMAKE_BINARY_DIR}
    -P ${CMAKE_SOURCE_DIR}/cmake/generate_version.cmake
  BYPRODUCTS ${CMAKE_BINARY_DIR}/generated/version.h
  COMMENT "Generating version.h"
)

# Create generated directory
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/generated)

if(TEST_BUILD)

  project(PicoDCCTest
    VERSION 1.0
    DESCRIPTION "RP2350 Raspberry Pico DCC Command Station - Tests"
    LANGUAGES C CXX
  )

  include_directories(
    ${CMAKE_SOURCE_DIR}/test/mocks
    ${CMAKE_SOURCE_DIR}/lib
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/generated
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
    DESCRIPTION "RP2350 Raspberry Pico DCC Command Station"
    LANGUAGES C CXX ASM
  )

  pico_sdk_init()

  # Suppress GCC ABI compatibility warnings for cleaner output
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_compile_options(-Wno-psabi)
  endif()

  # Add generated directory to include path
  include_directories(${CMAKE_BINARY_DIR}/generated)

  add_subdirectory(./lib)
  add_subdirectory(./src)

endif()
