#pragma once

#include <filesystem>
#include <string>
#include <vector>
#include <functional>
#include "ignore.hpp"

namespace kestr::engine {

    struct FileInfo {
        std::filesystem::path path;
        std::uintmax_t size;
        std::filesystem::file_time_type last_write_time;
        std::string hash;
    };

    class Scanner {
    public:
        using FileCallback = std::function<void(const FileInfo&)>;

        Scanner();

        /**
         * @brief Scans a directory recursively.
         * @param root The root directory to scan.
         * @param callback Called for every valid file found.
         */
        void scan(const std::filesystem::path& root, FileCallback callback);

        /**
         * @brief Computes the hash of a specific file.
         */
        std::string hash_file(const std::filesystem::path& path);

    private:
        Ignore m_ignore;
    };

}
