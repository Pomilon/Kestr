cmake_minimum_required(VERSION 3.20)

project(Kestr 
    VERSION 0.2.0 
    LANGUAGES CXX C
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Suppress FetchContent warnings and set modern behavior
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW) # Use extraction time for timestamps
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Platform
if(WIN32)
    add_compile_definitions(KESTR_PLATFORM_WINDOWS NOMINMAX)
    set(KESTR_OS_SRC src/platform/platform_windows.cpp)
elseif(APPLE)
    add_compile_definitions(KESTR_PLATFORM_MACOS)
    set(KESTR_OS_SRC src/platform/platform_macos.cpp)
    find_library(CORE_SERVICES_FRAMEWORK CoreServices)
    set(KESTR_PLATFORM_LIBS ${CORE_SERVICES_FRAMEWORK})
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

# tree-sitter core
FetchContent_Declare(
    tree_sitter
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter.git
    GIT_TAG v0.24.2
)
FetchContent_GetProperties(tree_sitter)
if(NOT tree_sitter_POPULATED)
    FetchContent_Populate(tree_sitter)
    add_library(tree-sitter STATIC ${tree_sitter_SOURCE_DIR}/lib/src/lib.c)
    target_include_directories(tree-sitter PUBLIC ${tree_sitter_SOURCE_DIR}/lib/include ${tree_sitter_SOURCE_DIR}/lib/src)
    set_target_properties(tree-sitter PROPERTIES LINKER_LANGUAGE C)
endif()

# tree-sitter-python
FetchContent_Declare(
    tree_sitter_python
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-python.git
    GIT_TAG v0.23.2
)
FetchContent_GetProperties(tree_sitter_python)
if(NOT tree_sitter_python_POPULATED)
    FetchContent_Populate(tree_sitter_python)
    add_library(tree-sitter-python STATIC 
        ${tree_sitter_python_SOURCE_DIR}/src/parser.c
        ${tree_sitter_python_SOURCE_DIR}/src/scanner.c
    )
    target_include_directories(tree-sitter-python PUBLIC ${tree_sitter_python_SOURCE_DIR}/src)
    target_link_libraries(tree-sitter-python PRIVATE tree-sitter)
    set_target_properties(tree-sitter-python PROPERTIES LINKER_LANGUAGE C)
endif()

# tree-sitter-cpp
FetchContent_Declare(
    tree_sitter_cpp
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-cpp.git
    GIT_TAG v0.23.4
)
FetchContent_GetProperties(tree_sitter_cpp)
if(NOT tree_sitter_cpp_POPULATED)
    FetchContent_Populate(tree_sitter_cpp)
    add_library(tree-sitter-cpp STATIC 
        ${tree_sitter_cpp_SOURCE_DIR}/src/parser.c
        ${tree_sitter_cpp_SOURCE_DIR}/src/scanner.c
    )
    target_include_directories(tree-sitter-cpp PUBLIC ${tree_sitter_cpp_SOURCE_DIR}/src)
    target_link_libraries(tree-sitter-cpp PRIVATE tree-sitter)
    set_target_properties(tree-sitter-cpp PROPERTIES LINKER_LANGUAGE C)
endif()

# tree-sitter-go
FetchContent_Declare(
    tree_sitter_go
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-go.git
    GIT_TAG v0.23.4
)
FetchContent_GetProperties(tree_sitter_go)
if(NOT tree_sitter_go_POPULATED)
    FetchContent_Populate(tree_sitter_go)
    add_library(tree-sitter-go STATIC 
        ${tree_sitter_go_SOURCE_DIR}/src/parser.c
    )
    target_include_directories(tree-sitter-go PUBLIC ${tree_sitter_go_SOURCE_DIR}/src)
    target_link_libraries(tree-sitter-go PRIVATE tree-sitter)
    set_target_properties(tree-sitter-go PROPERTIES LINKER_LANGUAGE C)
endif()

# tree-sitter-rust
FetchContent_Declare(
    tree_sitter_rust
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-rust.git
    GIT_TAG v0.23.2
)
FetchContent_GetProperties(tree_sitter_rust)
if(NOT tree_sitter_rust_POPULATED)
    FetchContent_Populate(tree_sitter_rust)
    add_library(tree-sitter-rust STATIC 
        ${tree_sitter_rust_SOURCE_DIR}/src/parser.c
        ${tree_sitter_rust_SOURCE_DIR}/src/scanner.c
    )
    target_include_directories(tree-sitter-rust PUBLIC ${tree_sitter_rust_SOURCE_DIR}/src)
    target_link_libraries(tree-sitter-rust PRIVATE tree-sitter)
    set_target_properties(tree-sitter-rust PROPERTIES LINKER_LANGUAGE C)
