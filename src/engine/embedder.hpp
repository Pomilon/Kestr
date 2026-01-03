#pragma once

#include <string>
#include <vector>
#include <memory>
#include "kestr/types.hpp"

namespace kestr::engine {

    /**
     * @brief Abstract base class for embedding generation.
     */
    class Embedder {
    public:
        virtual ~Embedder() = default;

        /**
         * @brief Generates an embedding vector for the given text.
         * @param text The input text chunk.
         * @return A vector of floats representing the embedding.
         */
        virtual std::vector<float> embed(const std::string& text) = 0;

        /**
         * @brief Returns the dimension of the vectors produced by this embedder.
         */
        virtual size_t dimension() const = 0;
    };

    class Chunker {
    public:
        /**
         * @brief Splits file content into chunks based on line count and overlap.
         */
        static std::vector<Chunk> chunk_file(const std::string& content, size_t chunk_size = 500, size_t overlap = 50);
    };

    std::unique_ptr<Embedder> create_ollama_embedder(const std::string& model);
    std::unique_ptr<Embedder> create_onnx_embedder(const std::string& model_path, const std::string& vocab_path);
    std::unique_ptr<Embedder> create_openai_embedder(const std::string& api_key, const std::string& model = "text-embedding-3-small");

}
