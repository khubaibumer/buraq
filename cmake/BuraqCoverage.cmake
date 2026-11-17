# Code coverage for buraq's headers, measured through the test suite and examples.
#
#   cmake -B build-cov -DCMAKE_BUILD_TYPE=Debug -DBURAQ_ENABLE_COVERAGE=ON
#   cmake --build build-cov --target buraq_coverage
#
# Requires GCC or Clang plus gcovr >= 8.3 (https://gcovr.com, `pip install gcovr`). Reports go to
# <build>/coverage/: index.html (per-line), coverage.xml (Cobertura), summary.json,
# and summary.md. A text summary is printed to the console.

set(BURAQ_COVERAGE_FAIL_UNDER_LINE "0" CACHE STRING "Fail buraq_coverage when line coverage is below this percentage")

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    message(FATAL_ERROR "BURAQ_ENABLE_COVERAGE requires GCC or Clang (found ${CMAKE_CXX_COMPILER_ID})")
endif()

# Usage requirements applied to every instrumented target (tests and examples).
add_library(buraq_coverage_flags INTERFACE)
target_compile_options(buraq_coverage_flags INTERFACE --coverage -O0 -g -fno-inline)
target_link_options(buraq_coverage_flags INTERFACE --coverage)

function(buraq_enable_coverage target)
    if(TARGET ${target})
        target_link_libraries(${target} PRIVATE buraq_coverage_flags)
    endif()
endfunction()

find_program(BURAQ_GCOVR_EXECUTABLE gcovr)

# Clang emits gcov-compatible data that must be read with the matching `llvm-cov gcov`.
set(_buraq_gcov_args "")
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    get_filename_component(_buraq_cxx_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
    find_program(BURAQ_LLVM_COV_EXECUTABLE NAMES llvm-cov HINTS "${_buraq_cxx_dir}")
    if(BURAQ_LLVM_COV_EXECUTABLE)
        set(_buraq_gcov_args --gcov-executable "${BURAQ_LLVM_COV_EXECUTABLE} gcov")
    endif()
else()
    get_filename_component(_buraq_cxx_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
    string(REGEX MATCH "-[0-9]+$" _buraq_gcc_suffix "${CMAKE_CXX_COMPILER}")
    find_program(BURAQ_GCOV_EXECUTABLE NAMES gcov${_buraq_gcc_suffix} gcov HINTS "${_buraq_cxx_dir}")
    if(BURAQ_GCOV_EXECUTABLE)
        set(_buraq_gcov_args --gcov-executable "${BURAQ_GCOV_EXECUTABLE}")
    endif()
endif()

if(NOT BURAQ_GCOVR_EXECUTABLE)
    message(WARNING "BURAQ_ENABLE_COVERAGE: gcovr not found; the buraq_coverage target will be unavailable "
                    "(install it with `pip install gcovr`)")
    return()
endif()

set(_buraq_cov_dir "${CMAKE_BINARY_DIR}/coverage")
set(_buraq_gcovr_common
        "${BURAQ_GCOVR_EXECUTABLE}"
        --root "${PROJECT_SOURCE_DIR}"
        --object-directory "${CMAKE_BINARY_DIR}"
        --filter "${PROJECT_SOURCE_DIR}/include/"
        --exclude-unreachable-branches
        --exclude-throw-branches
        --merge-lines
        --gcov-ignore-parse-errors=negative_hits.warn_once_per_file
        ${_buraq_gcov_args})

add_custom_target(buraq_coverage
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${_buraq_cov_dir}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_buraq_cov_dir}"
        # Reset counters so repeated runs report only this run.
        COMMAND ${_buraq_gcovr_common} --delete --txt "${_buraq_cov_dir}/.reset.txt"
        COMMAND ${CMAKE_CTEST_COMMAND} --test-dir "${CMAKE_BINARY_DIR}" -C $<CONFIG> --output-on-failure
        COMMAND ${_buraq_gcovr_common}
                --txt --txt-summary --sort uncovered-percent
                --html-details "${_buraq_cov_dir}/index.html"
                --html-title "buraq coverage"
                --cobertura "${_buraq_cov_dir}/coverage.xml"
                --json-summary-pretty --json-summary "${_buraq_cov_dir}/summary.json"
                --markdown-summary "${_buraq_cov_dir}/summary.md"
                --fail-under-line ${BURAQ_COVERAGE_FAIL_UNDER_LINE}
        COMMAND ${CMAKE_COMMAND} -E echo "Coverage report: ${_buraq_cov_dir}/index.html"
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
        USES_TERMINAL
        COMMENT "Running tests and collecting buraq header coverage")
