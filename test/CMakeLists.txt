# Fetch all files in current dir
file(GLOB FILES *.cpp *.h)
add_executable(unittests ${FILES})

add_library(mocks STATIC mocks.cpp)

include(../cmake/cmocka.cmake)
target_link_libraries(unittests PRIVATE cmocka PicoDCCEX)

add_test(
  NAME unittests
  COMMAND unittests
)
