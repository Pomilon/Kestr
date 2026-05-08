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
         * @brief Checks if a file might need re-indexing based on metadata only.
         * @return true if mtime or size differs from DB.
         */
        bool check_metadata(const std::filesystem::path& path, std::uintmax_t size, int64_t mtime);

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
         * @brief Inserts multiple chunks into the database and returns their IDs.
         */
        std::vector<int64_t> insert_chunks(const std::filesystem::path& file_path, const std::vector<Chunk>& chunks, const std::vector<std::vector<float>>& embeddings);

        /**
         * @brief Starts an explicit transaction.
         */
        void begin_transaction();

        /**
         * @brief Commits an explicit transaction.
         */
        void commit_transaction();

        /**
         * @brief Searches for chunks containing the given keyword, with optional filters.
         * Returns a pair of (chunk_id, chunk_data).
         */
        std::vector<std::pair<int64_t, Chunk>> query(const std::string& text, int limit = 5, const SearchFilters& filters = {});

        /**
         * @brief Searches for chunks containing the given keyword.
         * Returns a pair of (chunk_id, chunk_data).
         */
        std::vector<std::pair<int64_t, Chunk>> search_keywords(const std::string& query, int limit = 5);

        /**
         * @brief Retrieves a chunk by ID.
         */
        Chunk get_chunk(int64_t id);

        /**
         * @brief Callback for iterating all vectors.
         * Function signature: (id, vector)
         */
        void for_each_vector(std::function<void(int64_t, const std::vector<float>&)> callback);

        /**
         * @brief Retrieves all indexed file paths.
         */
        std::vector<std::string> get_all_files();

        /**
         * @brief Get statistics.
         */
        size_t count_files();
        size_t count_chunks();

        /**
         * @brief Returns the dimension of existing embeddings in the database, or 0 if empty.
         */
        size_t get_stored_dimension();

        /**
         * @brief Wipes all chunks and resets is_indexed status for all files.
         * Used for re-indexing when the model changes.
         */
        void wipe_all_chunks();

        /**
         * @brief Access the internal sqlite3 handle.
         */
        sqlite3* get_internal_db() { return m_db; }

        /**
         * @brief Adds a link between a chunk and a symbol name.
         */
        bool add_symbol_link(int64_t from_chunk_id, const std::string& to_symbol, const std::string& type);

        /**
         * @brief Finds all chunks that link to a specific symbol name.
         */
        std::vector<Chunk> find_references(const std::string& symbol_name);

    private:
        sqlite3* m_db = nullptr;
    };

}