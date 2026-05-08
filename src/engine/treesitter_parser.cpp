#include "treesitter_parser.hpp"
#include <iostream>
#include <cstring>
#include <map>

extern "C" const TSLanguage *tree_sitter_python();
extern "C" const TSLanguage *tree_sitter_cpp();
extern "C" const TSLanguage *tree_sitter_go();
extern "C" const TSLanguage *tree_sitter_rust();
extern "C" const TSLanguage *tree_sitter_javascript();
extern "C" const TSLanguage *tree_sitter_java();
extern "C" const TSLanguage *tree_sitter_c_sharp();
extern "C" const TSLanguage *tree_sitter_php();
extern "C" const TSLanguage *tree_sitter_ruby();

namespace kestr::engine {

    TreeSitterParser::TreeSitterParser() {
        m_parser = ts_parser_new();
    }

    TreeSitterParser::~TreeSitterParser() {
        if (m_parser) ts_parser_delete(m_parser);
    }

    std::vector<Chunk> TreeSitterParser::parse(const std::string& content, const std::string& language_name) {
        std::vector<Chunk> chunks;
        const TSLanguage* lang = nullptr;
        std::string query_source;

        if (language_name == "python") {
            lang = tree_sitter_python();
            query_source = 
                "(class_definition name: (identifier) @symbol.name) @symbol.body "
                "(function_definition name: (identifier) @symbol.name) @symbol.body";
        } else if (language_name == "cpp") {
            lang = tree_sitter_cpp();
            query_source = 
                "(class_specifier name: (type_identifier) @symbol.name) @symbol.body "
                "(function_definition declarator: (function_declarator declarator: (field_identifier) @symbol.name)) @symbol.body "
                "(function_definition declarator: (function_declarator declarator: (identifier) @symbol.name)) @symbol.body";
        } else if (language_name == "go") {
            lang = tree_sitter_go();
            query_source = 
                "(type_declaration (type_spec name: (type_identifier) @symbol.name)) @symbol.body "
                "(function_declaration name: (identifier) @symbol.name) @symbol.body "
                "(method_declaration name: (field_identifier) @symbol.name) @symbol.body";
        } else if (language_name == "rust") {
            lang = tree_sitter_rust();
            query_source = 
                "(struct_item name: (type_identifier) @symbol.name) @symbol.body "
                "(enum_item name: (type_identifier) @symbol.name) @symbol.body "
                "(function_item name: (identifier) @symbol.name) @symbol.body "
                "(impl_item) @symbol.body";
        } else if (language_name == "javascript" || language_name == "typescript") {
            lang = tree_sitter_javascript(); // Use JS for now, TS is similar
            query_source = 
                "(class_declaration name: (identifier) @symbol.name) @symbol.body "
                "(function_declaration name: (identifier) @symbol.name) @symbol.body "
                "(method_definition name: (property_identifier) @symbol.name) @symbol.body";
        } else if (language_name == "java") {
            lang = tree_sitter_java();
            query_source = 
                "(class_declaration name: (identifier) @symbol.name) @symbol.body "
                "(method_declaration name: (identifier) @symbol.name) @symbol.body";
        } else if (language_name == "c_sharp") {
            lang = tree_sitter_c_sharp();
            query_source = 
                "(class_declaration name: (identifier) @symbol.name) @symbol.body "
                "(method_declaration name: (identifier) @symbol.name) @symbol.body";
        } else if (language_name == "php") {
            lang = tree_sitter_php();
            query_source = 
                "(class_declaration name: (identifier) @symbol.name) @symbol.body "
                "(function_definition name: (identifier) @symbol.name) @symbol.body "
                "(method_definition name: (identifier) @symbol.name) @symbol.body";
        } else if (language_name == "ruby") {
            lang = tree_sitter_ruby();
            query_source = 
                "(class name: (identifier) @symbol.name) @symbol.body "
                "(method_definition name: (identifier) @symbol.name) @symbol.body "
                "(def name: (identifier) @symbol.name) @symbol.body";
        }


        if (!lang || query_source.empty()) return chunks;
        if (!ts_parser_set_language(m_parser, lang)) return chunks;

        TSTree* tree = ts_parser_parse_string(m_parser, nullptr, content.c_str(), content.length());
        if (!tree) return chunks;

        TSNode root_node = ts_tree_root_node(tree);

        uint32_t error_offset;
        TSQueryError error_type;
        TSQuery* query = ts_query_new(lang, query_source.c_str(), query_source.length(), &error_offset, &error_type);

        if (!query) {
            ts_tree_delete(tree);
            return chunks;
        }

        TSQueryCursor* cursor = ts_query_cursor_new();
        ts_query_cursor_exec(cursor, query, root_node);

        TSQueryMatch match;
        while (ts_query_cursor_next_match(cursor, &match)) {
            Chunk chunk;
            chunk.language = language_name;
            
            TSNode body_node;
            bool found_body = false;

            for (uint32_t i = 0; i < match.capture_count; ++i) {
                TSQueryCapture capture = match.captures[i];
                uint32_t capture_name_len;
                const char* capture_name = ts_query_capture_name_for_id(query, capture.index, &capture_name_len);
                std::string name(capture_name, capture_name_len);

                if (name == "symbol.body") {
                    body_node = capture.node;
                    found_body = true;
                    
                    const char* node_type = ts_node_type(body_node);
                    if (strstr(node_type, "class") || strstr(node_type, "struct") || strstr(node_type, "type_declaration")) {
                        chunk.symbol_type = "class";
                    } else {
                        chunk.symbol_type = "function";
                    }

                    uint32_t start_byte = ts_node_start_byte(body_node);
                    uint32_t end_byte = ts_node_end_byte(body_node);
                    chunk.content = content.substr(start_byte, end_byte - start_byte);
                    
                    TSPoint start_point = ts_node_start_point(body_node);
                    TSPoint end_point = ts_node_end_point(body_node);
                    chunk.start_line = start_point.row + 1;
                    chunk.end_line = end_point.row + 1;
                } else if (name == "symbol.name") {
                    uint32_t start_byte = ts_node_start_byte(capture.node);
                    uint32_t end_byte = ts_node_end_byte(capture.node);
                    chunk.symbol_name = content.substr(start_byte, end_byte - start_byte);
                }
            }

            if (found_body) {
                chunks.push_back(chunk);
            }
        }

        ts_query_cursor_delete(cursor);
        ts_query_delete(query);
        ts_tree_delete(tree);

        return chunks;
    }

