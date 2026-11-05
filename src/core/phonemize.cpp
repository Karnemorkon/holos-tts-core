#include "phonemize.h"

#include "core/accentor.h"
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

// Голосний звук, на який взагалі може впасти наголос: а е є и і ї о у ю я
// (верхній і нижній регістр).
bool is_vowel_cp(uint32_t cp) {
    switch (cp) {
        case 0x0430: case 0x0435: case 0x0454: case 0x0438: case 0x0456:
        case 0x0457: case 0x043E: case 0x0443: case 0x044E: case 0x044F:
        case 0x0410: case 0x0415: case 0x0404: case 0x0418: case 0x0406:
        case 0x0407: case 0x041E: case 0x0423: case 0x042E: case 0x042F:
            return true;
        default:
            return false;
    }
}

bool has_vowel(std::string_view word) {
    size_t i = 0;
    while (i < word.size()) {
        size_t j = i;
        if (is_vowel_cp(decode_cp(word, j))) return true;
        i = j;
    }
    return false;
}

// Авторський accentor приймає токен лише якщо його ПЕРШИЙ символ входить у
// WORD_MATCH_RULES: [а-яА-ЯіїІЇ'’`]. Через це слова, що починаються з є/ґ
// (Європа, ґанок), у Python взагалі не отримують neural-наголосу. Повторюємо,
// щоб не гіршати за автора: для таких слів лишається тільки словник.
bool accentor_accepts(std::string_view word) {
    if (word.empty() || !has_vowel(word)) return false;
    size_t i = 0;
    uint32_t cp = decode_cp(word, i);
    if (cp >= 0x0430 && cp <= 0x044F) return true;      // а-я
    if (cp >= 0x0410 && cp <= 0x042F) return true;      // А-Я
    return cp == 0x0456 || cp == 0x0406 ||             // і І
           cp == 0x0457 || cp == 0x0407 ||             // ї Ї
           cp == '\'' || cp == 0x2019 || cp == 0x02BC; // апострофи
}

// Codepoint-позиція першого U+0301 у слові; -1 якщо наголосу немає.
int acute_position(std::string_view s) {
    size_t i = 0;
    int cp_index = 0;
    while (i < s.size()) {
        if (decode_cp(s, i) == 0x0301) return cp_index;
        ++cp_index;
    }
    return -1;
}

} // namespace

// Словниковий + neural наголос: словник спершу; якщо без наголосу -> accentor.
std::string stress_word(const StressDictionary& dict, const Accentor* accentor,
                        std::string_view word) {
    const std::vector<int> chosen = dict.accent_positions(word);
    if (!chosen.empty()) return dict.stress_at(word, chosen);

    // Словник не дав наголосу: або OOV, або справжня омонімія.
    const auto variants = dict.accent_variants(word);
    // Слова без голосних (в, з, й, є) не мають наголосу: accentor у Python їх
    // пропускає (WORD_MATCH_RULES вимагає голосний), інакше він ставить
    // наголос у довільне місце ("в́"). accentor_accepts повторює й решту
    // обмежень авторського токенайзера (перший символ, є/ґ-ініціальні слова).
    std::string neural;
    int neural_pos = -1;
    if (accentor && accentor->loaded() && accentor_accepts(word)) {
        neural = accentor->stress(word);
        neural_pos = acute_position(neural);
    }
    if (variants.empty()) // OOV: довіряємо accentor
        return neural_pos >= 0 ? neural : std::string(word);

    // Омонім: accentor часто вгадує потрібне читання, але іноді ставить
    // наголос на склад, якого немає в жодному словниковому записі. Приймаємо
    // його лише тоді, коли позиція валідна; інакше — перший запис словника.
    if (neural_pos >= 0) {
        for (const auto& v : variants)
            for (int a : v)
                if (a == neural_pos) return neural;
    }
    return dict.stress_at(word, variants.front());
}

// Словниковий+neural наголос по тексту: кожне слово -> dict.stress (+ accentor fallback).
std::string stress_text(const StressDictionary& dict, const Accentor* accentor,
                        std::string_view text) {
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
            out += stress_word(dict, accentor, text.substr(start, i - start));
        } else {
            out.append(text, i, j - i);
            i = j;
        }
    }
    return out;
}

Phonemizer::Phonemizer(const std::string& stress_trie_path, const std::string& accentor_onnx)
    : dict_(stress_trie_path) {
    if (!accentor_onnx.empty()) {
        try {
            accentor_ = std::make_unique<Accentor>(accentor_onnx);
        } catch (...) {
            // accentor опційний — працюємо без нього
            accentor_.reset();
        }
    }
}

Phonemizer::~Phonemizer() = default;

std::vector<std::string> Phonemizer::phonemize(const std::string& text, bool raw_stress) const {
    std::vector<std::string> result;
    for (const std::string& part : TextPrep::split_to_parts(text, true)) {
        std::string norm = TextPrep::normalize_part(part);
        if (norm.empty()) continue;
        std::string stressed = raw_stress ? norm : stress_text(dict_, accentor_.get(), norm);
        std::string ph = ipa::transcribe(stressed);
        if (!ph.empty()) result.push_back(std::move(ph));
    }
    return result;
}

} // namespace holos
