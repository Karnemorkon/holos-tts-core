#pragma once
#include <string>
#include <string_view>
#include <vector>
#include <cstdint>

namespace holos {

// Посимвольний токенізатор за словником із holos.pt (vocab.json).
// Порт CharTokenizer з app/tts_pipeline.py. M0: мінімальна заглушка, M1 — повний.
class CharTokenizer {
public:
    // vocab: список символів; id = індекс, невідомі символи пропускаються.
    explicit CharTokenizer(std::vector<std::string> vocab);
    std::vector<int64_t> operator()(std::string_view text) const;
    size_t vocab_size() const { return vocab_.size(); }

private:
    std::vector<std::string> vocab_;
};

} // namespace holos
