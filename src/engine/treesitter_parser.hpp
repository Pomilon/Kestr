#pragma once

#include <string>
#include <vector>
#include <memory>
#include <tree_sitter/api.h>
#include "kestr/types.hpp"

namespace kestr::engine {

    class TreeSitterParser {
    public:
        TreeSitterParser();
        ~TreeSitterParser();

        /**
         * @brief Parses the file and extracts structural chunks (classes/functions).
         */
        std::vector<Chunk> parse(const std::string& content, const std::string& language_name);

        /**
         * @brief Extracts call sites and the symbols they refer to.
         */
        std::vector<std::pair<uint32_t, std::string>> extract_calls(const std::string& content, const std::string& language_name);

    private:
        TSParser* m_parser;
    };

}
