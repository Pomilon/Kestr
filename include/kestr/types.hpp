#pragma once
#include <string>
#include <filesystem>

namespace kestr::engine {

    struct FileInfo {
        std::filesystem::path path;
        std::uintmax_t size;
        std::filesystem::file_time_type last_write_time;
        std::string hash;
        std::string project_root;
    };

    struct SearchFilters {
        std::string type_filter;
        std::string language;
        std::string scope; // project_root
    };

    struct Chunk {
        std::string content;
        int start_line;
        int end_line;
        
        // Structural fields
        std::string symbol_name;
        std::string symbol_type;
        std::string project_root;
        std::string language;
    };

}
