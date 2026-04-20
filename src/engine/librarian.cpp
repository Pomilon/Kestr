#include "librarian.hpp"
#include <hnswlib/hnswlib.h>
#include <iostream>

namespace kestr::engine {

    struct Librarian::Impl {
        hnswlib::L2Space space;
        std::unique_ptr<hnswlib::HierarchicalNSW<float>> alg_hnsw;
        size_t max_elements_cached;

        Impl(size_t dim, size_t max_elements) : space(dim), max_elements_cached(max_elements) {
            // Optimized HNSW parameters: M=16, ef_construction=200
            alg_hnsw = std::make_unique<hnswlib::HierarchicalNSW<float>>(&space, max_elements, 16, 200);
        }
    };

    Librarian::Librarian(size_t dim, size_t max_elements) : m_dim(dim) {
        m_impl = std::make_unique<Impl>(dim, max_elements);
    }

    Librarian::~Librarian() = default;

    void Librarian::add_item(size_t id, const std::vector<float>& vector) {
        if (vector.size() != m_dim) return;
        try {
            // Use replace_if_exists = true to handle updates gracefully
            m_impl->alg_hnsw->addPoint(vector.data(), id, true);
        } catch (...) {
            // Ignore if full or error to maintain stability
        }
    }

    void Librarian::remove_item(size_t id) {
        try {
            m_impl->alg_hnsw->markDelete(id);
        } catch (...) {}
    }

    std::vector<size_t> Librarian::search(const std::vector<float>& query_vector, size_t k) {
        std::vector<size_t> results;
        if (query_vector.size() != m_dim) return results;

        try {
            m_impl->alg_hnsw->setEf(100); // Higher ef during search for better recall
            auto pq = m_impl->alg_hnsw->searchKnn(query_vector.data(), k);
            
            while (!pq.empty()) {
                results.push_back(pq.top().second);
                pq.pop();
            }
            std::reverse(results.begin(), results.end());
        } catch (...) {}
        return results;
    }

    void Librarian::save(const std::filesystem::path& path) {
        try { m_impl->alg_hnsw->saveIndex(path.string()); } catch (...) {}
    }

    void Librarian::load(const std::filesystem::path& path) {
        try {
            m_impl->alg_hnsw = std::make_unique<hnswlib::HierarchicalNSW<float>>(&m_impl->space, path.string(), false, m_impl->max_elements_cached);
        } catch (...) {}
    }

    size_t Librarian::count() const {
        return m_impl->alg_hnsw->cur_element_count;
    }

}
