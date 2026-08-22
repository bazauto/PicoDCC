# Generate version header with build date and git hash.
#
# Runs at configure time (so the header exists before the first compile) and
# again on every build (so the hash follows HEAD without a reconfigure).
#
# Deliberately NOT a build timestamp. A time-of-day field changes on every run,
# which makes configure_file rewrite the header every build and forces everything
# including it to recompile -- churn that shows up as builds never being a no-op.
# Date plus commit identifies an image; the seconds do not.

string(TIMESTAMP BUILD_DATE "%b %d %Y" UTC)

execute_process(
    COMMAND git rev-parse --short=7 HEAD
    WORKING_DIRECTORY ${SOURCE_DIR}
    OUTPUT_VARIABLE GIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)

# Mark a build made from a dirty tree, so an image that does not correspond to
# any commit cannot masquerade as one that does.
execute_process(
    COMMAND git status --porcelain
    WORKING_DIRECTORY ${SOURCE_DIR}
    OUTPUT_VARIABLE GIT_DIRTY
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)

if(NOT GIT_HASH)
    # No git, or not a repository: say so rather than inventing a plausible hash.
    set(GIT_HASH "nogit")
elseif(NOT GIT_DIRTY STREQUAL "")
    set(GIT_HASH "${GIT_HASH}+")
endif()

# configure_file leaves the file untouched when the content is unchanged, so a
# rebuild on the same commit and day does not trigger a recompile.
configure_file(
    ${SOURCE_DIR}/cmake/version.h.in
    ${BINARY_DIR}/generated/version.h
    @ONLY
)

message(STATUS "Generated version.h: ${BUILD_DATE} G-${GIT_HASH}")
