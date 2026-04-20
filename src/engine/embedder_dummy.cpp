#include "embedder.hpp"

namespace kestr::engine {

    class DummyEmbedder : public Embedder {
    public:
        std::vector<float> embed(const std::string&) override {
            return std::vector<float>(384, 0.0f);
        }
        size_t dimension() const override {
            return 384;
        }
    };

    std::unique_ptr<Embedder> create_dummy_embedder() {
        return std::make_unique<DummyEmbedder>();
    }

}
