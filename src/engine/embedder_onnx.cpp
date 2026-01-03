#include "embedder.hpp"
#include "tokenizer.hpp"
#include <iostream>
#include <vector>
#include <numeric>
#include <cmath>
#include <filesystem>

#ifdef KESTR_WITH_ONNX
#include <onnxruntime_cxx_api.h>
#endif

namespace kestr::engine {

    class OnnxEmbedder : public Embedder {
    public:
        OnnxEmbedder(const std::string& model_path, const std::string& vocab_path) {
#ifdef KESTR_WITH_ONNX
            if (!std::filesystem::exists(model_path) || !std::filesystem::exists(vocab_path)) {
                std::cerr << "[OnnxEmbedder] Model or Vocab file not found.\n";
                return;
            }

            try {
                // 1. Initialize Environment
                m_env = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "kestr");
                
                // 2. Load Model
                Ort::SessionOptions session_options;
                session_options.SetIntraOpNumThreads(1);
                session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
                
                m_session = std::make_unique<Ort::Session>(*m_env, model_path.c_str(), session_options);
                m_tokenizer = std::make_unique<Tokenizer>(vocab_path);
                
                std::cout << "[OnnxEmbedder] Loaded: " << model_path << "\n";
                m_ready = true;
            } catch (const Ort::Exception& e) {
                std::cerr << "[OnnxEmbedder] Initialization failed: " << e.what() << "\n";
            }
#else
            std::cerr << "[OnnxEmbedder] Compiled without ONNX Runtime support.\n";
#endif
        }

        std::vector<float> embed(const std::string& text) override {
            std::vector<float> embedding;
#ifdef KESTR_WITH_ONNX
            if (!m_ready) return embedding;

            // 1. Tokenize
            auto input_ids = m_tokenizer->encode(text);
            size_t batch_size = 1;
            size_t seq_length = input_ids.size();
            
            std::vector<int64_t> token_type_ids(seq_length, 0);
            std::vector<int64_t> attention_mask(seq_length, 1);

            // 2. Prepare Tensors
            std::vector<int64_t> input_shape = { (int64_t)batch_size, (int64_t)seq_length };
            
            auto memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
            
            std::vector<Ort::Value> input_tensors;
            input_tensors.push_back(Ort::Value::CreateTensor<int64_t>(memory_info, input_ids.data(), input_ids.size(), input_shape.data(), input_shape.size()));
            input_tensors.push_back(Ort::Value::CreateTensor<int64_t>(memory_info, attention_mask.data(), attention_mask.size(), input_shape.data(), input_shape.size()));
            input_tensors.push_back(Ort::Value::CreateTensor<int64_t>(memory_info, token_type_ids.data(), token_type_ids.size(), input_shape.data(), input_shape.size()));

            const char* input_names[] = { "input_ids", "attention_mask", "token_type_ids" };
            const char* output_names[] = { "last_hidden_state" }; // Adjust based on model inspection if needed

            // 3. Run
            try {
                auto output_tensors = m_session->Run(Ort::RunOptions{nullptr}, input_names, input_tensors.data(), 3, output_names, 1);
                
                // 4. Extract & Mean Pooling
                // Output shape: [batch, seq, hidden_size] (e.g., 1, 512, 384)
                float* float_data = output_tensors[0].GetTensorMutableData<float>();
                auto type_info = output_tensors[0].GetTensorTypeAndShapeInfo();
                auto shape = type_info.GetShape();
                size_t hidden_size = shape[2];
                
                embedding.resize(hidden_size, 0.0f);

                // Mean pooling: Sum vectors where mask=1, divide by count
                for (size_t i = 0; i < seq_length; ++i) {
                    for (size_t j = 0; j < hidden_size; ++j) {
                        embedding[j] += float_data[i * hidden_size + j];
                    }
                }
                
                // Normalize
                float norm = 0.0f;
                for (float& val : embedding) {
                    val /= (float)seq_length;
                    norm += val * val;
                }
                norm = std::sqrt(norm);
                for (float& val : embedding) val /= (norm + 1e-9f); // Avoid div/0

            } catch (const Ort::Exception& e) {
                std::cerr << "[OnnxEmbedder] Inference failed: " << e.what() << "\n";
            }
#endif
            return embedding;
        }

        size_t dimension() const override { return 384; }

    private:
        bool m_ready = false;
#ifdef KESTR_WITH_ONNX
        std::unique_ptr<Ort::Env> m_env;
        std::unique_ptr<Ort::Session> m_session;
        std::unique_ptr<Tokenizer> m_tokenizer;
#endif
    };

    std::unique_ptr<Embedder> create_onnx_embedder(const std::string& model_path, const std::string& vocab_path) {
        return std::make_unique<OnnxEmbedder>(model_path, vocab_path);
    }

}