#include "embedder.hpp"
#include <nlohmann/json.hpp>
#include <curl/curl.h>
#include <iostream>

using json = nlohmann::json;

namespace kestr::engine {

    class OllamaEmbedder : public Embedder {
    public:
        OllamaEmbedder(const std::string& model = "all-minilm", const std::string& endpoint = "http://localhost:11434/api/embeddings")
            : m_model(model), m_endpoint(endpoint) {
            curl_global_init(CURL_GLOBAL_DEFAULT);
        }

        ~OllamaEmbedder() {
            curl_global_cleanup();
        }

        std::vector<float> embed(const std::string& text) override {
            CURL* curl = curl_easy_init();
            std::vector<float> embedding;

            if (curl) {
                std::string json_str;
                try {
                    json body = {
                        {"model", m_model},
                        {"prompt", text}
                    };
                    json_str = body.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
                } catch (const std::exception& e) {
                    std::cerr << "[OllamaEmbedder] JSON serialization error: " << e.what() << "\n";
                    curl_easy_cleanup(curl);
                    return embedding;
                }

                struct curl_slist* headers = nullptr;
                headers = curl_slist_append(headers, "Content-Type: application/json");

                std::string response_string;
                curl_easy_setopt(curl, CURLOPT_URL, m_endpoint.c_str());
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_str.c_str());
                curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
                curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
                curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_string);

                CURLcode res = curl_easy_perform(curl);
                if (res != CURLE_OK) {
                    std::cerr << "[OllamaEmbedder] curl_easy_perform() failed: " << curl_easy_strerror(res) << "\n";
                } else {
                    try {
                        auto resp_json = json::parse(response_string);
                        if (resp_json.contains("embedding")) {
                            embedding = resp_json["embedding"].get<std::vector<float>>();
                            m_dimension = embedding.size();
                        }
                    } catch (const std::exception& e) {
                        std::cerr << "[OllamaEmbedder] JSON parse error: " << e.what() << "\n";
                    }
                }

                curl_slist_free_all(headers);
                curl_easy_cleanup(curl);
            }

            return embedding;
        }

        size_t dimension() const override { return m_dimension; }

    private:
        std::string m_model;
        std::string m_endpoint;
        size_t m_dimension = 0;

        static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
            ((std::string*)userp)->append((char*)contents, size * nmemb);
            return size * nmemb;
        }
    };

    // Factory method helper
    std::unique_ptr<Embedder> create_ollama_embedder(const std::string& model) {
        return std::make_unique<OllamaEmbedder>(model);
    }

}
