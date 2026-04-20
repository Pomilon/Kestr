#include <iostream>
#include <thread>
#include <chrono>
#include <csignal>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <mutex>
#include <algorithm>

#include "platform.hpp"
#include "engine/scanner.hpp"
#include "engine/database.hpp"
#include "engine/embedder.hpp"
#include "engine/librarian.hpp"
#include "engine/config.hpp"
#include "engine/job_queue.hpp"
#include "engine/text_chunker.hpp"
#include "engine/treesitter_parser.hpp"
#include <nlohmann/json.hpp>
#include <curl/curl.h>

// Global stop signal
std::atomic<bool> g_running{true};
std::mutex g_db_mutex;

void signal_handler(int signum) {
    std::cout << "\n[Kestr] Interrupt signal (" << signum << ") received. Shutting down..." << std::endl;
    g_running = false;
}

// Helper to determine if a file should use tree-sitter
bool should_use_treesitter(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    return ext == ".py" || ext == ".cpp" || ext == ".hpp" || ext == ".h" || ext == ".cc" || 
           ext == ".go" || ext == ".rs" || ext == ".js" || ext == ".ts";
}

// Helper to map extension to language string
std::string extension_to_language(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    if (ext == ".cpp" || ext == ".hpp" || ext == ".h" || ext == ".cc") return "cpp";
    if (ext == ".py") return "python";
    if (ext == ".js" || ext == ".jsx") return "javascript";
    if (ext == ".ts" || ext == ".tsx") return "typescript";
    if (ext == ".go") return "go";
    if (ext == ".rs") return "rust";
    if (ext == ".md") return "markdown";
    if (ext == ".json") return "json";
    if (ext == ".txt") return "text";
    if (!ext.empty() && ext[0] == '.') return ext.substr(1);
    return "unknown";
}

