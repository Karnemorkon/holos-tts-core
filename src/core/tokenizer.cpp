#include "tokenizer.h"

namespace holos {

CharTokenizer::CharTokenizer(std::vector<std::string> vocab) : vocab_(std::move(vocab)) {}

// M0-заглушка: декодування UTF-8 у індекси за vocab; M1 — точний порт.
std::vector<int64_t> CharTokenizer::operator()(std::string_view text) const {
    std::vector<int64_t> ids;
    size_t i = 0;
    while (i < text.size()) {
        unsigned char c = text[i];
        size_t adv = 1;
        uint32_t cp = c;
        if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; adv = 2; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; adv = 3; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; adv = 4; }
        for (size_t k = 1; k < adv && i + k < text.size(); k++)
            cp = (cp << 6) | (text[i + k] & 0x3F);
        // пошук символу у vocab (спрощено: перший символ рядка)
        for (size_t v = 0; v < vocab_.size(); v++) {
            if (!vocab_[v].empty() && (unsigned char)vocab_[v][0] == c) { ids.push_back((int64_t)v); break; }
        }
        i += adv;
    }
    return ids;
}

} // namespace holos
