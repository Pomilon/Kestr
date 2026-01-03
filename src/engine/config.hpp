#pragma once

#include <string>
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
        size_t hybrid_limit = 1000; // Max chunks in RAM for hybrid mode
        std::string embedding_model = "all-minilm";
        std::string embedding_backend = "ollama"; // ollama, onnx, openai
        std::string embedding_endpoint = "http://localhost:11434/api/embeddings"; // for ollama
        std::string openai_key = ""; 

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
            } catch (...) {}
            return cfg;
        }

        void save(const std::filesystem::path& path) const {
            nlohmann::json j;
            j["memory_mode"] = (memory_mode == MemoryMode::RAM) ? "ram" : "disk";
            j["embedding_model"] = embedding_model;
            j["embedding_backend"] = embedding_backend;
            if (!openai_key.empty()) j["openai_key"] = openai_key;

            std::ofstream f(path);
            f << j.dump(4);
        }
    };

}
