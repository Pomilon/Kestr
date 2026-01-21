#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace kestr::engine {

    struct Config {
        enum class MemoryMode {
            RAM,    // Load all vectors into memory (Fastest)
            HYBRID, // Load limited subset into memory
            DISK    // Do not load vectors (Keyword search only, lowest RAM)
        };

        MemoryMode memory_mode = MemoryMode::RAM;
        size_t hybrid_limit = 1000;
        std::string embedding_model = "all-minilm";
        std::string embedding_backend = "ollama";
        std::string openai_key = "";
        std::vector<std::string> watch_paths;

        static Config load(const std::filesystem::path& path) {
            Config cfg;
            if (!std::filesystem::exists(path)) return cfg;

            try {
                std::ifstream f(path);
                nlohmann::json j = nlohmann::json::parse(f);

                if (j.contains("memory_mode")) {
                    std::string mode = j["memory_mode"];
                    if (mode == "disk") cfg.memory_mode = MemoryMode::DISK;
                    else if (mode == "hybrid") cfg.memory_mode = MemoryMode::HYBRID;
                }
                if (j.contains("hybrid_limit")) cfg.hybrid_limit = j["hybrid_limit"];
                if (j.contains("embedding_model")) cfg.embedding_model = j["embedding_model"];
                if (j.contains("embedding_backend")) cfg.embedding_backend = j["embedding_backend"];
                if (j.contains("openai_key")) cfg.openai_key = j["openai_key"];
                if (j.contains("watch_paths")) cfg.watch_paths = j["watch_paths"].get<std::vector<std::string>>();
            } catch (...) {}
            return cfg;
        }

        void save(const std::filesystem::path& path) const {
            nlohmann::json j;
            j["memory_mode"] = (memory_mode == MemoryMode::RAM) ? "ram" : 
                               (memory_mode == MemoryMode::HYBRID) ? "hybrid" : "disk";
            j["hybrid_limit"] = hybrid_limit;
            j["embedding_model"] = embedding_model;
            j["embedding_backend"] = embedding_backend;
            if (!openai_key.empty()) j["openai_key"] = openai_key;
            j["watch_paths"] = watch_paths;

            std::ofstream f(path);
            f << j.dump(4);
        }
    };

}