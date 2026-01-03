#include "embedder.hpp"
#include <sstream>
#include <vector>

namespace kestr::engine {

    std::vector<Chunk> Chunker::chunk_file(const std::string& content, size_t chunk_size, size_t overlap) {
        std::vector<Chunk> chunks;
        std::vector<std::string> lines;
        std::string line;
        std::istringstream stream(content);

        while (std::getline(stream, line)) {
            lines.push_back(line);
        }

        if (lines.empty()) return chunks;

        // Simple line-based chunking for now
        // A more advanced version would use token counts
        size_t start = 0;
        while (start < lines.size()) {
            size_t end = std::min(start + chunk_size, lines.size());
            
            std::string chunk_text;
            for (size_t i = start; i < end; ++i) {
                chunk_text += lines[i] + "\n";
            }

            chunks.push_back({chunk_text, (int)start + 1, (int)end});

            if (end == lines.size()) break;
            start += (chunk_size - overlap);
        }

        return chunks;
    }

}