#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <regex>

namespace kestr::engine {

    class Ignore {
    public:
        /**
         * @brief Loads patterns from a .kestr_ignore file.
         * @param ignore_file Path to the ignore file.
         */
        void load(const std::filesystem::path& ignore_file);

        /**
         * @brief Checks if a path should be ignored.
         * @param path The path to check.
         * @return true if the path matches an ignore pattern.
         */
        bool check(const std::filesystem::path& path) const;

        /**
         * @brief Adds a default set of ignores (e.g. .git, build, etc.)
         */
        void add_defaults();

    private:
        struct Pattern {
            std::regex regex;
            std::string original;
        };
        std::vector<Pattern> m_patterns;

        std::string glob_to_regex(const std::string& glob);
    };

}
