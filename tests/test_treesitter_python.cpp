#include <iostream>
#include <cassert>
#include <cstring>
#include <tree_sitter/api.h>

extern "C" const TSLanguage *tree_sitter_python();

int main() {
    std::cout << "Testing Tree-sitter Python integration..." << std::endl;

    // Create a parser
    TSParser *parser = ts_parser_new();

    // Set the language (Python)
    const TSLanguage *python = tree_sitter_python();
    assert(python != nullptr);
    bool success = ts_parser_set_language(parser, python);
    assert(success);

    // Parse a simple Python string
    const char *source_code = "def hello():\n  print('Hello, World!')\n";
    TSTree *tree = ts_parser_parse_string(
        parser,
        nullptr,
        source_code,
        (uint32_t)strlen(source_code)
    );

    assert(tree != nullptr);

    // Get the root node
    TSNode root_node = ts_tree_root_node(tree);
    const char *type = ts_node_type(root_node);
    std::cout << "Root node type: " << type << std::endl;
    assert(std::string(type) == "module");

    // Cleanup
    ts_tree_delete(tree);
    ts_parser_delete(parser);

    std::cout << "Tree-sitter Python test passed!" << std::endl;
    return 0;
}