endif()

# tree-sitter-javascript
FetchContent_Declare(
    tree_sitter_javascript
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-javascript.git
    GIT_TAG v0.23.1
)
FetchContent_GetProperties(tree_sitter_javascript)
if(NOT tree_sitter_javascript_POPULATED)
    FetchContent_Populate(tree_sitter_javascript)
    add_library(tree-sitter-javascript STATIC 
        ${tree_sitter_javascript_SOURCE_DIR}/src/parser.c
        ${tree_sitter_javascript_SOURCE_DIR}/src/scanner.c
    )
    target_include_directories(tree-sitter-javascript PUBLIC ${tree_sitter_javascript_SOURCE_DIR}/src)
    target_link_libraries(tree-sitter-javascript PRIVATE tree-sitter)
    set_target_properties(tree-sitter-javascript PROPERTIES LINKER_LANGUAGE C)
endif()

# tree-sitter-java
FetchContent_Declare(
    tree_sitter_java
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-java.git
    GIT_TAG v0.23.0
)
FetchContent_GetProperties(tree_sitter_java)
if(NOT tree_sitter_java_POPULATED)
    FetchContent_Populate(tree_sitter_java)
    add_library(tree-sitter-java STATIC 
        ${tree_sitter_java_SOURCE_DIR}/src/parser.c
    )
    target_include_directories(tree-sitter-java PUBLIC ${tree_sitter_java_SOURCE_DIR}/src)
    target_link_libraries(tree-sitter-java PRIVATE tree-sitter)
    set_target_properties(tree-sitter-java PROPERTIES LINKER_LANGUAGE C)
endif()

# tree-sitter-c-sharp
FetchContent_Declare(
    tree_sitter_c_sharp
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-c-sharp.git
    GIT_TAG v0.23.0
)
FetchContent_GetProperties(tree_sitter_c_sharp)
if(NOT tree_sitter_c_sharp_POPULATED)
    FetchContent_Populate(tree_sitter_c_sharp)
    add_library(tree-sitter-c-sharp STATIC 
        ${tree_sitter_c_sharp_SOURCE_DIR}/src/parser.c
        ${tree_sitter_c_sharp_SOURCE_DIR}/src/scanner.c
    )
    target_include_directories(tree-sitter-c-sharp PUBLIC ${tree_sitter_c_sharp_SOURCE_DIR}/src)
    target_link_libraries(tree-sitter-c-sharp PRIVATE tree-sitter)
    set_target_properties(tree-sitter-c-sharp PROPERTIES LINKER_LANGUAGE C)
endif()

# tree-sitter-php
FetchContent_Declare(
    tree_sitter_php
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-php.git
    GIT_TAG v0.23.0
)
FetchContent_GetProperties(tree_sitter_php)
if(NOT tree_sitter_php_POPULATED)
    FetchContent_Populate(tree_sitter_php)
    add_library(tree-sitter-php STATIC 
        ${tree_sitter_php_SOURCE_DIR}/php/src/parser.c
        ${tree_sitter_php_SOURCE_DIR}/php/src/scanner.c
    )
    target_include_directories(tree-sitter-php PUBLIC ${tree_sitter_php_SOURCE_DIR}/php/src)
    target_link_libraries(tree-sitter-php PRIVATE tree-sitter)
    set_target_properties(tree-sitter-php PROPERTIES LINKER_LANGUAGE C)
endif()

# tree-sitter-ruby
FetchContent_Declare(
    tree_sitter_ruby
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-ruby.git
    GIT_TAG v0.23.0
)
FetchContent_GetProperties(tree_sitter_ruby)
if(NOT tree_sitter_ruby_POPULATED)
    FetchContent_Populate(tree_sitter_ruby)
    add_library(tree-sitter-ruby STATIC 
        ${tree_sitter_ruby_SOURCE_DIR}/src/parser.c
        ${tree_sitter_ruby_SOURCE_DIR}/src/scanner.c
    )
    target_include_directories(tree-sitter-ruby PUBLIC ${tree_sitter_ruby_SOURCE_DIR}/src)
    target_link_libraries(tree-sitter-ruby PRIVATE tree-sitter)
    set_target_properties(tree-sitter-ruby PROPERTIES LINKER_LANGUAGE C)
