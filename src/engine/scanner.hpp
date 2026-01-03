#pragma once

#include <filesystem>
#include <string>
#include <vector>
#include <functional>
#include "kestr/types.hpp"
#include "ignore.hpp"

namespace kestr::engine {

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
