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

include(FetchContent)

# OnnxRuntime (Linux x64)
set(ORT_VERSION "1.16.3")
if(UNIX AND NOT APPLE)
    FetchContent_Declare(
        onnxruntime
        URL https://github.com/microsoft/onnxruntime/releases/download/v${ORT_VERSION}/onnxruntime-linux-x64-${ORT_VERSION}.tgz
    )
    FetchContent_MakeAvailable(onnxruntime)

    add_library(onnxruntime SHARED IMPORTED)
    set_target_properties(onnxruntime PROPERTIES
        IMPORTED_LOCATION "${onnxruntime_SOURCE_DIR}/lib/libonnxruntime.so"
        INTERFACE_INCLUDE_DIRECTORIES "${onnxruntime_SOURCE_DIR}/include"
    )
endif()

# hnswlib
FetchContent_Declare(
    hnswlib
    GIT_REPOSITORY https://github.com/nmslib/hnswlib.git
    GIT_TAG v0.8.0
)
FetchContent_MakeAvailable(hnswlib)

# Libraries
find_package(Threads REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(CURL REQUIRED)

add_library(kestr_crypto src/engine/sha256.cpp)
add_library(kestr_ignore src/engine/ignore.cpp)
add_library(kestr_db src/engine/database.cpp)
add_library(kestr_embed src/engine/embedder.cpp src/engine/embedder_ollama.cpp src/engine/embedder_onnx.cpp src/engine/embedder_openai.cpp)
add_library(kestr_scanner src/engine/scanner.cpp)
add_library(kestr_librarian src/engine/librarian.cpp)

target_link_libraries(kestr_db PRIVATE SQLite::SQLite3)
target_link_libraries(kestr_embed PRIVATE ${CURL_LIBRARIES})
if(TARGET onnxruntime)
    target_link_libraries(kestr_embed PRIVATE onnxruntime)
    target_compile_definitions(kestr_embed PRIVATE KESTR_WITH_ONNX)
endif()
target_link_libraries(kestr_scanner PRIVATE kestr_ignore kestr_crypto)
target_link_libraries(kestr_librarian PRIVATE hnswlib)

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
    kestr_db
    kestr_embed
    kestr_librarian
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



# MCP Server Executable

add_executable(kestr-mcp

    src/mcp/main.cpp

    src/platform.hpp

    ${KESTR_OS_SRC}

)



target_link_libraries(kestr-mcp PRIVATE

    Threads::Threads

)
