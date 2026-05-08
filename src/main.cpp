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
#include <map>
#ifndef KESTR_PLATFORM_WINDOWS
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#endif

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

std::string get_observability_json(kestr::engine::Database& db, std::shared_ptr<kestr::engine::Librarian> librarian, kestr::engine::JobQueue& queue, const kestr::engine::Config& config) {
    nlohmann::json stats;
    {
        std::lock_guard<std::mutex> lock(g_db_mutex);
        stats["total_files"] = db.count_files();
        stats["total_chunks"] = db.count_chunks();
    }
    stats["memory_items"] = librarian ? librarian->count() : 0;
    stats["queue_size"] = queue.size();
    stats["watch_paths"] = config.watch_paths;
    return stats.dump();
}

void signal_handler(int signum) {
    std::cout << "\n[Kestr] Interrupt signal (" << signum << ") received. Shutting down..." << std::endl;
    g_running = false;
}

#ifndef KESTR_PLATFORM_WINDOWS
void start_web_server(int port, kestr::engine::Database& db, std::shared_ptr<kestr::engine::Librarian> librarian, kestr::engine::JobQueue& queue, const kestr::engine::Config& config) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) return;

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        close(server_fd);
        return;
    }
    listen(server_fd, 3);

    std::string html_page = R"(
    <!DOCTYPE html>
    <html>
    <head>
        <title>Kestr Dashboard</title>
        <style>
            body { font-family: monospace; background: #1a1a1a; color: #ddd; padding: 20px; }
            .card { background: #2a2a2a; border: 1px solid #444; padding: 15px; margin-bottom: 10px; border-radius: 5px; }
            .stat { font-size: 24px; color: #00ff00; }
            .label { color: #aaa; margin-bottom: 5px; }
            .grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 20px; }
        </style>
        <script>
            async function updateStats() {
                try {
                    const res = await fetch('/api/stats');
                    const data = await res.json();
                    document.getElementById('files').innerText = data.total_files;
                    document.getElementById('chunks').innerText = data.total_chunks;
                    document.getElementById('mem').innerText = data.memory_items;
                    document.getElementById('queue').innerText = data.queue_size;
                    document.getElementById('paths').innerText = data.watch_paths.join(', ');
                } catch (e) { console.error(e); }
            }
            setInterval(updateStats, 2000);
            window.onload = updateStats;
        </script>
    </head>
    <body>
        <h1>Kestr Index Observability</h1>
        <div class="grid">
            <div class="card"><div class="label">Total Files</div><div id="files" class="stat">0</div></div>
            <div class="card"><div class="label">Total Chunks</div><div id="chunks" class="stat">0</div></div>
            <div class="card"><div class="label">Items in RAM</div><div id="mem" class="stat">0</div></div>
            <div class="card"><div class="label">Queue Size</div><div id="queue" class="stat">0</div></div>
        </div>
        <div class="card" style="margin-top: 20px;">
            <div class="label">Watched Paths</div>
            <div id="paths" style="word-break: break-all;">None</div>
        </div>
    </body>
    </html>
    )";

    while (g_running) {
        int new_socket = accept(server_fd, nullptr, nullptr);
        if (new_socket == -1) continue;

        char buffer[1024] = {0};
        read(new_socket, buffer, 1024);
        std::string request(buffer);

        if (request.find("GET /api/stats") != std::string::npos) {
            std::string json = get_observability_json(db, librarian, queue, config);
            std::string response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " + std::to_string(json.size()) + "\r\n\r\n" + json;
            send(new_socket, response.c_str(), response.size(), 0);
        } else {
            std::string response = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: " + std::to_string(html_page.size()) + "\r\n\r\n" + html_page;
            send(new_socket, response.c_str(), response.size(), 0);
        }
        close(new_socket);
    }
    close(server_fd);
}
#endif

// Helper to determine if a file should use tree-sitter
bool should_use_treesitter(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    return ext == ".py" || ext == ".cpp" || ext == ".hpp" || ext == ".h" || ext == ".cc" || 
           ext == ".go" || ext == ".rs" || ext == ".js" || ext == ".ts" || 
           ext == ".java" || ext == ".cs" || ext == ".php" || ext == ".rb";
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
    if (ext == ".java") return "java";
    if (ext == ".cs") return "c_sharp";
    if (ext == ".php") return "php";
    if (ext == ".rb") return "ruby";
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

    std::cout << "[Kestr] Starting daemon (v0.2.0)..." << std::endl;

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
    std::cout << "[Kestr] Database path: " << db_path << std::endl;
    
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

    // 2.5 Check for Dimension Mismatch (Migration/Repair)
    // If stored vectors differ from the current model, we must re-index.
    size_t current_dim = embedder ? embedder->dimension() : 384;
    size_t stored_dim = db.get_stored_dimension();
    if (stored_dim != 0 && stored_dim != current_dim) {
        std::cout << "[Kestr] WARNING: Dimension mismatch (DB: " << stored_dim << ", Model: " << current_dim << "). Triggering re-index..." << std::endl;
        db.wipe_all_chunks();
    }

    size_t dim = embedder ? embedder->dimension() : 384; 
    if (dim == 0) dim = 384; 
    
    // 4. Initialize Librarian
    std::shared_ptr<kestr::engine::Librarian> librarian;
    if (config.memory_mode != kestr::engine::Config::MemoryMode::DISK) {
        size_t max_items = (config.memory_mode == kestr::engine::Config::MemoryMode::HYBRID) ? config.hybrid_limit : 100000;
        librarian = std::make_shared<kestr::engine::Librarian>(dim, max_items);
        std::lock_guard<std::mutex> lock(g_db_mutex);
        db.for_each_vector([&](int64_t id, const std::vector<float>& vec) {
            if (config.memory_mode == kestr::engine::Config::MemoryMode::HYBRID && librarian->count() >= config.hybrid_limit) return;
            if (vec.size() == dim) { 
                librarian->add_item(id, vec); 
            }
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

            while (g_running) {
                kestr::engine::FileInfo info;
                bool got_job = queue.pop(info);

                if (got_job) {
                    std::string ext = info.path.extension().string();
                    if (ext != ".cpp" && ext != ".hpp" && ext != ".h" && ext != ".md" && ext != ".txt" && ext != ".json" && ext != ".py" && ext != ".js" && ext != ".ts" && ext != ".go" && ext != ".rs" && ext != ".java" && ext != ".cs" && ext != ".php" && ext != ".rb") continue;
                    
                    std::string current_hash = hasher_scanner.hash_file(info.path);
                    if (current_hash.empty()) continue;

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

                    std::string lang = extension_to_language(info.path);
                    std::vector<kestr::engine::Chunk> chunks;
                    std::vector<std::pair<uint32_t, std::string>> calls;
                    if (should_use_treesitter(info.path)) {
                        kestr::engine::TreeSitterParser ts_parser;
                        chunks = ts_parser.parse(content, lang);
                        calls = ts_parser.extract_calls(content, lang);
                    } else {
                        chunks = kestr::engine::TextChunker::chunk(content, 4000, 0.15f);
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
                        db.begin_transaction();
                        
                        db.update_file(info);
                        auto ids = db.insert_chunks(info.path, chunks, embeddings);
                        
                        for (const auto& call : calls) {
                            if (!ids.empty()) {
                                db.add_symbol_link(ids[0], call.second, "call");
                            }
                        }
                        
                        if (librarian) {
                            for (size_t i = 0; i < ids.size(); ++i) {
                                if (i < embeddings.size() && !embeddings[i].empty()) {
                                    librarian->add_item(ids[i], embeddings[i]);
                                }
                            }
                        }

                        db.set_indexed_status(info.path, true);
                        db.commit_transaction();
                    }
                } else {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
            }
        });
    }

    kestr::engine::Scanner scanner;
    auto scan_directory = [&](const std::filesystem::path& root) {
        std::cout << "[Kestr] Scanning: " << root << std::endl;
        scanner.scan(root, [&](kestr::engine::FileInfo info) {
            info.project_root = root.string();
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
            if (method == "find_references") {
                if (params.empty()) return "{\"error\": \"missing symbol\"}";
                std::string symbol = params[0];
                
                std::lock_guard<std::mutex> lock(g_db_mutex);
                auto refs = db.find_references(symbol);
                
                nlohmann::json res_json = nlohmann::json::array();
                for (const auto& c : refs) {
                    res_json.push_back({{"content", c.content}, {"lines", {c.start_line, c.end_line}}, {"symbol", c.symbol_name}});
                }
                return nlohmann::json({{"result", res_json}}).dump();
            }
            if (method == "get_definition") {
                if (params.empty()) return "{\"error\": \"missing symbol\"}";
                std::string symbol = params[0];
                
                kestr::engine::SearchFilters filters;
                filters.type_filter = "function";
                
                std::lock_guard<std::mutex> lock(g_db_mutex);
                auto results = db.query(symbol, 1, filters);
                
                if (results.empty()) {
                    filters.type_filter = "class";
                    results = db.query(symbol, 1, filters);
                }
                
                if (!results.empty()) {
                    const auto& c = results[0].second;
                    return nlohmann::json({{"result", {{"content", c.content}, {"lines", {c.start_line, c.end_line}}, {"symbol", c.symbol_name}}}}).dump();
                }
                return "{\"error\": \"definition not found\"}";
            }
            if (method == "list_symbols") {
                if (params.empty()) return "{\"error\": \"missing path\"}";
                std::string path = params[0];
                
                std::lock_guard<std::mutex> lock(g_db_mutex);
                const char* sql = "SELECT symbol_name, symbol_type, start_line FROM chunks c "
                                  "JOIN files f ON c.file_id = f.id "
                                  "WHERE f.path = ? AND symbol_name IS NOT NULL ORDER BY start_line;";
                sqlite3_stmt* stmt;
                nlohmann::json symbols = nlohmann::json::array();
                if (sqlite3_prepare_v2(db.get_internal_db(), sql, -1, &stmt, nullptr) == SQLITE_OK) {
                    sqlite3_bind_text(stmt, 1, path.c_str(), -1, SQLITE_STATIC);
                    while (sqlite3_step(stmt) == SQLITE_ROW) {
                        symbols.push_back({
                            {"name", reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0))},
                            {"type", reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1))},
                            {"line", sqlite3_column_int(stmt, 2)}
                        });
                    }
                    sqlite3_finalize(stmt);
                }
                return nlohmann::json({{"result", symbols}}).dump();
            }
            if (method == "summarize_project") {
                if (params.empty()) return "{\"error\": \"missing root\"}";
                std::string root = params[0];
                
                nlohmann::json structure = nlohmann::json::array();
                try {
                    scanner.scan(root, [&](kestr::engine::FileInfo info) {
                        structure.push_back({{"path", info.path.string()}, {"size", info.size}});
                    });
                } catch (...) {}
                return nlohmann::json({{"result", structure}}).dump();
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
                        int candidate_limit = limit * 2;
                        auto semantic_ids = librarian->search(vec, candidate_limit);
                        
                        std::lock_guard<std::mutex> lock(g_db_mutex);
                        auto keyword_results = db.query(q, candidate_limit, filters);
                        
                        std::map<int64_t, double> rrf_scores;
                        const double k = 60.0;
                        
                        for (size_t i = 0; i < semantic_ids.size(); ++i) {
                            rrf_scores[semantic_ids[i]] += 1.0 / (k + i + 1);
                        }
                        for (size_t i = 0; i < keyword_results.size(); ++i) {
                            rrf_scores[keyword_results[i].first] += 1.0 / (k + i + 1);
                        }
                        
                        std::vector<std::pair<int64_t, double>> sorted_candidates(rrf_scores.begin(), rrf_scores.end());
                        std::sort(sorted_candidates.begin(), sorted_candidates.end(), [](const auto& a, const auto& b) {
                            return a.second > b.second;
                        });
                        
                        for (size_t i = 0; i < sorted_candidates.size() && i < (size_t)limit; ++i) {
                            int64_t id = sorted_candidates[i].first;
                            auto c = db.get_chunk(id);
                            
                            if (!filters.type_filter.empty() && c.symbol_type != filters.type_filter) continue;
                            if (!filters.language.empty() && c.language != filters.language) continue;
                            if (!filters.scope.empty() && c.project_root != filters.scope) continue;
                            
                            res_json.push_back({{"type", "hybrid"}, {"content", c.content}, {"lines", {c.start_line, c.end_line}}, {"symbol", c.symbol_name}, {"symbol_type", c.symbol_type}});
                        }
                    }
                }
                if (res_json.empty()) {
                    std::lock_guard<std::mutex> lock(g_db_mutex);
                    for (const auto& pair : db.query(q, limit, filters)) {
                        const auto& c = pair.second;
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
            
            for (const auto& wp : config.watch_paths) {
                if (event.path.string().find(wp) == 0) {
                    info.project_root = wp;
                    break;
                }
            }
            
            try {
                info.size = std::filesystem::file_size(event.path);
                info.last_write_time = std::filesystem::last_write_time(event.path);
                info.hash = "";
                
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

    for (const auto& path : config.watch_paths) {
        sentry->add_watch(path);
        std::thread([=]() { scan_directory(path); }).detach();
    }

    std::cout << "[Kestr] Ready." << std::endl;
    std::thread bridge_thread([&]() { bridge->listen("kestr.sock"); bridge->run(); });
    std::thread sentry_thread([&]() { sentry->start(); });
#ifndef KESTR_PLATFORM_WINDOWS
    std::thread web_thread([&]() { start_web_server(8080, db, librarian, queue, config); });
#endif

    while (g_running) std::this_thread::sleep_for(std::chrono::milliseconds(100));

    queue.stop(); sentry->stop(); bridge->stop();
    if (bridge_thread.joinable()) bridge_thread.join();
    if (sentry_thread.joinable()) sentry_thread.join();
#ifndef KESTR_PLATFORM_WINDOWS
    if (web_thread.joinable()) web_thread.join();
#endif

    return 0;
}
