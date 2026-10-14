#include "phonemize.h"

#include "core/ipa_uk.h"
#include "core/text_prep.h"

namespace holos {
namespace {

// Розмір UTF-8 codepoint, що починається з байта `c` (1..4).
size_t cp_len(unsigned char c) {
    if (c < 0x80) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    return 4;
}

// Декодує codepoint, що починається в s[i]; зсуває i на його кінець.
uint32_t decode_cp(std::string_view s, size_t& i) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    uint32_t cp; size_t n;
    if (c < 0x80) { cp = c; n = 1; }
    else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; n = 2; }
    else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; n = 3; }
    else { cp = c & 0x07; n = 4; }
    for (size_t k = 1; k < n && i + k < s.size(); k++)
        cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
    i += n;
    return cp;
}

// Слово: українські кириличні літери + апострофи (ʼ, ’, ').
bool is_word_cp(uint32_t cp) {
    return (cp >= 0x0400 && cp <= 0x04FF) || cp == 0x02BC || cp == 0x2019 || cp == '\'';
}

// Словниковий наголос по тексту (варіант A): кожне слово -> dict.stress.
std::string stress_text(const StressDictionary& dict, std::string_view text) {
    std::string out; out.reserve(text.size());
    size_t i = 0, n = text.size();
    while (i < n) {
        size_t j = i;
        uint32_t cp = decode_cp(text, j); // j = кінець цього codepoint
        if (is_word_cp(cp)) {
            size_t start = i;
            while (i < n) {
                size_t k = i;
                uint32_t c2 = decode_cp(text, k);
                if (!is_word_cp(c2)) break;
                i = k;
            }
            out += dict.stress(text.substr(start, i - start));
        } else {
            out.append(text, i, j - i);
            i = j;
        }
    }
    return out;
}

} // namespace

Phonemizer::Phonemizer(const std::string& stress_trie_path) : dict_(stress_trie_path) {}

std::vector<std::string> Phonemizer::phonemize(const std::string& text, bool raw_stress) const {
    std::vector<std::string> result;
    for (const std::string& part : TextPrep::split_to_parts(text, true)) {
        std::string norm = TextPrep::normalize_part(part);
        if (norm.empty()) continue;
        std::string stressed = raw_stress ? norm : stress_text(dict_, norm);
        std::string ph = ipa::transcribe(stressed);
        if (!ph.empty()) result.push_back(std::move(ph));
    }
    return result;
}

} // namespace holos
