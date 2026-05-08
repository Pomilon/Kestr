#include "text_chunker.hpp"
#include <vector>
#include <string>
#include <algorithm>
#include <sstream>

namespace kestr::engine {

    static int count_newlines(const std::string& text) {
        int count = 0;
        for (char c : text) if (c == '\n') count++;
        return count;
    }

    std::vector<Chunk> TextChunker::chunk(const std::string& content, size_t target_size, float overlap_percent) {
        std::vector<Chunk> result;
        if (content.empty()) return result;

        std::vector<std::string> separators = {"\n\n", "\n", " "};
        
        auto split_results = recursive_split(content, 1, target_size, separators, 0);

        size_t overlap_size = static_cast<size_t>(target_size * overlap_percent);
        
        std::string current_buffer;
        int current_start_line = 1;
        int current_end_line = 1;

        for (const auto& sr : split_results) {
            if (current_buffer.size() + sr.text.size() > target_size && !current_buffer.empty()) {
                result.push_back({current_buffer, current_start_line, current_end_line});
                
                size_t actual_overlap = std::min(current_buffer.size(), overlap_size);
                std::string overlap_text = current_buffer.substr(current_buffer.size() - actual_overlap);
                
                current_buffer = overlap_text + sr.text;
                current_start_line = std::max(1, current_end_line - count_newlines(overlap_text));
                current_end_line = current_start_line + count_newlines(current_buffer);
            } else {
                if (current_buffer.empty()) {
                    current_start_line = sr.start_line;
                }
                current_buffer += sr.text;
                current_end_line = sr.end_line;
            }
        }

        if (!current_buffer.empty()) {
            result.push_back({current_buffer, current_start_line, current_end_line});
        }

        return result;
    }

    std::vector<Chunk> TextChunker::chunk_with_breakpoints(const std::string& content, 
                                                            size_t target_size, 
                                                            float overlap_percent, 
                                                            const std::vector<uint32_t>& breakpoints) {
        std::vector<Chunk> result;
        if (content.empty()) return result;

        size_t overlap_size = static_cast<size_t>(target_size * overlap_percent);
        size_t current_start = 0;

        while (current_start < content.size()) {
            size_t ideal_end = current_start + target_size;
            if (ideal_end >= content.size()) {
                result.push_back({content.substr(current_start), 1, 1 + count_newlines(content.substr(current_start))});
                break;
            }

            auto it = std::lower_bound(breakpoints.begin(), breakpoints.end(), ideal_end);
            size_t actual_end = (it == breakpoints.begin()) ? ideal_end : *std::prev(it);
            
            if (actual_end < current_start + target_size / 2) {
                actual_end = ideal_end;
            }

            result.push_back({content.substr(current_start, actual_end - current_start), 1, 1 + count_newlines(content.substr(current_start, actual_end - current_start))});
            
            current_start = actual_end > overlap_size ? actual_end - overlap_size : 0;
            if (current_start >= actual_end && actual_end < content.size()) {
                current_start = actual_end;
            }
        }

        return result;
    }

    std::vector<TextChunker::SplitResult> TextChunker::recursive_split(const std::string& text, 
                                                                        int start_line, 
                                                                        size_t target_size, 
                                                                        const std::vector<std::string>& separators, 
                                                                        size_t sep_idx) {
        std::vector<SplitResult> results;
        if (text.size() <= target_size || sep_idx >= separators.size()) {
            results.push_back({text, start_line, start_line + count_newlines(text)});
            return results;
        }

        std::string sep = separators[sep_idx];
        std::vector<std::string> parts;
        size_t pos = 0;
        size_t next_pos;
        while ((next_pos = text.find(sep, pos)) != std::string::npos) {
            parts.push_back(text.substr(pos, next_pos - pos + sep.size()));
            pos = next_pos + sep.size();
        }
        if (pos < text.size()) {
            parts.push_back(text.substr(pos));
        }

        int current_line = start_line;
        for (const auto& part : parts) {
            if (part.size() > target_size) {
                auto sub_results = recursive_split(part, current_line, target_size, separators, sep_idx + 1);
                results.insert(results.end(), sub_results.begin(), sub_results.end());
            } else {
                results.push_back({part, current_line, current_line + count_newlines(part)});
            }
            current_line += count_newlines(part);
        }

        return results;
    }

}
