cmake_minimum_required(VERSION 3.13)

if (NOT EXISTS $ENV{PICO_SDK_PATH})
message(FATAL_ERROR "Pico Debug SDK Path not found, please set correct environment variable PICO_SDK_PATH")
endif ()

include($ENV{PICO_SDK_PATH}/pico_sdk_init.cmake)
pico_sdk_init()

project(PicoDCC
  VERSION 1.0
  DESCRIPTION "RP2040 Raspberry Pico DCC Command Station"
  LANGUAGES C CXX ASM
)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

add_subdirectory(./lib)
add_subdirectory(./src)