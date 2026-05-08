#include "database.hpp"
#include <iostream>
#include <chrono>
#include <cstring>

namespace kestr::engine {

    Database::Database() = default;
    Database::~Database() { close(); }

    bool Database::open(const std::filesystem::path& path) {
        if (sqlite3_open(path.string().c_str(), &m_db) != SQLITE_OK) {
            std::cerr << "[Database] Failed to open: " << sqlite3_errmsg(m_db) << "\n";
            return false;
        }
        return initialize_schema();
    }

    void Database::close() {
        if (m_db) {
            sqlite3_close(m_db);
            m_db = nullptr;
        }
    }

    bool Database::initialize_schema() {
        // Enable WAL mode and optimize for performance
        sqlite3_exec(m_db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
        sqlite3_exec(m_db, "PRAGMA synchronous=NORMAL;", nullptr, nullptr, nullptr);
        sqlite3_exec(m_db, "PRAGMA cache_size=-64000;", nullptr, nullptr, nullptr); // 64MB cache

        const char* sql = 
            "CREATE TABLE IF NOT EXISTS files ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT," 
            "  path TEXT UNIQUE NOT NULL," 
            "  hash TEXT NOT NULL," 
            "  last_modified INTEGER," 
            "  size INTEGER," 
            "  is_indexed INTEGER DEFAULT 0,"
            "  project_root TEXT"
            ");"
            "CREATE INDEX IF NOT EXISTS idx_path ON files(path);"
            "CREATE TABLE IF NOT EXISTS chunks ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  file_id INTEGER,"
            "  content TEXT,"
            "  start_line INTEGER,"
            "  end_line INTEGER,"
            "  symbol_name TEXT,"
            "  symbol_type TEXT,"
            "  project_root TEXT,"
            "  language TEXT,"
            "  embedding BLOB,"
            "  FOREIGN KEY(file_id) REFERENCES files(id) ON DELETE CASCADE"
            ");"
            "CREATE TABLE IF NOT EXISTS symbol_links ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  from_chunk_id INTEGER,"
            "  to_symbol_name TEXT,"
            "  link_type TEXT,"
            "  FOREIGN KEY(from_chunk_id) REFERENCES chunks(id) ON DELETE CASCADE"
            ");"
            "CREATE VIRTUAL TABLE IF NOT EXISTS chunks_fts USING fts5(content);";
        char* err_msg = nullptr;
        if (sqlite3_exec(m_db, sql, nullptr, nullptr, &err_msg) != SQLITE_OK) {
            std::cerr << "[Database] Schema error: " << err_msg << "\n";
            sqlite3_free(err_msg);
            return false;
        }

        // Migration for existing databases
        const char* migration_sql[] = {
            "ALTER TABLE chunks ADD COLUMN symbol_name TEXT;",
            "ALTER TABLE chunks ADD COLUMN symbol_type TEXT;",
            "ALTER TABLE chunks ADD COLUMN project_root TEXT;",
            "ALTER TABLE chunks ADD COLUMN language TEXT;",
            "ALTER TABLE files ADD COLUMN project_root TEXT;"
        };

        for (const char* m_sql : migration_sql) {
            sqlite3_exec(m_db, m_sql, nullptr, nullptr, nullptr); 
            // We ignore errors here because the columns might already exist
        }

        return true;
    }

    bool Database::check_metadata(const std::filesystem::path& path, std::uintmax_t size, int64_t mtime) {
        const char* sql = "SELECT size, last_modified FROM files WHERE path = ?;";
        sqlite3_stmt* stmt;
        bool changed = true;

        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, path.string().c_str(), -1, SQLITE_STATIC);
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                std::uintmax_t db_size = sqlite3_column_int64(stmt, 0);
                int64_t db_mtime = sqlite3_column_int64(stmt, 1);
                changed = (db_size != size || db_mtime != mtime);
            }
            sqlite3_finalize(stmt);
        }
        return changed;
    }

    bool Database::needs_indexing(const std::filesystem::path& path, const std::string& current_hash) {
        const char* sql = "SELECT hash FROM files WHERE path = ?;";
        sqlite3_stmt* stmt;
        bool needs = true;

        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, path.string().c_str(), -1, SQLITE_STATIC);
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                const unsigned char* text = sqlite3_column_text(stmt, 0);
                if (text) {
                    std::string db_hash = reinterpret_cast<const char*>(text);
                    needs = (db_hash != current_hash);
                }
            }
            sqlite3_finalize(stmt);
        }
        return needs;
    }

    bool Database::update_file(const FileInfo& info) {
        const char* sql = 
            "INSERT INTO files (path, hash, last_modified, size, is_indexed, project_root) "
            "VALUES (?, ?, ?, ?, 0, ?) "
            "ON CONFLICT(path) DO UPDATE SET "
            "hash = excluded.hash, "
            "last_modified = excluded.last_modified, "
            "size = excluded.size, "
            "is_indexed = 0, "
            "project_root = excluded.project_root;";

        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

        sqlite3_bind_text(stmt, 1, info.path.string().c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, info.hash.c_str(), -1, SQLITE_STATIC);
        
        auto duration = info.last_write_time.time_since_epoch();
        auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
        sqlite3_bind_int64(stmt, 3, millis);
        sqlite3_bind_int64(stmt, 4, info.size);
        sqlite3_bind_text(stmt, 5, info.project_root.c_str(), -1, SQLITE_STATIC);

        bool success = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
        return success;
    }

    bool Database::set_indexed_status(const std::filesystem::path& path, bool indexed) {
        const char* sql = "UPDATE files SET is_indexed = ? WHERE path = ?;";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

        sqlite3_bind_int(stmt, 1, indexed ? 1 : 0);
        sqlite3_bind_text(stmt, 2, path.string().c_str(), -1, SQLITE_STATIC);

        bool success = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
        return success;
    }

    bool Database::remove_file(const std::filesystem::path& path) {
        // First, remove from FTS table to maintain consistency
        const char* fts_cleanup_sql = 
            "DELETE FROM chunks_fts WHERE rowid IN ("
            "  SELECT c.id FROM chunks c "
            "  JOIN files f ON c.file_id = f.id "
            "  WHERE f.path = ?"
            ");";
        sqlite3_stmt* fts_stmt;
        if (sqlite3_prepare_v2(m_db, fts_cleanup_sql, -1, &fts_stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(fts_stmt, 1, path.string().c_str(), -1, SQLITE_STATIC);
            sqlite3_step(fts_stmt);
            sqlite3_finalize(fts_stmt);
        }

        const char* sql = "DELETE FROM files WHERE path = ?;";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

        sqlite3_bind_text(stmt, 1, path.string().c_str(), -1, SQLITE_STATIC);

        bool success = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
        return success;
    }

    void Database::begin_transaction() {
        sqlite3_exec(m_db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
    }

    void Database::commit_transaction() {
        sqlite3_exec(m_db, "COMMIT;", nullptr, nullptr, nullptr);
    }

    std::vector<int64_t> Database::insert_chunks(const std::filesystem::path& file_path, const std::vector<Chunk>& chunks, const std::vector<std::vector<float>>& embeddings) {
        std::vector<int64_t> ids;
        const char* id_sql = "SELECT id FROM files WHERE path = ?;";
        sqlite3_stmt* id_stmt;
        int64_t file_id = -1;

        if (sqlite3_prepare_v2(m_db, id_sql, -1, &id_stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(id_stmt, 1, file_path.string().c_str(), -1, SQLITE_STATIC);
            if (sqlite3_step(id_stmt) == SQLITE_ROW) {
                file_id = sqlite3_column_int64(id_stmt, 0);
            }
            sqlite3_finalize(id_stmt);
        }

        if (file_id == -1) return ids;

        const char* sql = "INSERT INTO chunks (file_id, content, start_line, end_line, symbol_name, symbol_type, project_root, language, embedding) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return ids;

        for (size_t i = 0; i < chunks.size(); ++i) {
            const auto& chunk = chunks[i];
            const auto& embedding = (i < embeddings.size()) ? embeddings[i] : std::vector<float>();

            sqlite3_bind_int64(stmt, 1, file_id);
            sqlite3_bind_text(stmt, 2, chunk.content.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_int(stmt, 3, chunk.start_line);
            sqlite3_bind_int(stmt, 4, chunk.end_line);
            sqlite3_bind_text(stmt, 5, chunk.symbol_name.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 6, chunk.symbol_type.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 7, chunk.project_root.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 8, chunk.language.c_str(), -1, SQLITE_STATIC);
            
            if (!embedding.empty()) {
                sqlite3_bind_blob(stmt, 9, embedding.data(), embedding.size() * sizeof(float), SQLITE_STATIC);
            } else {
                sqlite3_bind_null(stmt, 9);
            }

            if (sqlite3_step(stmt) == SQLITE_DONE) {
                int64_t chunk_id = sqlite3_last_insert_rowid(m_db);
                ids.push_back(chunk_id);
                
                // Mirror to FTS table
                const char* fts_sql = "INSERT INTO chunks_fts(rowid, content) VALUES (?, ?);";
                sqlite3_stmt* fts_stmt;
                if (sqlite3_prepare_v2(m_db, fts_sql, -1, &fts_stmt, nullptr) == SQLITE_OK) {
                    sqlite3_bind_int64(fts_stmt, 1, chunk_id);
                    sqlite3_bind_text(fts_stmt, 2, chunk.content.c_str(), -1, SQLITE_STATIC);
                    sqlite3_step(fts_stmt);
                    sqlite3_finalize(fts_stmt);
                }
            }
            sqlite3_reset(stmt);
        }

        sqlite3_finalize(stmt);
        return ids;
    }

    bool Database::insert_chunk(const std::filesystem::path& file_path, const Chunk& chunk, const std::vector<float>& embedding) {
        return !insert_chunks(file_path, {chunk}, {embedding}).empty();
    }

    std::vector<std::pair<int64_t, Chunk>> Database::query(const std::string& text, int limit, const SearchFilters& filters) {
        std::vector<std::pair<int64_t, Chunk>> results;
        std::string sql = "SELECT f.rowid, c.content, c.start_line, c.end_line, c.symbol_name, c.symbol_type, c.project_root, c.language "
                          "FROM chunks c "
                          "JOIN chunks_fts f ON c.id = f.rowid "
                          "WHERE chunks_fts MATCH ?";
        
        if (!filters.type_filter.empty()) sql += " AND c.symbol_type = ?";
        if (!filters.language.empty()) sql += " AND c.language = ?";
        if (!filters.scope.empty()) sql += " AND c.project_root = ?";
        
        sql += " ORDER BY rank LIMIT ?;";

        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
            int bind_idx = 1;
            sqlite3_bind_text(stmt, bind_idx++, text.c_str(), -1, SQLITE_STATIC);
            
            if (!filters.type_filter.empty()) sqlite3_bind_text(stmt, bind_idx++, filters.type_filter.c_str(), -1, SQLITE_STATIC);
            if (!filters.language.empty()) sqlite3_bind_text(stmt, bind_idx++, filters.language.c_str(), -1, SQLITE_STATIC);
            if (!filters.scope.empty()) sqlite3_bind_text(stmt, bind_idx++, filters.scope.c_str(), -1, SQLITE_STATIC);
            
            sqlite3_bind_int(stmt, bind_idx++, limit);

            while (sqlite3_step(stmt) == SQLITE_ROW) {
                int64_t id = sqlite3_column_int64(stmt, 0);
                Chunk chunk;
                const char* content_ptr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
                if (content_ptr) chunk.content = content_ptr;
                chunk.start_line = sqlite3_column_int(stmt, 2);
                chunk.end_line = sqlite3_column_int(stmt, 3);
                if (const char* val = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4))) chunk.symbol_name = val;
                if (const char* val = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5))) chunk.symbol_type = val;
                if (const char* val = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6))) chunk.project_root = val;
                if (const char* val = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7))) chunk.language = val;
                results.push_back({id, chunk});
            }
            sqlite3_finalize(stmt);
        }
        return results;
    }

    std::vector<std::pair<int64_t, Chunk>> Database::search_keywords(const std::string& query_str, int limit) {
        return query(query_str, limit);
    }

    Chunk Database::get_chunk(int64_t id) {
        Chunk chunk;
        const char* sql = "SELECT content, start_line, end_line, symbol_name, symbol_type, project_root, language FROM chunks WHERE id = ?;";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int64(stmt, 1, id);
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                const char* content_ptr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                if (content_ptr) chunk.content = content_ptr;
                chunk.start_line = sqlite3_column_int(stmt, 1);
                chunk.end_line = sqlite3_column_int(stmt, 2);
                if (const char* val = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3))) chunk.symbol_name = val;
                if (const char* val = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4))) chunk.symbol_type = val;
                if (const char* val = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5))) chunk.project_root = val;
                if (const char* val = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6))) chunk.language = val;
            }
            sqlite3_finalize(stmt);
        }
        return chunk;
    }

    void Database::for_each_vector(std::function<void(int64_t, const std::vector<float>&)> callback) {
        const char* sql = 
            "SELECT c.id, c.embedding "
            "FROM chunks c "
            "JOIN files f ON c.file_id = f.id "
            "WHERE c.embedding IS NOT NULL "
            "ORDER BY f.last_modified DESC;";
            
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                int64_t id = sqlite3_column_int64(stmt, 0);
                const void* blob = sqlite3_column_blob(stmt, 1);
                int bytes = sqlite3_column_bytes(stmt, 1);
                
                if (blob && bytes > 0) {
                    size_t vec_count = bytes / sizeof(float);
                    std::vector<float> vec(vec_count);
                    memcpy(vec.data(), blob, bytes);
                    callback(id, vec);
                }
            }
            sqlite3_finalize(stmt);
        }
    }

    std::vector<std::string> Database::get_all_files() {
        std::vector<std::string> files;
        const char* sql = "SELECT path FROM files;";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                files.push_back(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
            }
            sqlite3_finalize(stmt);
        }
        return files;
    }

    size_t Database::count_files() {
        const char* sql = "SELECT count(*) FROM files;";
        sqlite3_stmt* stmt;
        size_t count = 0;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            if (sqlite3_step(stmt) == SQLITE_ROW) count = sqlite3_column_int(stmt, 0);
            sqlite3_finalize(stmt);
        }
        return count;
    }

    size_t Database::count_chunks() {
        const char* sql = "SELECT count(*) FROM chunks;";
        sqlite3_stmt* stmt;
        size_t count = 0;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            if (sqlite3_step(stmt) == SQLITE_ROW) count = sqlite3_column_int(stmt, 0);
            sqlite3_finalize(stmt);
        }
        return count;
    }

    size_t Database::get_stored_dimension() {
        const char* sql = "SELECT length(embedding) FROM chunks WHERE embedding IS NOT NULL LIMIT 1;";
        sqlite3_stmt* stmt;
        size_t dim = 0;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                int bytes = sqlite3_column_int(stmt, 0);
                dim = bytes / sizeof(float);
            }
            sqlite3_finalize(stmt);
        }
        return dim;
    }

    void Database::wipe_all_chunks() {
        sqlite3_exec(m_db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
        sqlite3_exec(m_db, "DELETE FROM chunks;", nullptr, nullptr, nullptr);
        sqlite3_exec(m_db, "DELETE FROM chunks_fts;", nullptr, nullptr, nullptr);
        sqlite3_exec(m_db, "DELETE FROM symbol_links;", nullptr, nullptr, nullptr);
        sqlite3_exec(m_db, "UPDATE files SET is_indexed = 0;", nullptr, nullptr, nullptr);
        sqlite3_exec(m_db, "COMMIT;", nullptr, nullptr, nullptr);
    }

    bool Database::add_symbol_link(int64_t from_chunk_id, const std::string& to_symbol, const std::string& type) {
        const char* sql = "INSERT INTO symbol_links (from_chunk_id, to_symbol_name, link_type) VALUES (?, ?, ?);";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
        sqlite3_bind_int64(stmt, 1, from_chunk_id);
        sqlite3_bind_text(stmt, 2, to_symbol.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, type.c_str(), -1, SQLITE_STATIC);
        bool success = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
        return success;
    }

    std::vector<Chunk> Database::find_references(const std::string& symbol_name) {
        std::vector<Chunk> results;
        const char* sql = "SELECT c.content, c.start_line, c.end_line, c.symbol_name, c.symbol_type, c.project_root, c.language "
                          "FROM chunks c "
                          "JOIN symbol_links l ON c.id = l.from_chunk_id "
                          "WHERE l.to_symbol_name = ?;";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, symbol_name.c_str(), -1, SQLITE_STATIC);
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                Chunk chunk;
                const char* content_ptr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                if (content_ptr) chunk.content = content_ptr;
                chunk.start_line = sqlite3_column_int(stmt, 1);
                chunk.end_line = sqlite3_column_int(stmt, 2);
                if (const char* val = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3))) chunk.symbol_name = val;
                if (const char* val = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4))) chunk.symbol_type = val;
                if (const char* val = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5))) chunk.project_root = val;
                if (const char* val = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6))) chunk.language = val;
                results.push_back(chunk);
            }
            sqlite3_finalize(stmt);
        }
        return results;
    }

} 
