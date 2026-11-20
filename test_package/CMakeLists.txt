cmake_minimum_required(VERSION 3.15)
project(buraq_test_package LANGUAGES CXX)

option(BURAQ_TEST_GLAZE "Also exercise buraq/codecs/glaze.hpp" OFF)
option(BURAQ_TEST_NLOHMANN "Also exercise buraq/codecs/nlohmann.hpp" OFF)

find_package(buraq CONFIG REQUIRED)

add_executable(test_package test_package.cpp)
target_link_libraries(test_package PRIVATE buraq::buraq)
target_compile_features(test_package PRIVATE cxx_std_20)

if (BURAQ_TEST_GLAZE)
    find_package(glaze CONFIG REQUIRED)
    target_link_libraries(test_package PRIVATE glaze::glaze)
    target_compile_definitions(test_package PRIVATE BURAQ_TEST_GLAZE)
endif ()

if (BURAQ_TEST_NLOHMANN)
    find_package(nlohmann_json CONFIG REQUIRED)
    target_link_libraries(test_package PRIVATE nlohmann_json::nlohmann_json)
    target_compile_definitions(test_package PRIVATE BURAQ_TEST_NLOHMANN)
endif ()
