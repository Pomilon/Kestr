#pragma once
#include <string>
#include <filesystem>

namespace kestr::engine {

    struct FileInfo {
        std::filesystem::path path;
        std::uintmax_t size;
        std::filesystem::file_time_type last_write_time;
        std::string hash;
    };

    struct Chunk {
        std::string content;
        int start_line;
        int end_line;
    };

}
