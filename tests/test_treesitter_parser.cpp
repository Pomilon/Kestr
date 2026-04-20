#include <iostream>
#include <cassert>
#include "engine/treesitter_parser.hpp"

using namespace kestr::engine;

void test_python_parsing() {
    std::cout << "Testing TreeSitterParser for Python..." << std::endl;
    TreeSitterParser parser;

    std::string code = 
        "class MyClass:\n"
        "    def my_method(self):\n"
        "        pass\n"
        "\n"
        "def my_function(a, b):\n"
        "    return a + b\n";

    auto chunks = parser.parse(code, "python");
    assert(chunks.size() == 3);

    bool found_class = false;
    for (const auto& chunk : chunks) {
        if (chunk.symbol_name == "MyClass") {
            assert(chunk.symbol_type == "class");
            found_class = true;
        }
    }
    assert(found_class);
    std::cout << "Python parsing test passed!" << std::endl;
}

void test_cpp_parsing() {
    std::cout << "Testing TreeSitterParser for C++..." << std::endl;
    TreeSitterParser parser;

    std::string code = 
        "class MyClass {\n"
        "public:\n"
        "    void my_method() {}\n"
        "};\n"
        "\n"
        "int my_function(int a) {\n"
        "    return a * 2;\n"
        "}\n";

    auto chunks = parser.parse(code, "cpp");
    // Class + Method + Function = 3 chunks
    assert(chunks.size() >= 3);
    
    bool found_func = false;
    for (const auto& chunk : chunks) {
        if (chunk.symbol_name == "my_function") {
            assert(chunk.symbol_type == "function");
            found_func = true;
        }
    }
    assert(found_func);
    std::cout << "C++ parsing test passed!" << std::endl;
}

void test_go_parsing() {
    std::cout << "Testing TreeSitterParser for Go..." << std::endl;
    TreeSitterParser parser;

    std::string code = 
        "type MyStruct struct {}\n"
        "\n"
        "func (m *MyStruct) MyMethod() {}\n"
        "\n"
        "func MyFunction() int {\n"
        "    return 42\n"
        "}\n";

    auto chunks = parser.parse(code, "go");
    assert(chunks.size() >= 3);
    
    bool found_struct = false;
    for (const auto& chunk : chunks) {
        if (chunk.symbol_name == "MyStruct") {
            assert(chunk.symbol_type == "class"); // We mapped struct to class
            found_struct = true;
        }
    }
    assert(found_struct);
    std::cout << "Go parsing test passed!" << std::endl;
}

void test_rust_parsing() {
    std::cout << "Testing TreeSitterParser for Rust..." << std::endl;
    TreeSitterParser parser;

    std::string code = 
        "struct MyStruct {}\n"
        "\n"
        "impl MyStruct {\n"
        "    fn my_method(&self) {}\n"
        "}\n"
        "\n"
        "fn my_function() {}\n";

    auto chunks = parser.parse(code, "rust");
    assert(chunks.size() >= 3);
    
    bool found_func = false;
    for (const auto& chunk : chunks) {
        if (chunk.symbol_name == "my_function") {
            assert(chunk.symbol_type == "function");
            found_func = true;
        }
    }
    assert(found_func);
    std::cout << "Rust parsing test passed!" << std::endl;
}

void test_js_parsing() {
    std::cout << "Testing TreeSitterParser for JavaScript..." << std::endl;
    TreeSitterParser parser;

    std::string code = 
        "class MyClass {\n"
        "    myMethod() {}\n"
        "}\n"
        "\n"
        "function myFunction() {}\n";

    auto chunks = parser.parse(code, "javascript");
    assert(chunks.size() >= 3);
    
    bool found_class = false;
    for (const auto& chunk : chunks) {
        if (chunk.symbol_name == "MyClass") {
            assert(chunk.symbol_type == "class");
            found_class = true;
        }
    }
    assert(found_class);
    std::cout << "JavaScript parsing test passed!" << std::endl;
}

void test_unsupported_language() {
    std::cout << "Testing unsupported language..." << std::endl;
    TreeSitterParser parser;
    auto chunks = parser.parse("int main() {}", "cobol");
    assert(chunks.empty());
    std::cout << "Unsupported language test passed!" << std::endl;
}

int main() {
    try {
        test_python_parsing();
        test_cpp_parsing();
        test_go_parsing();
        test_rust_parsing();
        test_js_parsing();
        test_unsupported_language();
        std::cout << "All TreeSitterParser tests passed!" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Test failed: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
