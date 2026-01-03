#pragma once

#include <vector>
#include <string>
#include <memory>
#include <filesystem>

namespace kestr::engine {

    class Librarian {
    public:
        Librarian(size_t dim, size_t max_elements = 10000);
        ~Librarian();

        /**
         * @brief Adds a vector to the index.
         * @param id The unique ID of the chunk (from database).
         * @param vector The embedding vector.
         */
        void add_item(size_t id, const std::vector<float>& vector);

        /**
         * @brief Searches for the nearest neighbors.
         * @param query_vector The query vector.
         * @param k Number of results to return.
         * @return List of chunk IDs.
         */
        std::vector<size_t> search(const std::vector<float>& query_vector, size_t k = 5);

        /**
         * @brief Persists the index to disk.
         */
        void save(const std::filesystem::path& path);

        /**
         * @brief Loads the index from disk.
         */
        void load(const std::filesystem::path& path);

        /**
         * @brief Returns current number of elements.
         */
        size_t count() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
        size_t m_dim;
    };

}
