#include "embedder.hpp"
#include <nlohmann/json.hpp>
#include <curl/curl.h>
#include <iostream>
#include <cstdlib>

using json = nlohmann::json;

namespace kestr::engine {

    class OpenAIEmbedder : public Embedder {
    public:
        OpenAIEmbedder(const std::string& api_key, const std::string& model = "text-embedding-3-small")
            : m_api_key(api_key), m_model(model) {
            curl_global_init(CURL_GLOBAL_DEFAULT);
        }

        ~OpenAIEmbedder() {
            curl_global_cleanup();
        }

        std::vector<float> embed(const std::string& text) override {
            CURL* curl = curl_easy_init();
            std::vector<float> embedding;

            if (curl) {
                json body = {
                    {"model", m_model},
                    {"input", text}
                };
                std::string json_str = body.dump();

                struct curl_slist* headers = nullptr;
                headers = curl_slist_append(headers, "Content-Type: application/json");
                std::string auth_header = "Authorization: Bearer " + m_api_key;
                headers = curl_slist_append(headers, auth_header.c_str());

                std::string response_string;
                curl_easy_setopt(curl, CURLOPT_URL, "https://api.openai.com/v1/embeddings");
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_str.c_str());
                curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
                curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
                curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_string);

                CURLcode res = curl_easy_perform(curl);
                if (res != CURLE_OK) {
                    std::cerr << "[OpenAIEmbedder] curl_easy_perform() failed: " << curl_easy_strerror(res) << "\n";
                } else {
                    try {
                        auto resp_json = json::parse(response_string);
                        if (resp_json.contains("error")) {
                             std::cerr << "[OpenAIEmbedder] API Error: " << resp_json["error"].dump() << "\n";
                        } else if (resp_json.contains("data") && !resp_json["data"].empty()) {
                            embedding = resp_json["data"][0]["embedding"].get<std::vector<float>>();
                            m_dimension = embedding.size();
                        }
                    } catch (const std::exception& e) {
                        std::cerr << "[OpenAIEmbedder] JSON parse error: " << e.what() << "\n";
                    }
                }

                curl_slist_free_all(headers);
                curl_easy_cleanup(curl);
            }

            return embedding;
        }

        size_t dimension() const override { return m_dimension; }

    private:
        std::string m_api_key;
        std::string m_model;
        size_t m_dimension = 0;

        static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
            ((std::string*)userp)->append((char*)contents, size * nmemb);
            return size * nmemb;
        }
    };

    std::unique_ptr<Embedder> create_openai_embedder(const std::string& api_key, const std::string& model) {
        return std::make_unique<OpenAIEmbedder>(api_key, model);
    }

}
