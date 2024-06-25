project(PicoDCCLoco
  VERSION 1.0
  DESCRIPTION "RP2040 Raspberry Pico DCC Command Station"
  LANGUAGES C CXX
)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

add_library(${PROJECT_NAME} pico_dccloco.cpp pico_dcclocos.cpp)

target_link_libraries(${PROJECT_NAME} PRIVATE
        pico_stdlib
        PicoDCCTrack
)
