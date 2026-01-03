#include <iostream>
#include <thread>
#include <chrono>
#include <csignal>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <mutex>

#include "platform.hpp"
#include "engine/scanner.hpp"
#include "engine/database.hpp"
#include "engine/embedder.hpp"
#include "engine/librarian.hpp"
#include "engine/config.hpp"
#include "engine/job_queue.hpp"
#include <nlohmann/json.hpp>

// Global stop signal
std::atomic<bool> g_running{true};
std::mutex g_db_mutex;

void signal_handler(int signum) {
    std::cout << "\n[Kestr] Interrupt signal (" << signum << ") received. Shutting down...\n";
    g_running = false;
}

int main(int argc, char* argv[]) {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    std::cout << "[Kestr] Starting daemon (v0.1.0)...\n";

    // Setup Config Directory
    auto config_dir = kestr::platform::system::get_config_dir();
    if (!config_dir.empty()) {
        std::filesystem::create_directories(config_dir);
    }
    auto config_path = config_dir / "config.json";
    std::cout << "[Kestr] Config path: " << config_path << "\n";

    auto config = kestr::engine::Config::load(config_path);
    std::cout << "[Kestr] Memory Mode: " << (config.memory_mode == kestr::engine::Config::MemoryMode::RAM ? "RAM" : 
                                             (config.memory_mode == kestr::engine::Config::MemoryMode::HYBRID ? "HYBRID" : "DISK")) << "\n";
    
    // Setup Data Directory
    auto data_dir = kestr::platform::system::get_data_dir();
    if (!data_dir.empty()) {
        std::filesystem::create_directories(data_dir);
    } else {
        data_dir = std::filesystem::current_path(); // Fallback
    }
    std::filesystem::path db_path = data_dir / "kestr.db";
    std::cout << "[Kestr] Database path: " << db_path << "\n";

    kestr::engine::Database db;
    if (!db.open(db_path)) {

        std::cerr << "[Kestr] Failed to open database.\n";
        return 1;
    }

    // Initialize Embedder
    std::unique_ptr<kestr::engine::Embedder> embedder;
    const char* env_openai_key = std::getenv("OPENAI_API_KEY");
    if (env_openai_key) config.openai_key = env_openai_key;

    if (!config.openai_key.empty()) {
        std::cout << "[Kestr] Using OpenAI Embedder.\n";
        embedder = kestr::engine::create_openai_embedder(config.openai_key);
    } else if (std::filesystem::exists("model.onnx") && std::filesystem::exists("vocab.txt")) {
        std::cout << "[Kestr] Using Local ONNX Embedder.\n";
        embedder = kestr::engine::create_onnx_embedder("model.onnx", "vocab.txt");
    } else {
        std::cout << "[Kestr] Using Ollama fallback.\n";
        embedder = kestr::engine::create_ollama_embedder(config.embedding_model);
    }

    size_t dim = embedder ? embedder->dimension() : 384; 
    if (dim == 0) dim = 384; 
    
    std::shared_ptr<kestr::engine::Librarian> librarian;
    if (config.memory_mode != kestr::engine::Config::MemoryMode::DISK) {
        size_t max_items = (config.memory_mode == kestr::engine::Config::MemoryMode::HYBRID) ? config.hybrid_limit : 100000;
        librarian = std::make_shared<kestr::engine::Librarian>(dim, max_items);
        
        std::cout << "[Kestr] Loading vectors into memory (" << (config.memory_mode == kestr::engine::Config::MemoryMode::HYBRID ? "HYBRID" : "RAM") << " mode)...\n";
        
        std::lock_guard<std::mutex> lock(g_db_mutex);
        int loaded = 0;
        db.for_each_vector([&](int64_t id, const std::vector<float>& vec) {
            if (config.memory_mode == kestr::engine::Config::MemoryMode::HYBRID && loaded >= config.hybrid_limit) return;
            if (vec.size() == dim) {
                librarian->add_item(id, vec);
                loaded++;
            }
        });
        std::cout << "[Kestr] Librarian ready with " << librarian->count() << " items.\n";
    }

    // Worker Logic
    kestr::engine::JobQueue queue;
    std::thread worker_thread([&]() {
        while (g_running) {
            kestr::engine::FileInfo info;
            if (queue.pop(info)) {
                // Extension check
                std::string ext = info.path.extension().string();
                if (ext != ".cpp" && ext != ".hpp" && ext != ".h" && ext != ".md" && ext != ".txt" && ext != ".json") {
                    continue;
                }

                std::cout << "[Worker] Indexing: " << info.path.filename() << "\n";
                
                std::ifstream file(info.path);
                if (!file.is_open()) continue;
                std::stringstream buffer;
                buffer << file.rdbuf();
                std::string content = buffer.str();

                auto chunks = kestr::engine::Chunker::chunk_file(content, 100, 10);
                
                {
                    std::lock_guard<std::mutex> lock(g_db_mutex);
                    db.update_file(info);
                    for (const auto& chunk : chunks) {
                        std::vector<float> vector;
                        if (embedder) vector = embedder->embed(chunk.content);
                        int64_t chunk_id = db.insert_chunk(info.path, chunk, vector); 
                        if (chunk_id > 0 && !vector.empty() && librarian) {
                            librarian->add_item(chunk_id, vector);
                        }
                    }
                    db.set_indexed_status(info.path, true);
                }
            }
        }
    });

    // Scanner (Producer)
    std::cout << "[Kestr] Queueing files for indexing...\n";
    kestr::engine::Scanner scanner;
    int skipped = 0;
    int queued = 0;
    scanner.scan(std::filesystem::current_path(), [&](const kestr::engine::FileInfo& info) {
        bool needs;
        {
            std::lock_guard<std::mutex> lock(g_db_mutex);
            needs = db.needs_indexing(info.path, info.hash);
        }
        if (needs) {
            queue.push(info);
            queued++;
        } else {
            skipped++;
        }
    });
    std::cout << "[Kestr] Scan complete. Queued: " << queued << ", Skipped: " << skipped << "\n";

    // Setup IPC & Sentry
    auto sentry = kestr::platform::Sentry::create();
    auto bridge = kestr::platform::Bridge::create();
    if (!sentry || !bridge) return 1;

    bridge->set_handler([&](const std::string& request) -> std::string {
        try {
            auto j = nlohmann::json::parse(request);
            std::string method = j.value("method", "");
            auto params = j.value("params", nlohmann::json::array());

            if (method == "ping") return "{\"result\": \"pong\"}";
            if (method == "status") {
                nlohmann::json res;
                res["memory_items"] = librarian ? librarian->count() : 0;
                res["queue_size"] = queue.size();
                res["memory_mode"] = (config.memory_mode == kestr::engine::Config::MemoryMode::RAM) ? "RAM" : 
                                     (config.memory_mode == kestr::engine::Config::MemoryMode::HYBRID) ? "HYBRID" : "DISK";
                return nlohmann::json({{"result", res}}).dump();
            }
            if (method == "reindex") {
                // Simplified: just trigger a scan in a separate thread to not block handler
                std::thread([&scanner, &db, &queue]() {
                    scanner.scan(std::filesystem::current_path(), [&](const kestr::engine::FileInfo& info) {
                        queue.push(info);
                    });
                }).detach();
                return "{\"result\": \"reindexing started\"}";
            }
            if (method == "shutdown") {
                g_running = false;
                return "{\"result\": \"shutting down\"}";
            }
            if (method == "query") {
                if (params.empty()) return "{\"error\": \"missing query\"}";
                std::string q = params[0];
                nlohmann::json res_json = nlohmann::json::array();
                
                if (embedder && librarian) {
                    auto vec = embedder->embed(q);
                    if (!vec.empty()) {
                        auto ids = librarian->search(vec, 5);
                        std::lock_guard<std::mutex> lock(g_db_mutex);
                        for (auto id : ids) {
                            auto c = db.get_chunk(id);
                            res_json.push_back({{"type", "semantic"}, {"content", c.content}, {"lines", {c.start_line, c.end_line}}});
                        }
                    }
                }

                if (res_json.empty()) {
                    std::lock_guard<std::mutex> lock(g_db_mutex);
                    auto results = db.search_keywords(q);
                    for (const auto& c : results) {
                        res_json.push_back({{"type", "keyword"}, {"content", c.content}, {"lines", {c.start_line, c.end_line}}});
                    }
                }
                return nlohmann::json({{"result", res_json}}).dump();
            }
        } catch (...) { return "{\"error\": \"invalid json\"}"; }
        return "{\"error\": \"unknown method\"}";
    });

    sentry->set_callback([&](const kestr::platform::FileEvent& event) {
         if (event.type != kestr::platform::FileEvent::Type::Deleted) {
            std::cout << "[Sentry] Change detected: " << event.path << "\n";
            // Basic FileInfo reconstruction
            kestr::engine::FileInfo info;
            info.path = event.path;
            try {
                info.size = std::filesystem::file_size(event.path);
                info.last_write_time = std::filesystem::last_write_time(event.path);
                info.hash = kestr::engine::Scanner().hash_file(event.path);
                queue.push(info);
            } catch (...) {
                // File might be gone or inaccessible
            }
         } else {
            // Handle deletion: Remove from DB and Librarian
            std::cout << "[Sentry] File deleted: " << event.path << "\n";
            std::lock_guard<std::mutex> lock(g_db_mutex);
            db.remove_file(event.path);
            // Note: Removing from Librarian HNSW is hard (requires rebuild or soft delete). 
            // Ideally we soft-delete in Librarian or just keep it until restart.
            // For now, DB delete prevents retrieval in query (fallback), but vector search might still return ID.
            // Since we check DB for content after search, it will fail gracefully (get_chunk returns empty).
         }
    });


    sentry->add_watch(std::filesystem::current_path());
    std::cout << "[Kestr] Ready.\n";
    
    std::thread bridge_thread([&bridge]() { bridge->listen("kestr.sock"); bridge->run(); });
    std::thread sentry_thread([&sentry]() { sentry->start(); });

    while (g_running) std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Shutdown
    queue.stop();
    sentry->stop();
    bridge->stop();
    if (worker_thread.joinable()) worker_thread.join();
    if (bridge_thread.joinable()) bridge_thread.detach(); 
    if (sentry_thread.joinable()) sentry_thread.join();

    return 0;
}