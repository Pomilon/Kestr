#pragma once

#include <string>
#include <vector>
#include "kestr/types.hpp"

namespace kestr::engine {

    /**
     * @brief TextChunker provides recursive splitting of text into chunks.
     * Splitting hierarchy: Double newlines (\n\n), single newlines (\n), spaces.
     */
    class TextChunker {
    public:
        /**
         * @brief Chunks a raw string using a recursive splitting strategy.
         * @param content The raw text to chunk.
         * @param target_size Target chunk size in characters (~4000).
         * @param overlap_percent Overlap between sequential chunks (default 15%).
         * @return A vector of Chunk objects.
         */
        static std::vector<Chunk> chunk(const std::string& content, 
                                        size_t target_size = 4000, 
                                        float overlap_percent = 0.15f);

    private:
        struct SplitResult {
            std::string text;
            int start_line;
            int end_line;
        };

        static std::vector<SplitResult> recursive_split(const std::string& text, 
                                                        int start_line, 
                                                        size_t target_size, 
                                                        const std::vector<std::string>& separators, 
                                                        size_t sep_idx);
    };

}
