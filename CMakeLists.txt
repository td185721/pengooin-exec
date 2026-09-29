# CMake root — pengooin executor
# language: CMake, target: Windows 11 x64, MSVC 19+ (VS 2022), C++20
cmake_minimum_required(VERSION 3.24)
project(pengooin LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(
    /W4 /permissive- /Zc:__cplusplus /Zc:preprocessor /EHsc /GR-
    /wd4100 /wd4201 /wd4324
    $<$<CONFIG:Release>:/O2 /GL /Gy /Gw>
)
add_link_options($<$<CONFIG:Release>:/LTCG /OPT:REF /OPT:ICF>)

add_compile_definitions(
    _WIN32_WINNT=0x0A00
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE _UNICODE
)

set(LUAU_BUILD_CLI OFF CACHE BOOL "" FORCE)
set(LUAU_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LUAU_BUILD_WEB OFF CACHE BOOL "" FORCE)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/luau/CMakeLists.txt)
    add_subdirectory(third_party/luau)
endif()

add_subdirectory(injector)
add_subdirectory(payload)
