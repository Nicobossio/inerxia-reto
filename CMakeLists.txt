cmake_minimum_required(VERSION 3.20)

project(inerxia
    VERSION 0.1.0
    DESCRIPTION "Inerxia Founder Engineer challenge - ISP billing and service management"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT MSVC)
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

option(INERXIA_BUILD_TESTS "Build tests" ON)

add_subdirectory(src)

if(INERXIA_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
