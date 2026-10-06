cmake_minimum_required(VERSION 3.25)

project(OpenBL
    VERSION 0.0.1
    DESCRIPTION "Open-source engine reimplementation compatible with Bloodlines game data"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "OpenBL is 64-bit only.")
endif()

option(OPENBL_BUILD_TESTS "Build unit tests" ON)
option(OPENBL_BUILD_TOOLS "Build command-line tools" ON)
option(OPENBL_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

# Shared compile options for all first-party targets.
add_library(openbl_options INTERFACE)
if(MSVC)
    target_compile_options(openbl_options INTERFACE /W4 /permissive- /utf-8 /Zc:__cplusplus)
    target_compile_definitions(openbl_options INTERFACE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
    if(OPENBL_WARNINGS_AS_ERRORS)
        target_compile_options(openbl_options INTERFACE /WX)
    endif()
else()
    target_compile_options(openbl_options INTERFACE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    if(OPENBL_WARNINGS_AS_ERRORS)
        target_compile_options(openbl_options INTERFACE -Werror)
    endif()
endif()

add_subdirectory(src)

if(OPENBL_BUILD_TOOLS)
    add_subdirectory(tools)
endif()

if(OPENBL_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
