cmake_minimum_required(VERSION 3.13)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

set(TEST_BUILD ON CACHE BOOL "Build for testing")

if(TEST_BUILD)

  project(PicoDCCTest
    VERSION 1.0
    DESCRIPTION "RP2040 Raspberry Pico DCC Command Station - Tests"
    LANGUAGES C CXX
  )

  enable_testing()
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