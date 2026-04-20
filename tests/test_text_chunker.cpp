#include <iostream>
#include <cassert>
#include <vector>
#include <string>
#include "engine/text_chunker.hpp"

using namespace kestr::engine;

void test_simple_split() {
    std::cout << "Testing simple split..." << std::endl;
    std::string text = "Paragraph 1\n\nParagraph 2\n\nParagraph 3";
    // Target size small enough to split by double newlines
    auto chunks = TextChunker::chunk(text, 15, 0.0f);
    
    assert(chunks.size() >= 3);
    assert(chunks[0].content.find("Paragraph 1") != std::string::npos);
    assert(chunks[1].content.find("Paragraph 2") != std::string::npos);
    std::cout << "Simple split passed!" << std::endl;
}

void test_overlap() {
    std::cout << "Testing overlap..." << std::endl;
    std::string text = "This is a long sentence that should be split with some overlap between chunks.";
    size_t target_size = 20;
    float overlap_percent = 0.25f; // 5 chars overlap
    
    auto chunks = TextChunker::chunk(text, target_size, overlap_percent);
    
    assert(chunks.size() > 1);
    for (size_t i = 0; i < chunks.size() - 1; ++i) {
        // Find overlap between chunks[i] and chunks[i+1]
        const std::string& c1 = chunks[i].content;
        const std::string& c2 = chunks[i+1].content;
        
        // The end of c1 should be the start of c2
        size_t overlap_len = static_cast<size_t>(target_size * overlap_percent);
        std::string suffix = c1.substr(c1.size() - overlap_len);
        assert(c2.substr(0, overlap_len) == suffix);
    }
    std::cout << "Overlap test passed!" << std::endl;
}

void test_hierarchy() {
    std::cout << "Testing hierarchy (newlines vs spaces)..." << std::endl;
    std::string text = "Line 1\nLine 2. More words here.";
    // Should split at \n first if target_size is small
    auto chunks = TextChunker::chunk(text, 10, 0.0f);
    
    assert(chunks.size() >= 2);
    assert(chunks[0].content.find("Line 1") != std::string::npos);
    assert(chunks[0].content.find("Line 2") == std::string::npos);
    std::cout << "Hierarchy test passed!" << std::endl;
}

int main() {
    try {
        test_simple_split();
        test_overlap();
        test_hierarchy();
        std::cout << "All TextChunker tests passed!" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Test failed: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
