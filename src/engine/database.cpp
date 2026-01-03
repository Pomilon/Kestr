#include "database.hpp"
#include <iostream>
#include <chrono>
#include <cstring>

namespace kestr::engine {

    Database::Database() = default;
    Database::~Database() { close(); }

    bool Database::open(const std::filesystem::path& path) {
        if (sqlite3_open(path.c_str(), &m_db) != SQLITE_OK) {
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
        const char* sql = 
            "CREATE TABLE IF NOT EXISTS files ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT," 
            "  path TEXT UNIQUE NOT NULL," 
            "  hash TEXT NOT NULL," 
            "  last_modified INTEGER," 
            "  size INTEGER," 
            "  is_indexed INTEGER DEFAULT 0"
            ");"
            "CREATE INDEX IF NOT EXISTS idx_path ON files(path);"
                        "CREATE TABLE IF NOT EXISTS chunks ("
                        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
                        "  file_id INTEGER,"
                        "  content TEXT,"
                        "  start_line INTEGER,"
                        "  end_line INTEGER,"
                        "  embedding BLOB,"
                        "  FOREIGN KEY(file_id) REFERENCES files(id) ON DELETE CASCADE"
                        ");";
                    char* err_msg = nullptr;
        if (sqlite3_exec(m_db, sql, nullptr, nullptr, &err_msg) != SQLITE_OK) {
            std::cerr << "[Database] Schema error: " << err_msg << "\n";
            sqlite3_free(err_msg);
            return false;
        }
        return true;
    }

    bool Database::needs_indexing(const std::filesystem::path& path, const std::string& current_hash) {
        const char* sql = "SELECT hash FROM files WHERE path = ?;";
        sqlite3_stmt* stmt;
        bool needs = true;

        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, path.c_str(), -1, SQLITE_STATIC);
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                std::string db_hash = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                needs = (db_hash != current_hash);
            }
            sqlite3_finalize(stmt);
        }
        return needs;
    }

    bool Database::update_file(const FileInfo& info) {
        const char* sql = 
            "INSERT INTO files (path, hash, last_modified, size, is_indexed) "
            "VALUES (?, ?, ?, ?, 0) "
            "ON CONFLICT(path) DO UPDATE SET "
            "hash = excluded.hash, "
            "last_modified = excluded.last_modified, "
            "size = excluded.size, "
            "is_indexed = 0;";

        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

        sqlite3_bind_text(stmt, 1, info.path.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, info.hash.c_str(), -1, SQLITE_STATIC);
        
        auto duration = info.last_write_time.time_since_epoch();
        auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
        sqlite3_bind_int64(stmt, 3, millis);
        sqlite3_bind_int64(stmt, 4, info.size);

        bool success = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
        return success;
    }

    bool Database::set_indexed_status(const std::filesystem::path& path, bool indexed) {
        const char* sql = "UPDATE files SET is_indexed = ? WHERE path = ?;";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

        sqlite3_bind_int(stmt, 1, indexed ? 1 : 0);
        sqlite3_bind_text(stmt, 2, path.c_str(), -1, SQLITE_STATIC);

        bool success = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
        return success;
    }

    bool Database::remove_file(const std::filesystem::path& path) {
        const char* sql = "DELETE FROM files WHERE path = ?;";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

        sqlite3_bind_text(stmt, 1, path.c_str(), -1, SQLITE_STATIC);

        bool success = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
        return success;
    }

    bool Database::insert_chunk(const std::filesystem::path& file_path, const Chunk& chunk, const std::vector<float>& embedding) {
        const char* id_sql = "SELECT id FROM files WHERE path = ?;";
        sqlite3_stmt* id_stmt;
        int64_t file_id = -1;

        if (sqlite3_prepare_v2(m_db, id_sql, -1, &id_stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(id_stmt, 1, file_path.c_str(), -1, SQLITE_STATIC);
            if (sqlite3_step(id_stmt) == SQLITE_ROW) {
                file_id = sqlite3_column_int64(id_stmt, 0);
            }
            sqlite3_finalize(id_stmt);
        }

        if (file_id == -1) return false;

        const char* sql = "INSERT INTO chunks (file_id, content, start_line, end_line, embedding) VALUES (?, ?, ?, ?, ?);";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

        sqlite3_bind_int64(stmt, 1, file_id);
        sqlite3_bind_text(stmt, 2, chunk.content.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 3, chunk.start_line);
        sqlite3_bind_int(stmt, 4, chunk.end_line);
        
        if (!embedding.empty()) {
            sqlite3_bind_blob(stmt, 5, embedding.data(), embedding.size() * sizeof(float), SQLITE_STATIC);
        } else {
            sqlite3_bind_null(stmt, 5);
        }

        bool success = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
        return success;
    }

    std::vector<Chunk> Database::search_keywords(const std::string& query, int limit) {
        // ... (existing impl)
        std::vector<Chunk> results;
        const char* sql = "SELECT content, start_line, end_line FROM chunks WHERE content LIKE ? LIMIT ?;";
        sqlite3_stmt* stmt;

        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            std::string pattern = "%" + query + "%";
            sqlite3_bind_text(stmt, 1, pattern.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_int(stmt, 2, limit);

            while (sqlite3_step(stmt) == SQLITE_ROW) {
                Chunk chunk;
                chunk.content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                chunk.start_line = sqlite3_column_int(stmt, 1);
                chunk.end_line = sqlite3_column_int(stmt, 2);
                results.push_back(chunk);
            }
            sqlite3_finalize(stmt);
        }
        return results;
    }

    Chunk Database::get_chunk(int64_t id) {
        Chunk chunk;
        const char* sql = "SELECT content, start_line, end_line FROM chunks WHERE id = ?;";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int64(stmt, 1, id);
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                chunk.content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                chunk.start_line = sqlite3_column_int(stmt, 1);
                chunk.end_line = sqlite3_column_int(stmt, 2);
            }
            sqlite3_finalize(stmt);
        }
        return chunk;
    }

    void Database::for_each_vector(std::function<void(int64_t, const std::vector<float>&)> callback) {
        const char* sql = "SELECT id, embedding FROM chunks WHERE embedding IS NOT NULL;";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                int64_t id = sqlite3_column_int64(stmt, 0);
                const void* blob = sqlite3_column_blob(stmt, 1);
                int bytes = sqlite3_column_bytes(stmt, 1);
                
                if (blob && bytes > 0) {
                    size_t count = bytes / sizeof(float);
                    std::vector<float> vec(count);
                    memcpy(vec.data(), blob, bytes);
                    callback(id, vec);
                }
            }
            sqlite3_finalize(stmt);
        }
    }

} 
