#include "tokenizer.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <stdexcept>

namespace holos {

namespace {

// Розмір UTF-8 codepoint, що починається з байта `c` (1..4).
size_t cp_len(unsigned char c) {
    if (c < 0x80) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    return 4;
}

} // namespace

CharTokenizer::CharTokenizer(const std::vector<std::string>& vocab) : vocab_(vocab) {
    index_.reserve(vocab.size());
    for (size_t i = 0; i < vocab.size(); ++i)
        index_[vocab[i]] = static_cast<int64_t>(i); // last-wins, як у Python dict
}

CharTokenizer CharTokenizer::from_vocab_json(const std::string& path) {
    std::ifstream f(path);
    if (!f) throw std::runtime_error("cannot open vocab.json: " + path);
    nlohmann::json j;
    f >> j;
    if (!j.is_array()) throw std::runtime_error("vocab.json: expected a JSON array");
    return CharTokenizer(j.get<std::vector<std::string>>());
}

// Ітерація по codepoint (як `for ch in text` у Python); невідомі пропускаються.
std::vector<int64_t> CharTokenizer::operator()(std::string_view text) const {
    std::vector<int64_t> ids;
    size_t i = 0;
    while (i < text.size()) {
        size_t n = cp_len(static_cast<unsigned char>(text[i]));
        if (i + n > text.size()) n = 1; // неповний codepoint — безпека
        auto it = index_.find(std::string(text.substr(i, n)));
        if (it != index_.end()) ids.push_back(it->second);
        i += n;
    }
    return ids;
}

} // namespace holos