endif()

# Libraries
find_package(Threads REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(CURL REQUIRED)

add_library(kestr_crypto src/engine/sha256.cpp)
add_library(kestr_ignore src/engine/ignore.cpp)
add_library(kestr_db src/engine/database.cpp)
add_library(kestr_embed src/engine/embedder.cpp src/engine/embedder_ollama.cpp src/engine/embedder_onnx.cpp src/engine/embedder_openai.cpp src/engine/embedder_dummy.cpp)
add_library(kestr_scanner src/engine/scanner.cpp src/engine/text_chunker.cpp src/engine/treesitter_parser.cpp)
add_library(kestr_librarian src/engine/librarian.cpp)

target_link_libraries(kestr_db PUBLIC SQLite::SQLite3)
target_link_libraries(kestr_embed PRIVATE CURL::libcurl)
if(TARGET onnxruntime)
    target_link_libraries(kestr_embed PRIVATE onnxruntime)
    target_compile_definitions(kestr_embed PRIVATE KESTR_WITH_ONNX)
endif()
target_link_libraries(kestr_scanner PUBLIC 
    kestr_ignore 
    kestr_crypto 
    tree-sitter 
    tree-sitter-python
    tree-sitter-cpp
    tree-sitter-go
    tree-sitter-rust
    tree-sitter-javascript
    tree-sitter-java
    tree-sitter-c-sharp
    tree-sitter-php
    tree-sitter-ruby
)

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
    tree-sitter
    tree-sitter-python
    tree-sitter-cpp
    tree-sitter-go
    tree-sitter-rust
    tree-sitter-javascript
    tree-sitter-java
    tree-sitter-c-sharp
    tree-sitter-php
    tree-sitter-ruby
    Threads::Threads
    ${KESTR_PLATFORM_LIBS}
    )

    # Client Executable
    add_executable(kestr 
    src/client/main.cpp 
    src/platform.hpp
    ${KESTR_OS_SRC}
    )

    target_link_libraries(kestr PRIVATE 
    Threads::Threads
    ${KESTR_PLATFORM_LIBS}
    )

    # MCP Server Executable
    add_executable(kestr-mcp
    src/mcp/main.cpp
    src/platform.hpp
    ${KESTR_OS_SRC}
    )

    target_link_libraries(kestr-mcp PRIVATE 
    Threads::Threads
    ${KESTR_PLATFORM_LIBS}
    )


enable_testing()

# Hybrid Database Test
add_executable(test_database_hybrid tests/test_database_hybrid.cpp src/engine/database.cpp)
target_include_directories(test_database_hybrid PRIVATE src include)
target_link_libraries(test_database_hybrid PRIVATE SQLite::SQLite3)
add_test(NAME DatabaseHybrid COMMAND test_database_hybrid)

# TextChunker Unit Test
add_executable(test_text_chunker tests/test_text_chunker.cpp)
target_include_directories(test_text_chunker PRIVATE src include)
target_link_libraries(test_text_chunker PRIVATE kestr_scanner)
add_test(NAME TextChunkerUnit COMMAND test_text_chunker)

# Tree-sitter Python Unit Test
add_executable(test_treesitter_python tests/test_treesitter_python.cpp)
target_link_libraries(test_treesitter_python PRIVATE tree-sitter tree-sitter-python)
add_test(NAME TreeSitterPythonUnit COMMAND test_treesitter_python)

# TreeSitterParser Unit Test
add_executable(test_treesitter_parser tests/test_treesitter_parser.cpp)
target_include_directories(test_treesitter_parser PRIVATE src include)
target_link_libraries(test_treesitter_parser PRIVATE kestr_scanner tree-sitter tree-sitter-python)
add_test(NAME TreeSitterParserUnit COMMAND test_treesitter_parser)

# MCP Test
add_test(NAME MCPIntegration COMMAND python3 ${CMAKE_SOURCE_DIR}/tests/test_mcp.py)
set_tests_properties(MCPIntegration PROPERTIES ENVIRONMENT "CMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}")

# E2E Test
add_test(NAME E2EIntegration COMMAND python3 ${CMAKE_SOURCE_DIR}/tests/test_e2e.py)
set_tests_properties(E2EIntegration PROPERTIES ENVIRONMENT "CMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}")
