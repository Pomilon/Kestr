#include "ignore.hpp"
#include <fstream>
#include <iostream>

namespace kestr::engine {

    void Ignore::load(const std::filesystem::path& ignore_file) {
        if (!std::filesystem::exists(ignore_file)) return;

        std::ifstream file(ignore_file);
        std::string line;
        while (std::getline(file, line)) {
            // Trim whitespace
            line.erase(0, line.find_first_not_of(" 	"));
            line.erase(line.find_last_not_of(" 	") + 1);

            if (line.empty() || line[0] == '#') continue;

            m_patterns.push_back({std::regex(glob_to_regex(line)), line});
        }
    }

    void Ignore::add_defaults() {
        std::vector<std::string> defaults = {
            ".git", ".svn", ".hg",
            "build", "dist", "node_modules",
            "*.o", "*.obj", "*.exe", "*.dll", "*.so", "*.dylib",
            ".DS_Store", "Thumbs.db",
            "kestr.db", "kestr.db-journal", "kestrd.log", "config.json"
        };
        for (const auto& p : defaults) {
            m_patterns.push_back({std::regex(glob_to_regex(p)), p});
        }
    }

    bool Ignore::check(const std::filesystem::path& path) const {
        // Check filename first (common case)
        std::string filename = path.filename().string();
        for (const auto& p : m_patterns) {
            if (std::regex_match(filename, p.regex)) return true;
        }
        
        return false;
    }

    std::string Ignore::glob_to_regex(const std::string& glob) {
        std::string regex_str = "^";
        for (char c : glob) {
            if (c == '*') {
                regex_str += ".*";
            } else if (c == '?') {
                regex_str += ".";
            } else if (c == '.') {
                regex_str += "\\.";
            } else if (c == '/') {
                regex_str += "[/\\\\]"; // Match both separators
            } else {
                regex_str += c;
            }
        }
        regex_str += "$";
        return regex_str;
    }

}
