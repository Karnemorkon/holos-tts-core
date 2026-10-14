#pragma once
#include <string>
#include <string_view>
#include <vector>
#include <cstdint>

// Мінімальні UTF-8 утиліти: український текст 2 байти/символ, але реалізація
// загальна. Позиції наголосу у словнику — індекси CODEPOINT (не байти).
namespace holos::utf8 {

// Кількість codepoint'ів
inline size_t len(std::string_view s) {
    size_t n = 0;
    for (unsigned char c : s) n += (c & 0xC0) != 0x80;
    return n;
}

// Байтовий зсув до codepoint-індексу [0, len); повертає s.size() якщо поза межами
inline size_t byte_offset(std::string_view s, size_t cp_index) {
    size_t n = 0, bytes = 0;
    for (unsigned char c : s) {
        if ((c & 0xC0) == 0x80) { bytes++; continue; } // continuation
        if (n == cp_index) return bytes;
        n++; bytes++;
    }
    return cp_index == n ? bytes : s.size();
}

// Вставити `ins` ПЕРЕД codepoint-позицією pos (0-based). pos >= len -> в кінець.
// Відповідає Python s[:pos] + ins + s[pos:] (позиція наголосу зі словника).
inline std::string insert_at_cp(std::string_view s, size_t pos, std::string_view ins) {
    size_t off = byte_offset(s, pos);
    std::string out;
    out.reserve(s.size() + ins.size());
    out.append(s.data(), off);
    out.append(ins);
    out.append(s.data() + off, s.size() - off);
    return out;
}

// UTF-8 encode codepoint (для U+0301)
inline std::string encode_u32(uint32_t cp) {
    std::string o;
    if (cp < 0x80) o += char(cp);
    else if (cp < 0x800) { o += char(0xC0 | (cp >> 6)); o += char(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { o += char(0xE0 | (cp >> 12)); o += char(0x80 | ((cp >> 6) & 0x3F)); o += char(0x80 | (cp & 0x3F)); }
    else { o += char(0xF0 | (cp >> 18)); o += char(0x80 | ((cp >> 12) & 0x3F)); o += char(0x80 | ((cp >> 6) & 0x3F)); o += char(0x80 | (cp & 0x3F)); }
    return o;
}

} // namespace holos::utf8
