cmake_minimum_required(VERSION 3.23)
project(buraq VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(BURAQ_BUILD_EXAMPLES "Build example ACP agent and client" ${PROJECT_IS_TOP_LEVEL})
option(BURAQ_BUILD_TESTS "Build unit tests" ${PROJECT_IS_TOP_LEVEL})
option(BURAQ_FETCH_CODECS "Fetch Glaze / nlohmann_json automatically if not found" ON)
option(BURAQ_INSTALL "Generate install and CMake package export rules" ${PROJECT_IS_TOP_LEVEL})
option(BURAQ_ENABLE_COVERAGE "Instrument tests/examples and add the buraq_coverage target (GCC/Clang + gcovr)" OFF)
option(BURAQ_BUILD_DOCS "Add the buraq_docs target (Doxygen API reference)" OFF)

if(BURAQ_ENABLE_COVERAGE)
    if(NOT BURAQ_BUILD_TESTS)
        message(FATAL_ERROR "BURAQ_ENABLE_COVERAGE requires BURAQ_BUILD_TESTS=ON")
    endif()
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/BuraqCoverage.cmake")
endif()

if(BURAQ_BUILD_DOCS)
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/BuraqDocs.cmake")
endif()

# --- Core Header-Only Library (Zero External Dependencies) ---
add_library(buraq INTERFACE)
add_library(buraq::buraq ALIAS buraq)

target_compile_features(buraq INTERFACE cxx_std_20)

# buraq/runtime (Connection, serve_stdio, spawn_agent) uses std::thread.
find_package(Threads REQUIRED)
target_link_libraries(buraq INTERFACE Threads::Threads)

target_sources(buraq INTERFACE
        FILE_SET HEADERS
        BASE_DIRS include
        FILES
        include/buraq/acp.hpp
        include/buraq/detail/compat.hpp
        include/buraq/protocol/jsonrpc.hpp
        include/buraq/protocol/schema_v1.hpp
        include/buraq/core/agent_base.hpp
        include/buraq/core/async.hpp
        include/buraq/core/client_base.hpp
        include/buraq/transport/stdio.hpp
        include/buraq/transport/process.hpp
        include/buraq/runtime/connection.hpp
        include/buraq/codecs/glaze.hpp
        include/buraq/codecs/nlohmann.hpp
)

target_include_directories(buraq INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

# --- Install & find_package(buraq) Support ---
if(BURAQ_INSTALL)
    install(TARGETS buraq
            EXPORT buraqTargets
            FILE_SET HEADERS DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )

    install(EXPORT buraqTargets
            FILE buraqTargets.cmake
            NAMESPACE buraq::
            DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/buraq
    )

    configure_package_config_file(
            "${CMAKE_CURRENT_SOURCE_DIR}/cmake/buraqConfig.cmake.in"
            "${CMAKE_CURRENT_BINARY_DIR}/buraqConfig.cmake"
            INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/buraq
    )

    write_basic_package_version_file(
            "${CMAKE_CURRENT_BINARY_DIR}/buraqConfigVersion.cmake"
            VERSION ${PROJECT_VERSION}
            COMPATIBILITY SameMajorVersion
            ARCH_INDEPENDENT
    )

    install(FILES
            "${CMAKE_CURRENT_BINARY_DIR}/buraqConfig.cmake"
            "${CMAKE_CURRENT_BINARY_DIR}/buraqConfigVersion.cmake"
            DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/buraq
    )
endif()

# --- Optional Codec Dependencies (Only for Examples / Tests) ---
if(BURAQ_BUILD_EXAMPLES OR BURAQ_BUILD_TESTS)
    find_package(glaze QUIET)
    if(NOT glaze_FOUND AND BURAQ_FETCH_CODECS)
        include(FetchContent)
        FetchContent_Declare(
                glaze
                GIT_REPOSITORY https://github.com/stephenberry/glaze.git
                GIT_TAG v4.4.3
                GIT_SHALLOW TRUE
        )
        FetchContent_MakeAvailable(glaze)
    endif()

    find_package(nlohmann_json QUIET)
    if(NOT nlohmann_json_FOUND AND BURAQ_FETCH_CODECS)
        include(FetchContent)
        FetchContent_Declare(
                nlohmann_json
                GIT_REPOSITORY https://github.com/nlohmann/json.git
                GIT_TAG v3.11.3
                GIT_SHALLOW TRUE
        )
        FetchContent_MakeAvailable(nlohmann_json)
    endif()
endif()

# --- Examples ---
if(BURAQ_BUILD_EXAMPLES)
    if(TARGET glaze::glaze)
        add_executable(buraq_echo_agent examples/echo_agent.cpp)
        target_link_libraries(buraq_echo_agent PRIVATE buraq::buraq glaze::glaze)
        target_compile_options(buraq_echo_agent PRIVATE
                $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
                $<$<CXX_COMPILER_ID:MSVC>:/W4>
        )

        add_executable(buraq_minimal_client examples/minimal_client.cpp)
        target_link_libraries(buraq_minimal_client PRIVATE buraq::buraq glaze::glaze)
        target_compile_options(buraq_minimal_client PRIVATE
                $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
                $<$<CXX_COMPILER_ID:MSVC>:/W4>
        )
    else()
        message(STATUS "Skipping examples: glaze::glaze not available")
    endif()
endif()

# --- Tests ---
if(BURAQ_BUILD_TESTS)
    enable_testing()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/dispatch_test.cpp")
        add_executable(buraq_dispatch_test tests/dispatch_test.cpp)
        target_link_libraries(buraq_dispatch_test PRIVATE
                buraq::buraq
                $<$<TARGET_EXISTS:glaze::glaze>:glaze::glaze>
                $<$<TARGET_EXISTS:nlohmann_json::nlohmann_json>:nlohmann_json::nlohmann_json>
        )
        add_test(NAME dispatch_test COMMAND buraq_dispatch_test)

        # Same suite compiled against the amalgamated single-header distribution
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/single_include/buraq/acp.hpp"
                AND TARGET glaze::glaze AND TARGET nlohmann_json::nlohmann_json)
            add_executable(buraq_single_include_test tests/dispatch_test.cpp)
            target_compile_features(buraq_single_include_test PRIVATE cxx_std_20)
            target_include_directories(buraq_single_include_test PRIVATE
                    "${CMAKE_CURRENT_SOURCE_DIR}/single_include")
            target_link_libraries(buraq_single_include_test PRIVATE
                    glaze::glaze nlohmann_json::nlohmann_json)
            add_test(NAME single_include_test COMMAND buraq_single_include_test)
        endif()
    endif()

    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/runtime_test.cpp"
            AND TARGET glaze::glaze AND TARGET nlohmann_json::nlohmann_json)
        add_executable(buraq_runtime_test tests/runtime_test.cpp)
        target_link_libraries(buraq_runtime_test PRIVATE
                buraq::buraq glaze::glaze nlohmann_json::nlohmann_json)
        if(TARGET buraq_echo_agent)
            add_test(NAME runtime_test COMMAND buraq_runtime_test $<TARGET_FILE:buraq_echo_agent>)
        else()
            add_test(NAME runtime_test COMMAND buraq_runtime_test)
        endif()
    endif()

    # End-to-end: the minimal client spawns the echo agent over real stdio pipes.
    if(TARGET buraq_minimal_client AND TARGET buraq_echo_agent)
        add_test(NAME stdio_e2e_test
                COMMAND buraq_minimal_client $<TARGET_FILE:buraq_echo_agent>)
        set_tests_properties(stdio_e2e_test PROPERTIES PASS_REGULAR_EXPRESSION "Turn finished: end_turn")
    endif()
endif()

if(BURAQ_ENABLE_COVERAGE)
    # single_include_test is not instrumented: it would only re-measure the amalgamated copy.
    foreach(_buraq_cov_target buraq_dispatch_test buraq_runtime_test buraq_echo_agent buraq_minimal_client)
        buraq_enable_coverage(${_buraq_cov_target})
    endforeach()
endif()