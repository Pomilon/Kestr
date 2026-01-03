cmake_minimum_required(VERSION 3.20)

project(kestr 
    VERSION 0.1.0 
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Platform
if(WIN32)
    add_compile_definitions(KESTR_PLATFORM_WINDOWS)
    set(KESTR_OS_SRC src/platform/platform_windows.cpp)
elseif(APPLE)
    add_compile_definitions(KESTR_PLATFORM_MACOS)
    set(KESTR_OS_SRC src/platform/platform_macos.cpp)
elseif(UNIX)
    add_compile_definitions(KESTR_PLATFORM_LINUX)
    set(KESTR_OS_SRC src/platform/platform_linux.cpp)
endif()

include_directories(
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
)

# Libraries
find_package(Threads REQUIRED)

add_library(kestr_crypto src/engine/sha256.cpp)
add_library(kestr_ignore src/engine/ignore.cpp)
add_library(kestr_scanner src/engine/scanner.cpp)
target_link_libraries(kestr_scanner PRIVATE kestr_ignore kestr_crypto)

# Executable
add_executable(kestrd 
    src/main.cpp
    src/platform.hpp
    ${KESTR_OS_SRC}
)

target_link_libraries(kestrd PRIVATE 
    kestr_scanner 
    kestr_ignore
    kestr_crypto
    Threads::Threads
)

# Client Executable
add_executable(kestr 
    src/client/main.cpp 
    src/platform.hpp
    ${KESTR_OS_SRC}
)

target_link_libraries(kestr PRIVATE 
    Threads::Threads
)
