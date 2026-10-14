#pragma once
// Спільний PCRE2-хелпер: компіляція UTF-8/UCP-патернів та re.sub-еквівалент
// (ручна побудова заміни; неучасть опційної групи -> порожній рядок, як Python re).

#include <cctype>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef PCRE2_CODE_UNIT_WIDTH
#define PCRE2_CODE_UNIT_WIDTH 8
#endif
#include <pcre2.h>

namespace holos::util {

struct Pcre2Deleter { void operator()(pcre2_code* c) const { if (c) pcre2_code_free(c); } };
using Pcre2Ptr = std::unique_ptr<pcre2_code, Pcre2Deleter>;

inline Pcre2Ptr compile(const std::string& pattern) {
    int err = 0; PCRE2_SIZE off = 0;
    pcre2_code* re = pcre2_compile(reinterpret_cast<PCRE2_SPTR>(pattern.c_str()),
                                   PCRE2_ZERO_TERMINATED, PCRE2_UTF | PCRE2_UCP,
                                   &err, &off, nullptr);
    if (!re) {
        PCRE2_UCHAR buf[256];
        pcre2_get_error_message(err, buf, sizeof(buf));
        throw std::runtime_error("pcre2 compile @" + std::to_string(off) + ": " +
                                 reinterpret_cast<const char*>(buf));
    }
    return Pcre2Ptr(re);
}

namespace detail {
// Appends replacement for one match; `tmpl` uses $1,$2,.. (unset group -> empty).
inline void append_repl(std::string& out, const std::string& tmpl, const std::string& subject,
                        PCRE2_SIZE* ov, int ngroups) {
    for (size_t i = 0; i < tmpl.size(); ) {
        if (tmpl[i] == '$' && i + 1 < tmpl.size() &&
            std::isdigit(static_cast<unsigned char>(tmpl[i + 1]))) {
            int g = 0; size_t j = i + 1;
            while (j < tmpl.size() && std::isdigit(static_cast<unsigned char>(tmpl[j]))) {
                g = g * 10 + (tmpl[j] - '0'); ++j;
            }
            if (g >= 1 && g <= ngroups && ov[2 * g] != PCRE2_UNSET)
                out.append(subject, ov[2 * g], ov[2 * g + 1] - ov[2 * g]);
            i = j;
        } else {
            out.push_back(tmpl[i]); ++i;
        }
    }
}
} // namespace detail

// Python re.sub(pattern, repl, subject) equivalent (global, non-overlapping).
inline std::string re_sub(const std::string& pattern, const std::string& repl,
                          const std::string& subject) {
    Pcre2Ptr re = compile(pattern);
    int ngroups = 0;
    pcre2_pattern_info(re.get(), PCRE2_INFO_CAPTURECOUNT, &ngroups);
    pcre2_match_data* md = pcre2_match_data_create_from_pattern(re.get(), nullptr);
    std::string out; out.reserve(subject.size());
    PCRE2_SIZE off = 0, len = subject.size();
    while (off <= len) {
        int rc = pcre2_match(re.get(), reinterpret_cast<PCRE2_SPTR>(subject.data()),
                             len, off, 0, md, nullptr);
        if (rc <= 0) break;
        PCRE2_SIZE* ov = pcre2_get_ovector_pointer(md);
        PCRE2_SIZE ms = ov[0], me = ov[1];
        out.append(subject, off, ms - off);
        detail::append_repl(out, repl, subject, ov, ngroups);
        if (me == ms) {
            if (me >= len) { off = len + 1; break; }
            unsigned char c = static_cast<unsigned char>(subject[me]);
            size_t n = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
            out.append(subject, me, n);
            off = me + n;
        } else {
            off = me;
        }
    }
    pcre2_match_data_free(md);
    out.append(subject, off, len - off);
    return out;
}

} // namespace holos::util
