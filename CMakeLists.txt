cmake_minimum_required(VERSION 3.25)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if("${CMAKE_SYSTEM_NAME}" STREQUAL "iOS" OR IOS)
    set(CMAKE_OSX_ARCHITECTURES "arm64")
else()
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
endif()

set(CMAKE_CXX_VISIBILITY_PRESET hidden)

project(MusicRightsChecker VERSION 0.1.0 LANGUAGES CXX)

set(MRC_CORE_SOURCES
    src/core/Types.cpp
    src/core/JsonCodec.cpp
    src/core/LocalCache.cpp
    src/core/AttributionAndSafety.cpp
)

option(MRC_BUILD_HOST_TESTS_ONLY "Build only standalone C++23 core unit tests without Geode SDK" OFF)

if(MRC_BUILD_HOST_TESTS_ONLY)
    enable_testing()
    add_executable(mrc_core_tests
        tests/cpp/test_mrc_core.cpp
        ${MRC_CORE_SOURCES}
    )
    target_include_directories(mrc_core_tests PRIVATE include)
    if(MSVC)
        target_compile_options(mrc_core_tests PRIVATE /W4 /WX /utf-8)
    else()
        target_compile_options(mrc_core_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME mrc_core_tests COMMAND mrc_core_tests)
    return()
endif()

set(MRC_MOD_SOURCES
    ${MRC_CORE_SOURCES}
    src/net/BackendClient.cpp
    src/jukebox/JukeboxIntegration.cpp
    src/ui/RightsDetailPopup.cpp
    src/ui/RightsBrowserPopup.cpp
    src/hooks/CustomSongWidgetHook.cpp
    src/main.cpp
)

add_library(${PROJECT_NAME} SHARED ${MRC_MOD_SOURCES})
target_include_directories(${PROJECT_NAME} PRIVATE include)

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4 /utf-8 /bigobj)
    target_compile_definitions(${PROJECT_NAME} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra)
endif()

if(NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "Unable to find Geode SDK! Please define GEODE_SDK environment variable pointing to Geode SDK >= 5.10.1.")
else()
    message(STATUS "Found Geode SDK: $ENV{GEODE_SDK}")
endif()

add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)

setup_geode_mod(${PROJECT_NAME})