    std::vector<std::pair<uint32_t, std::string>> TreeSitterParser::extract_calls(const std::string& content, const std::string& language_name) {
        std::vector<std::pair<uint32_t, std::string>> calls;
        const TSLanguage* lang = nullptr;
        std::string query_source;

        if (language_name == "python") {
            lang = tree_sitter_python();
            query_source = "(call function: (identifier) @call.name)";
        } else if (language_name == "cpp") {
            lang = tree_sitter_cpp();
            query_source = "(call_expression function: (identifier) @call.name)";
        } else if (language_name == "go") {
            lang = tree_sitter_go();
            query_source = "(call_expression function: (identifier) @call.name)";
        } else if (language_name == "rust") {
            lang = tree_sitter_rust();
            query_source = "(call_expression function: (identifier) @call.name)";
        } else if (language_name == "javascript" || language_name == "typescript") {
            lang = tree_sitter_javascript();
            query_source = "(call_expression function: (identifier) @call.name)";
        } else if (language_name == "java") {
            lang = tree_sitter_java();
            query_source = "(method_invocation function: (identifier) @call.name)";
        } else if (language_name == "c_sharp") {
            lang = tree_sitter_c_sharp();
            query_source = "(invocation_expression function: (identifier) @call.name)";
        } else if (language_name == "php") {
            lang = tree_sitter_php();
            query_source = "(function_call function: (identifier) @call.name)";
        } else if (language_name == "ruby") {
            lang = tree_sitter_ruby();
            query_source = "(call method: (identifier) @call.name)";
        }

        if (!lang || query_source.empty()) return calls;
        if (!ts_parser_set_language(m_parser, lang)) return calls;

        TSTree* tree = ts_parser_parse_string(m_parser, nullptr, content.c_str(), content.length());
        if (!tree) return calls;

        TSNode root_node = ts_tree_root_node(tree);
        uint32_t error_offset;
        TSQueryError error_type;
        TSQuery* query = ts_query_new(lang, query_source.c_str(), query_source.length(), &error_offset, &error_type);

        if (query) {
            TSQueryCursor* cursor = ts_query_cursor_new();
            ts_query_cursor_exec(cursor, query, root_node);

            TSQueryMatch match;
            while (ts_query_cursor_next_match(cursor, &match)) {
                for (uint32_t i = 0; i < match.capture_count; ++i) {
                    TSQueryCapture capture = match.captures[i];
                    uint32_t capture_name_len;
                    const char* capture_name = ts_query_capture_name_for_id(query, capture.index, &capture_name_len);
                    if (std::string(capture_name, capture_name_len) == "call.name") {
                        uint32_t start_byte = ts_node_start_byte(capture.node);
                        uint32_t end_byte = ts_node_end_byte(capture.node);
                        calls.push_back({start_byte, content.substr(start_byte, end_byte - start_byte)});
                    }
                }
            }
            ts_query_cursor_delete(cursor);
            ts_query_delete(query);
        }
        ts_tree_delete(tree);
        return calls;
    }

}
