#pragma once

#include <string>
#include <vector>
#include <memory>
#include <tree_sitter/api.h>
#include "kestr/types.hpp"

namespace kestr::engine {

    /**
     * @brief TreeSitterParser uses Tree-sitter to extract high-fidelity chunks from source code.
     */
    class TreeSitterParser {
    public:
        TreeSitterParser();
        ~TreeSitterParser();

        /**
         * @brief Parses Python source code and extracts function and class chunks.
         * @param content The raw source code.
         * @param language_name The language name (currently only "python" supported).
         * @return A vector of Chunk objects.
         */
        std::vector<Chunk> parse(const std::string& content, const std::string& language_name);

    private:
        TSParser* m_parser;
    };

}