int main(int argc, char* argv[]) {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    curl_global_init(CURL_GLOBAL_ALL);

    std::cout << "[Kestr] Starting daemon (v0.1.0)..." << std::endl;

    // 1. Setup Config
    auto config_dir = kestr::platform::system::get_config_dir();
    if (!config_dir.empty()) {
        std::filesystem::create_directories(config_dir);
    }
    auto config_path = config_dir / "config.json";
    auto config = kestr::engine::Config::load(config_path);
    
    // 2. Setup Data & DB
    auto data_dir = kestr::platform::system::get_data_dir();
    if (!data_dir.empty()) {
        std::filesystem::create_directories(data_dir);
    } else {
        data_dir = std::filesystem::current_path();
    }
    std::filesystem::path db_path = data_dir / "kestr.db";
    
    kestr::engine::Database db;
    if (!db.open(db_path)) {
        std::cerr << "[Kestr] Failed to open database." << std::endl;
        return 1;
    }

    // 3. Initialize Embedder with Smart Detection & Auto-Download
    std::unique_ptr<kestr::engine::Embedder> embedder;
    const char* env_openai_key = std::getenv("OPENAI_API_KEY");
    if (env_openai_key) config.openai_key = env_openai_key;

    if (config.embedding_backend == "openai" && !config.openai_key.empty()) {
        std::cout << "[Kestr] Using OpenAI Embedder." << std::endl;
        embedder = kestr::engine::create_openai_embedder(config.openai_key);
    } else if (config.embedding_backend == "none") {
        std::cout << "[Kestr] Using Dummy Embedder (none)." << std::endl;
        embedder = kestr::engine::create_dummy_embedder();
    } else {
        std::filesystem::path model_path = data_dir / "model.onnx";
        std::filesystem::path vocab_path = data_dir / "vocab.txt";
        
        bool local_found = std::filesystem::exists(model_path) && std::filesystem::exists(vocab_path);
        
        if (!local_found) {
            // Check CWD as fallback
            if (std::filesystem::exists("model.onnx") && std::filesystem::exists("vocab.txt")) {
                model_path = "model.onnx";
                vocab_path = "vocab.txt";
                local_found = true;
            }
        }

        if (local_found) {
            std::cout << "[Kestr] Using Local ONNX Embedder (" << model_path << ")." << std::endl;
            embedder = kestr::engine::create_onnx_embedder(model_path.string(), vocab_path.string());
        } else {
            bool ollama_up = false;
            CURL* curl = curl_easy_init();
            if (curl) {
                curl_easy_setopt(curl, CURLOPT_URL, "http://localhost:11434/api/tags");
                curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
                curl_easy_setopt(curl, CURLOPT_TIMEOUT, 2L);
                ollama_up = (curl_easy_perform(curl) == CURLE_OK);
                curl_easy_cleanup(curl);
            }

            if (ollama_up && config.embedding_backend != "onnx") {
                std::cout << "[Kestr] Ollama detected. Using " << config.embedding_model << "." << std::endl;
                embedder = kestr::engine::create_ollama_embedder(config.embedding_model);
            } else {
                std::cout << "[Kestr] No embedding backend found. Downloading all-MiniLM-L6-v2..." << std::endl;
                std::filesystem::create_directories(data_dir);
                model_path = data_dir / "model.onnx";
                vocab_path = data_dir / "vocab.txt";
                
                std::string cmd_m = "curl -L -o " + model_path.string() + " https://huggingface.co/Xenova/all-MiniLM-L6-v2/resolve/main/onnx/model.onnx";
                std::string cmd_v = "curl -L -o " + vocab_path.string() + " https://huggingface.co/Xenova/all-MiniLM-L6-v2/resolve/main/vocab.txt";
                
                if (std::system(cmd_m.c_str()) == 0 && std::system(cmd_v.c_str()) == 0) {
                    std::cout << "[Kestr] Download complete." << std::endl;
                    embedder = kestr::engine::create_onnx_embedder(model_path.string(), vocab_path.string());
                } else {
                    std::cerr << "[Kestr] Download failed. Falling back to Ollama stub." << std::endl;
                    embedder = kestr::engine::create_ollama_embedder(config.embedding_model);
                }
            }
        }
    }

    size_t dim = embedder ? embedder->dimension() : 384; 
    if (dim == 0) dim = 384; 
    
    // 4. Initialize Librarian
    std::shared_ptr<kestr::engine::Librarian> librarian;
    if (config.memory_mode != kestr::engine::Config::MemoryMode::DISK) {
        size_t max_items = (config.memory_mode == kestr::engine::Config::MemoryMode::HYBRID) ? config.hybrid_limit : 100000;
        librarian = std::make_shared<kestr::engine::Librarian>(dim, max_items);
        std::lock_guard<std::mutex> lock(g_db_mutex);
        int loaded = 0;
        db.for_each_vector([&](int64_t id, const std::vector<float>& vec) {
            if (config.memory_mode == kestr::engine::Config::MemoryMode::HYBRID && loaded >= (int)config.hybrid_limit) return;
            if (vec.size() == dim) { librarian->add_item(id, vec); loaded++; }
        });
        std::cout << "[Kestr] Librarian ready with " << librarian->count() << " items." << std::endl;
    }

    // 5. Worker Logic
    kestr::engine::JobQueue queue;
    std::vector<std::thread> workers;
    size_t num_workers = std::max(1u, std::thread::hardware_concurrency());
    std::cout << "[Kestr] Spawning " << num_workers << " worker threads." << std::endl;

    for (size_t i = 0; i < num_workers; ++i) {
        workers.emplace_back([&]() {
            kestr::engine::Scanner hasher_scanner; 
            int pending_commits = 0;

            while (g_running) {
                kestr::engine::FileInfo info;
                bool got_job = queue.pop(info);

                if (got_job) {
                    std::string ext = info.path.extension().string();
                    if (ext != ".cpp" && ext != ".hpp" && ext != ".h" && ext != ".md" && ext != ".txt" && ext != ".json" && ext != ".py" && ext != ".js" && ext != ".ts" && ext != ".go" && ext != ".rs") continue;
                    
                    std::string current_hash = hasher_scanner.hash_file(info.path);
                    
                    {
                        std::lock_guard<std::mutex> lock(g_db_mutex);
                        if (!db.needs_indexing(info.path, current_hash)) continue; 
                    }

                    info.hash = current_hash;

                    std::string content;
                    try {
                        std::ifstream file(info.path, std::ios::binary);
                        if (!file.is_open()) continue;
                        auto size = std::filesystem::file_size(info.path);
                        content.resize(size);
                        file.read(&content[0], size);
                    } catch (...) {
                        continue;
                    }

                    std::string lang = extension_to_language(info.path);                    std::vector<kestr::engine::Chunk> chunks;

                    if (should_use_treesitter(info.path)) {
                        kestr::engine::TreeSitterParser ts_parser;
                        chunks = ts_parser.parse(content, lang);
                    }

                    if (chunks.empty()) {
                        chunks = kestr::engine::TextChunker::chunk(content, 4000, 0.15f);
                    }
                    
                    std::vector<std::vector<float>> embeddings;
                    for (auto& c : chunks) {
                        c.language = lang;
                        c.project_root = info.project_root;
                        if (embedder) embeddings.push_back(embedder->embed(c.content));
                        else embeddings.push_back({});
                    }

                    {
                        std::lock_guard<std::mutex> lock(g_db_mutex);
                        if (pending_commits == 0) db.begin_transaction();
                        
                        db.update_file(info);
                        auto ids = db.insert_chunks(info.path, chunks, embeddings);
                        
                        if (librarian) {
                            for (size_t i = 0; i < ids.size(); ++i) {
                                if (i < embeddings.size() && !embeddings[i].empty()) {
                                    librarian->add_item(ids[i], embeddings[i]);
                                }
                            }
                        }

                        db.set_indexed_status(info.path, true);
                        
                        pending_commits++;
                        if (pending_commits >= 10) {
                            db.commit_transaction();
                            pending_commits = 0;
                        }
                    }
                } else {
                    if (pending_commits > 0) {
                        std::lock_guard<std::mutex> lock(g_db_mutex);
                        db.commit_transaction();
                        pending_commits = 0;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
            }
        });
    }

    kestr::engine::Scanner scanner;
    auto scan_directory = [&](const std::filesystem::path& root) {
        std::cout << "[Kestr] Scanning: " << root << std::endl;
        scanner.scan(root, [&](kestr::engine::FileInfo info) {
            info.project_root = root.string(); // Set project_root during initial scan
            bool metadata_changed;
            {
                std::lock_guard<std::mutex> lock(g_db_mutex);
                auto duration = info.last_write_time.time_since_epoch();
                auto mtime = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
                metadata_changed = db.check_metadata(info.path, info.size, mtime);
            }
            if (metadata_changed) {
                queue.push(info);
            }
        });
    };

    // 6. Setup IPC & Sentry
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
                { std::lock_guard<std::mutex> lock(g_db_mutex); res["total_files"] = db.count_files(); res["total_chunks"] = db.count_chunks(); }
                res["memory_items"] = librarian ? librarian->count() : 0;
                res["queue_size"] = queue.size();
                res["watch_paths"] = config.watch_paths;
                return nlohmann::json({{"result", res}}).dump();
            }
            if (method == "watch_add") {
                if (params.empty()) return "{\"error\": \"missing path\"}";
                std::filesystem::path p;
                try { p = std::filesystem::canonical(std::filesystem::absolute(params[0].get<std::string>())); } 
                catch (...) { p = std::filesystem::absolute(params[0].get<std::string>()); }
                
                if (!std::filesystem::exists(p)) return "{\"error\": \"path does not exist\"}";
                std::string path_s = p.string();
                
                if (std::find(config.watch_paths.begin(), config.watch_paths.end(), path_s) == config.watch_paths.end()) {
                    config.watch_paths.push_back(path_s);
                    config.save(config_path);
                    sentry->add_watch(p);
                    std::thread([=]() { scan_directory(p); }).detach();
                    return "{\"result\": \"added: " + path_s + "\"}";
                }
                return "{\"result\": \"already watched\"}";
            }
            if (method == "shutdown") { g_running = false; return "{\"result\": \"shutting down\"}"; }
            if (method == "resource_list") {
                std::lock_guard<std::mutex> lock(g_db_mutex);
                return nlohmann::json({{"result", db.get_all_files()}}).dump();
            }
            if (method == "resource_read") {
                if (params.empty()) return "{\"error\": \"missing uri\"}";
                std::string uri = params[0];
                if (uri.find("kestr://") != 0) return "{\"error\": \"invalid uri\"}";
                std::ifstream t(uri.substr(8));
                if (!t.is_open()) return "{\"error\": \"not found\"}";
                std::stringstream b; b << t.rdbuf();
                return nlohmann::json({{"result", {{"content", b.str()}}}}).dump();
            }
            if (method == "query") {
                if (params.empty()) return "{\"error\": \"missing query\"}";
                std::string q = params[0];
                int limit = (params.size() > 1 && params[1].is_number()) ? params[1].get<int>() : 5;
                
                kestr::engine::SearchFilters filters;
                if (params.size() > 2 && params[2].is_string()) filters.type_filter = params[2];
                if (params.size() > 3 && params[3].is_string()) filters.language = params[3];
                if (params.size() > 4 && params[4].is_string()) filters.scope = params[4];

                nlohmann::json res_json = nlohmann::json::array();
                if (embedder && librarian) {
                    auto vec = embedder->embed(q);
                    if (!vec.empty()) {
                        auto ids = librarian->search(vec, limit);
                        std::lock_guard<std::mutex> lock(g_db_mutex);
                        for (auto id : ids) {
                            auto c = db.get_chunk(id);
                            
                            // Apply filters manually for semantic search for now
                            if (!filters.type_filter.empty() && c.symbol_type != filters.type_filter) continue;
                            if (!filters.language.empty() && c.language != filters.language) continue;
                            if (!filters.scope.empty() && c.project_root != filters.scope) continue;

                            res_json.push_back({{"type", "semantic"}, {"content", c.content}, {"lines", {c.start_line, c.end_line}}, {"symbol", c.symbol_name}, {"symbol_type", c.symbol_type}});
                        }
                    }
                }
                if (res_json.empty()) {
                    std::lock_guard<std::mutex> lock(g_db_mutex);
                    for (const auto& c : db.query(q, limit, filters)) {
                        res_json.push_back({{"type", "keyword"}, {"content", c.content}, {"lines", {c.start_line, c.end_line}}, {"symbol", c.symbol_name}, {"symbol_type", c.symbol_type}});
                    }
                }
                return nlohmann::json({{"result", res_json}}).dump();
            }
        } catch (...) { return "{\"error\": \"error\"}"; }
        return "{\"error\": \"unknown\"}";
    });

    sentry->set_callback([&](const kestr::platform::FileEvent& event) {
         if (event.type != kestr::platform::FileEvent::Type::Deleted) {
            kestr::engine::FileInfo info;
            info.path = event.path;
            
            // Find which watch_path this file belongs to
            for (const auto& wp : config.watch_paths) {
                if (event.path.string().find(wp) == 0) {
                    info.project_root = wp;
                    break;
                }
            }

            try {
                info.size = std::filesystem::file_size(event.path);
                info.last_write_time = std::filesystem::last_write_time(event.path);
                info.hash = ""; // Worker will compute it
                
                bool metadata_changed;
                {
                    std::lock_guard<std::mutex> lock(g_db_mutex);
                    auto duration = info.last_write_time.time_since_epoch();
                    auto mtime = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
                    metadata_changed = db.check_metadata(info.path, info.size, mtime);
                }
                
                if (metadata_changed) {
                    std::cout << "[Sentry] Queueing changed file: " << event.path << std::endl;
                    queue.push(info);
                }
            } catch (...) {} 
         } else {
            std::lock_guard<std::mutex> lock(g_db_mutex);
            db.remove_file(event.path);
         }
    });

    // Startup
    for (const auto& path : config.watch_paths) {
        sentry->add_watch(path);
        std::thread([=]() { scan_directory(path); }).detach();
    }

    std::cout << "[Kestr] Ready." << std::endl;
    std::thread bridge_thread([&]() { bridge->listen("kestr.sock"); bridge->run(); });
    std::thread sentry_thread([&]() { sentry->start(); });

    while (g_running) std::this_thread::sleep_for(std::chrono::milliseconds(100));

    queue.stop(); sentry->stop(); bridge->stop();
    for (auto& worker : workers) {
        if (worker.joinable()) worker.join();
    }
    if (bridge_thread.joinable()) bridge_thread.join();
    if (sentry_thread.joinable()) sentry_thread.join();

    return 0;
}
