# Generate version header with build timestamp and git hash

# Get current date/time
string(TIMESTAMP BUILD_DATE "%b %d %Y" UTC)
string(TIMESTAMP BUILD_TIME "%H:%M:%S" UTC)

# Try to get git hash
execute_process(
    COMMAND git rev-parse --short=7 HEAD
    WORKING_DIRECTORY ${SOURCE_DIR}
    OUTPUT_VARIABLE GIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)

# If git failed or not in a git repo, use placeholder
if(NOT GIT_HASH)
    set(GIT_HASH "abc123")
endif()

# Generate version header file
configure_file(
    ${SOURCE_DIR}/cmake/version.h.in
    ${BINARY_DIR}/generated/version.h
    @ONLY
)

message(STATUS "Generated version.h: ${BUILD_DATE} ${BUILD_TIME} G-${GIT_HASH}")
