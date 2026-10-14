#include "text_prep.h"
#include "pcre2_util.h"

#include <utf8proc.h>

#include <cstdlib>
#include <string>

namespace holos {

namespace {

using util::re_sub;

// NFKC-нормалізація (парність з Python unicodedata.normalize('NFKC', ...)).
std::string nfkc(const std::string& s) {
    utf8proc_uint8_t* out = nullptr;
    utf8proc_ssize_t n = utf8proc_map(
        reinterpret_cast<const utf8proc_uint8_t*>(s.data()),
        static_cast<utf8proc_ssize_t>(s.size()), &out,
        static_cast<utf8proc_option_t>(UTF8PROC_NULLTERM | UTF8PROC_STABLE |
                                       UTF8PROC_COMPAT | UTF8PROC_COMPOSE));
    if (n < 0 || !out) return s;
    std::string r(reinterpret_cast<char*>(out), static_cast<size_t>(n));
    std::free(out);
    return r;
}

// Останній codepoint рядка як UTF-8 (для перевірки термінальної пунктуації).
std::string last_cp(const std::string& s) {
    if (s.empty()) return "";
    size_t i = s.size() - 1;
    while (i > 0 && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) --i;
    return s.substr(i);
}

} // namespace

std::string TextPrep::collapse_newlines(std::string_view text) {
    std::string s(text);
    s = re_sub("(\\w+[^.,!:\\-])\\n", "$1. ", s);
    size_t p = 0;
    while ((p = s.find('\n', p)) != std::string::npos) { s.replace(p, 1, " "); ++p; }
    return s;
}

std::vector<std::string> TextPrep::split_to_parts(std::string_view text, bool group) {
    std::string t = collapse_newlines(text);
    std::vector<std::string> parts;
    // Розкладаємо на codepoints (Python лічить символи, не байти).
    std::vector<std::string> cps;
    for (size_t i = 0; i < t.size(); ) {
        unsigned char c = static_cast<unsigned char>(t[i]);
        size_t n = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
        cps.push_back(t.substr(i, n));
        i += n;
    }
    std::string tail;
    int tail_cp = 0;
    size_t last = cps.empty() ? 0 : cps.size() - 1;
    for (size_t pos = 0; pos < cps.size(); ++pos) {
        const std::string& ch = cps[pos];
        tail += ch; ++tail_cp;
        bool is_break = (ch == "." || ch == "!" || ch == "?" || ch == ":");
        if (is_break && pos < last && cps[pos + 1] == " ") {
            if (group && tail_cp <= 100) continue;
            parts.push_back(tail);
            tail.clear(); tail_cp = 0;
        }
    }
    parts.push_back(tail);
    return parts;
}

std::string TextPrep::strip_wrappers(std::string_view text) {
    std::string s(text);
    s = re_sub("^\\s+", "", s);
    s = re_sub("\\s+$", "", s);
    s = re_sub("\"", "", s);
    return s;
}

std::string TextPrep::apply_stress_markup(std::string_view text) {
    std::string s(text);
    size_t p = 0;
    while ((p = s.find('+', p)) != std::string::npos) { s.replace(p, 1, "\u0301"); p += 2; }
    return nfkc(s);
}

std::string TextPrep::standardize_dashes(std::string_view text) {
    const std::string dash_class =
        "[\u1806\u2010\u2011\u2012\u2013\u2014\u2015\u207B\u208B\u2212\u2E3A\u2E3B]";
    return re_sub(dash_class, "-", std::string(text));
}

std::string TextPrep::ensure_terminal_punct(std::string text) {
    if (text.empty()) return text;
    std::string last = last_cp(text);
    if (last != "." && last != "!" && last != "?" && last != ":" && last != "-")
        text += ".";
    return text;
}

std::string TextPrep::normalize_part(std::string_view part) {
    std::string s = strip_wrappers(part);
    if (s.empty()) return "";
    s = apply_stress_markup(s);
    s = standardize_dashes(s);
    s = ensure_terminal_punct(s);
    size_t p = 0;
    while ((p = s.find(" - ", p)) != std::string::npos) { s.replace(p, 3, ": "); p += 2; }
    return s;
}

} // namespace holos
