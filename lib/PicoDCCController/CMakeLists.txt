project(PicoDCCController
  VERSION 1.0
  DESCRIPTION "RP2040 Raspberry Pico DCC Command Station"
  LANGUAGES C CXX
)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

add_library(${PROJECT_NAME} pico_dcccontroller.cpp)

if(TARGET generate_version)
  add_dependencies(${PROJECT_NAME} generate_version)
endif()

if(TEST_BUILD)

  # pass -D TEST_BUILD when compiling
  target_compile_definitions(${PROJECT_NAME} PUBLIC -D TEST_BUILD)
  target_link_libraries(${PROJECT_NAME} PRIVATE mocks PicoDCCTrack PicoDCCEX PicoDCCLoco)
  # Note: PicoDccProgrammer is linked directly by tests, not through controller dependency

else()

  target_link_libraries(${PROJECT_NAME} PRIVATE
          pico_stdlib
          pico_sync
          PicoDCCProgrammer
  )

endif(TEST_BUILD)