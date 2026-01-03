#include "librarian.hpp"
#include <hnswlib/hnswlib.h>
#include <iostream>

namespace kestr::engine {

    struct Librarian::Impl {
        hnswlib::L2Space space;
        std::unique_ptr<hnswlib::HierarchicalNSW<float>> alg_hnsw;

        Impl(size_t dim, size_t max_elements) : space(dim) {
            alg_hnsw = std::make_unique<hnswlib::HierarchicalNSW<float>>(&space, max_elements);
        }
    };

    Librarian::Librarian(size_t dim, size_t max_elements) : m_dim(dim) {
        m_impl = std::make_unique<Impl>(dim, max_elements);
    }

    Librarian::~Librarian() = default;

    void Librarian::add_item(size_t id, const std::vector<float>& vector) {
        if (vector.size() != m_dim) {
            std::cerr << "[Librarian] Error: Vector dimension mismatch. Expected " << m_dim << ", got " << vector.size() << "\n";
            return;
        }
        try {
            m_impl->alg_hnsw->addPoint(vector.data(), id);
        } catch (const std::exception& e) {
            std::cerr << "[Librarian] Error adding item: " << e.what() << "\n";
            // If full, we might need to resize, but for now we set max_elements high enough or let it fail.
        }
    }

    std::vector<size_t> Librarian::search(const std::vector<float>& query_vector, size_t k) {
        std::vector<size_t> results;
        if (query_vector.size() != m_dim) {
            std::cerr << "[Librarian] Error: Query dimension mismatch.\n";
            return results;
        }

        try {
            // searchKnn returns a priority queue of pairs <dist, label>
            auto pq = m_impl->alg_hnsw->searchKnn(query_vector.data(), k);
            
            // Extract labels (IDs)
            while (!pq.empty()) {
                results.push_back(pq.top().second);
                pq.pop();
            }
            // Result is furthest to nearest, so reverse it
            std::reverse(results.begin(), results.end());
        } catch (const std::exception& e) {
            std::cerr << "[Librarian] Search error: " << e.what() << "\n";
        }
        return results;
    }

    void Librarian::save(const std::filesystem::path& path) {
        try {
            m_impl->alg_hnsw->saveIndex(path.string());
        } catch (const std::exception& e) {
            std::cerr << "[Librarian] Save error: " << e.what() << "\n";
        }
    }

    void Librarian::load(const std::filesystem::path& path) {
        try {
            // Need to recreate the index object to load
            size_t max_elements = 10000; // Should ideally store this meta
            m_impl->alg_hnsw = std::make_unique<hnswlib::HierarchicalNSW<float>>(&m_impl->space, path.string(), false, max_elements);
        } catch (const std::exception& e) {
            std::cerr << "[Librarian] Load error: " << e.what() << "\n";
        }
    }

    size_t Librarian::count() const {
        return m_impl->alg_hnsw->cur_element_count;
    }

}
