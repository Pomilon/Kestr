#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <fstream>
#include <iostream>
#include <sstream>
#include <algorithm>

namespace kestr::engine {

    class Tokenizer {
    public:
        Tokenizer(const std::string& vocab_path) {
            load_vocab(vocab_path);
        }

        std::vector<int64_t> encode(const std::string& text, size_t max_length = 512) {
            std::vector<int64_t> ids;
            ids.push_back(101); // [CLS]

            std::string normalized = to_lower(text); // Basic normalization
            std::stringstream ss(normalized);
            std::string word;

            while (ss >> word) {
                // Remove basic punctuation (simplified)
                word.erase(std::remove_if(word.begin(), word.end(), ::ispunct), word.end());
                if (word.empty()) continue;

                // Max WordPiece length check to avoid stalls
                if (word.length() > 100) word = "[UNK]";

                bool is_bad = false;
                size_t start = 0;
                std::vector<int64_t> sub_tokens;

                while (start < word.length()) {
                    size_t end = word.length();
                    int64_t cur_substr = -1;

                    while (start < end) {
                        std::string substr = word.substr(start, end - start);
                        if (start > 0) substr = "##" + substr;

                        if (m_vocab.count(substr)) {
                            cur_substr = m_vocab[substr];
                            break;
                        }
                        end--;
                    }

                    if (cur_substr == -1) {
                        is_bad = true;
                        break;
                    }

                    sub_tokens.push_back(cur_substr);
                    start = end;
                }

                if (is_bad) {
                    ids.push_back(100); // [UNK]
                } else {
                    ids.insert(ids.end(), sub_tokens.begin(), sub_tokens.end());
                }

                if (ids.size() >= max_length - 1) break; // Reserve 1 for [SEP]
            }

            if (ids.size() >= max_length) {
                ids.resize(max_length - 1);
            }
            ids.push_back(102); // [SEP]

            return ids;
        }

    private:
        std::unordered_map<std::string, int64_t> m_vocab;

        void load_vocab(const std::string& path) {
            std::ifstream file(path);
            if (!file.is_open()) {
                std::cerr << "[Tokenizer] Failed to load vocab: " << path << "\n";
                return;
            }
            std::string line;
            int64_t id = 0;
            while (std::getline(file, line)) {
                // Trim newline
                if (!line.empty() && line.back() == '\r') line.pop_back();
                m_vocab[line] = id++;
            }
        }

        std::string to_lower(const std::string& s) {
            std::string data = s;
            std::transform(data.begin(), data.end(), data.begin(),
                [](unsigned char c){ return std::tolower(c); });
            return data;
        }
    };

}
