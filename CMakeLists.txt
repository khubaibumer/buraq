cmake_minimum_required(VERSION 3.23)
project(buraq VERSION 0.1.0 LANGUAGES CXX)

option(BURAQ_BUILD_EXAMPLES "Build example ACP agent and client" ON)
option(BURAQ_BUILD_TESTS "Build unit tests" ON)
option(BURAQ_FETCH_CODECS "Fetch Glaze / nlohmann_json automatically if not found" ON)

# --- Core Header-Only Library (Zero External Dependencies) ---
add_library(buraq INTERFACE)
add_library(buraq::buraq ALIAS buraq)

target_compile_features(buraq INTERFACE cxx_std_20)

target_sources(buraq INTERFACE
        FILE_SET HEADERS
        BASE_DIRS include
        FILES
        include/buraq/acp.hpp
        include/buraq/detail/compat.hpp
        include/buraq/protocol/jsonrpc.hpp
        include/buraq/protocol/schema_v1.hpp
        include/buraq/core/agent_base.hpp
        include/buraq/core/client_base.hpp
        include/buraq/transport/stdio.hpp
        include/buraq/codecs/glaze.hpp
        include/buraq/codecs/nlohmann.hpp
)

target_include_directories(buraq INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

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
    else()
        message(STATUS "Skipping buraq_echo_agent: glaze::glaze not available")
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
    endif()
endif()