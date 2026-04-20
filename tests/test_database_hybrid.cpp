#include <iostream>
#include <filesystem>
#include <cassert>
#include "engine/database.hpp"

using namespace kestr::engine;

void test_new_db() {
    std::cout << "Testing new database initialization..." << std::endl;
    std::filesystem::path db_path = "test_new.db";
    if (std::filesystem::exists(db_path)) std::filesystem::remove(db_path);

    Database db;
    assert(db.open(db_path));

    Chunk chunk;
    chunk.content = "void main() {}";
    chunk.start_line = 1;
    chunk.end_line = 1;
    chunk.symbol_name = "main";
    chunk.symbol_type = "function";
    chunk.project_root = "/tmp/test";
    chunk.language = "cpp";

    FileInfo info;
    info.path = "main.cpp";
    info.hash = "abc";
    info.size = 100;
    info.last_write_time = std::filesystem::file_time_type::clock::now();
    
    assert(db.update_file(info));
    assert(db.insert_chunk("main.cpp", chunk, {0.1f, 0.2f}));

    auto results = db.search_keywords("main", 1);
    assert(!results.empty());
    assert(results[0].symbol_name == "main");
    assert(results[0].symbol_type == "function");
    assert(results[0].project_root == "/tmp/test");
    assert(results[0].language == "cpp");

    std::cout << "New database test passed!" << std::endl;
    db.close();
    std::filesystem::remove(db_path);
}

void test_migration() {
    std::cout << "Testing database migration..." << std::endl;
    std::filesystem::path db_path = "test_migration.db";
    if (std::filesystem::exists(db_path)) std::filesystem::remove(db_path);

    // Create a "legacy" database manually
    sqlite3* raw_db;
    assert(sqlite3_open(db_path.c_str(), &raw_db) == SQLITE_OK);
    const char* legacy_sql = 
        "CREATE TABLE files (id INTEGER PRIMARY KEY, path TEXT UNIQUE, hash TEXT, last_modified INTEGER, size INTEGER, is_indexed INTEGER);"
        "CREATE TABLE chunks (id INTEGER PRIMARY KEY, file_id INTEGER, content TEXT, start_line INTEGER, end_line INTEGER, embedding BLOB);";
    assert(sqlite3_exec(raw_db, legacy_sql, nullptr, nullptr, nullptr) == SQLITE_OK);
    sqlite3_close(raw_db);

    // Open with our Database class which should migrate it
    Database db;
    assert(db.open(db_path));

    Chunk chunk;
    chunk.content = "class MyClass {};";
    chunk.symbol_name = "MyClass";
    chunk.symbol_type = "class";

    FileInfo info;
    info.path = "class.cpp";
    info.hash = "def";
    assert(db.update_file(info));
    assert(db.insert_chunk("class.cpp", chunk, {}));

    auto results = db.search_keywords("MyClass", 1);
    assert(!results.empty());
    assert(results[0].symbol_name == "MyClass");
    assert(results[0].symbol_type == "class");

    std::cout << "Migration test passed!" << std::endl;
    db.close();
    std::filesystem::remove(db_path);
}

int main() {
    try {
        test_new_db();
        test_migration();
        std::cout << "All hybrid database tests passed!" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Test failed: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
