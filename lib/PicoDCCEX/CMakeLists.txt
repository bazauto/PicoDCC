project(PicoDCCEX
  VERSION 1.0
  DESCRIPTION "RP2040 Raspberry Pico DCC Command Station"
  LANGUAGES C CXX
)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

add_library(${PROJECT_NAME} pico_dccex.cpp pico_dccexpacket.cpp pico_dccex_config.cpp)

if(TEST_BUILD)

  # pass -D TEST_BUILD when compiling
  target_compile_definitions(${PROJECT_NAME} PUBLIC -D TEST_BUILD)
  target_link_libraries(${PROJECT_NAME} mocks PicoConfigStorage)

else()

  target_link_libraries(${PROJECT_NAME} PRIVATE
          pico_stdlib
          pico_sync
          PicoConfigStorage
          hardware_adc
  )

endif(TEST_BUILD)