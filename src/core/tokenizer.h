#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace holos {

// Посимвольний токенізатор за фонемним словником (vocab.json).
// Порт CharTokenizer з app/tts_pipeline.py: id = індекс символу у vocab,
// невідомі символи пропускаються. Ітерація — по codepoint (як Python str).
class CharTokenizer {
public:
    CharTokenizer() = default;
    // vocab: список фонемних символів; entry -> останній індекс (як Python dict).
    explicit CharTokenizer(const std::vector<std::string>& vocab);
    // Завантаження з vocab.json (JSON-масив рядків).
    static CharTokenizer from_vocab_json(const std::string& path);

    std::vector<int64_t> operator()(std::string_view text) const;
    size_t vocab_size() const { return vocab_.size(); }

private:
    std::vector<std::string> vocab_;
    std::unordered_map<std::string, int64_t> index_; // codepoint-стрінг -> id (last-wins)
};

} // namespace holos
