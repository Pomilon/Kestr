#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <sqlite3.h>
#include <functional>
#include "kestr/types.hpp"

namespace kestr::engine {

    class Database {
    public:
        Database();
        ~Database();

        bool open(const std::filesystem::path& path);
        void close();

        /**
         * @brief Initializes the schema if it doesn't exist.
         */
        bool initialize_schema();

        /**
         * @brief Updates or inserts file metadata.
         */
        bool update_file(const FileInfo& info);

        /**
         * @brief Checks if a file needs re-indexing based on its hash.
         * @return true if the file hash in DB is different from current hash.
         */
        bool needs_indexing(const std::filesystem::path& path, const std::string& current_hash);

        /**
         * @brief Marks a file as indexed.
         */
        bool set_indexed_status(const std::filesystem::path& path, bool indexed);

        /**
         * @brief Removes a file and its chunks from the database.
         */
        bool remove_file(const std::filesystem::path& path);

        /**
         * @brief Inserts a chunk into the database.
         */
        bool insert_chunk(const std::filesystem::path& file_path, const Chunk& chunk, const std::vector<float>& embedding);

        /**
         * @brief Searches for chunks containing the given keyword.
         */
        std::vector<Chunk> search_keywords(const std::string& query, int limit = 5);

        /**
         * @brief Retrieves a chunk by ID.
         */
        Chunk get_chunk(int64_t id);

        /**
         * @brief Callback for iterating all vectors.
         * Function signature: (id, vector)
         */
        void for_each_vector(std::function<void(int64_t, const std::vector<float>&)> callback);

    private:
        sqlite3* m_db = nullptr;
    };

}