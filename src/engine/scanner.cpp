#include "scanner.hpp"
#include "kestr/sha256.h"
#include <iostream>

namespace kestr::engine {

    Scanner::Scanner() {
        m_ignore.add_defaults();
        // Try to load local ignore if present in current dir (or pass it in)
        m_ignore.load(".kestr_ignore");
    }

    void Scanner::scan(const std::filesystem::path& root, FileCallback callback) {
        if (!std::filesystem::exists(root) || !std::filesystem::is_directory(root)) {
            std::cerr << "[Scanner] Invalid root path: " << root << "\n";
            return;
        }

        for (auto it = std::filesystem::recursive_directory_iterator(root, std::filesystem::directory_options::skip_permission_denied);
             it != std::filesystem::recursive_directory_iterator();
             ++it) {
            
            const auto& path = it->path();
            
            if (m_ignore.check(path)) {
                if (it->is_directory()) {
                    it.disable_recursion_pending();
                }
                continue;
            }

            if (it->is_regular_file()) {
                FileInfo info;
                info.path = path;
                info.size = it->file_size();
                info.last_write_time = it->last_write_time();
                info.hash = hash_file(info.path);

                if (callback) {
                    callback(info);
                }
            }
        }
    }

    std::string Scanner::hash_file(const std::filesystem::path& path) {
        return kestr::crypto::SHA256::hash_file(path.string());
    }

}
