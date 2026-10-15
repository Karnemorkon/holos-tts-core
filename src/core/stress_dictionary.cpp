#include "stress_dictionary.h"
#include "utf8.h"

#include <marisa/trie.h>
#include <marisa/agent.h>

#include <utf8proc.h>

#include <algorithm>
#include <stdexcept>

namespace holos {

namespace {

// Unicode lower: як Python word.lower() (кирилиця, латиниця тощо).
std::string unicode_lower(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        utf8proc_int32_t cp = 0;
        auto n = utf8proc_iterate(reinterpret_cast<const utf8proc_uint8_t*>(s.data()) + i,
                                  static_cast<utf8proc_ssize_t>(s.size() - i), &cp);
        if (n <= 0) { out.push_back(s[i]); ++i; continue; } // битий байт
        out += utf8::encode_u32(static_cast<uint32_t>(utf8proc_tolower(cp)));
        i += static_cast<size_t>(n);
    }
    return out;
}

// Unicode title: як Python word.title() — після не-літери верхній регістр,
// решта — нижній.
std::string unicode_title(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    size_t i = 0;
    bool boundary = true;
    while (i < s.size()) {
        utf8proc_int32_t cp = 0;
        auto n = utf8proc_iterate(reinterpret_cast<const utf8proc_uint8_t*>(s.data()) + i,
                                  static_cast<utf8proc_ssize_t>(s.size() - i), &cp);
        if (n <= 0) { out.push_back(s[i]); ++i; boundary = true; continue; }
        auto lc = utf8proc_tolower(cp);
        auto uc = utf8proc_toupper(cp);
        const bool cased = (lc != uc);
        out += utf8::encode_u32(static_cast<uint32_t>((cased && boundary) ? uc : lc));
        boundary = !cased;
        i += static_cast<size_t>(n);
    }
    return out;
}
constexpr char kTrieSep = '\xFF'; // роздільник word/payload у BytesTrie
constexpr char kRecSep  = '\xFF'; // Record-separator тегів
constexpr char kPosSep  = '\xFE'; // POS-separator тегів
constexpr const char* kAcute = "\u0301";

// Порт _parse_dictionary_value: байти payload -> позиції наголосу.
// Варіант A: >1 POS-варіантів -> порожній (без наголосу).
std::vector<int> parse_payload(std::string_view payload) {
    std::vector<std::string_view> records;
    size_t start = 0;
    while (true) {
        size_t sep = payload.find(kRecSep, start);
        if (sep == std::string_view::npos) { records.push_back(payload.substr(start)); break; }
        if (sep > start) records.push_back(payload.substr(start, sep - start));
        start = sep + 1;
    }
    records.erase(std::remove_if(records.begin(), records.end(),
                                 [](std::string_view r) { return r.empty(); }),
                  records.end());
    if (records.size() > 1) return {}; // неоднозначність -> варіант A (skip)
    if (records.empty()) return {};

    std::string_view rec = records[0];
    size_t psep = rec.find(kPosSep);
    std::string_view accent_bytes = (psep == std::string_view::npos) ? rec : rec.substr(0, psep);
    std::vector<int> pos;
    pos.reserve(accent_bytes.size());
    for (unsigned char b : accent_bytes) pos.push_back(int(b));
    return pos;
}
} // namespace

struct StressDictionary::Impl {
    marisa::Trie trie;
};

StressDictionary::StressDictionary(const std::string& trie_path) : impl_(new Impl) {
    try {
        impl_->trie.load(trie_path.c_str());
    } catch (const std::exception& e) {
        delete impl_; impl_ = nullptr;
        throw std::runtime_error("Не вдалося завантажити stress.trie: " + std::string(e.what()));
    }
}

StressDictionary::~StressDictionary() { delete impl_; }

bool StressDictionary::loaded() const { return impl_ && impl_->trie.num_keys() > 0; }
size_t StressDictionary::size() const { return impl_ ? impl_->trie.num_keys() : 0; }

std::vector<int> StressDictionary::accent_positions(std::string_view word) const {
    if (!impl_ || word.empty()) return {};
    // Python пробує base, base.lower(), base.title() (Unicode-регістр)
    const std::string lower = unicode_lower(word);
    const std::string title = unicode_title(word);

    const std::string candidates[3] = { std::string(word), lower, title };
    for (const std::string& w : candidates) {
        std::string q;
        q.reserve(w.size() + 1);
        q.append(w);
        q.push_back(kTrieSep);
        marisa::Agent agent;
        agent.set_query(q.c_str(), q.size());
        while (impl_->trie.predictive_search(agent)) {
            const char* k = agent.key().ptr();
            std::size_t klen = agent.key().length();
            if (klen <= q.size()) continue;
            std::string_view payload(k + q.size(), klen - q.size());
            auto pos = parse_payload(payload);
            if (!pos.empty() || payload.empty()) return pos;
        }
    }
    return {};
}

std::string StressDictionary::stress(std::string_view word) const {
    auto pos = accent_positions(word);
    if (pos.empty()) return std::string(word);
    // вставити U+0301 після кожної позиції (спадаючий порядок)
    std::sort(pos.begin(), pos.end(), std::greater<int>());
    std::string s(word);
    for (int p : pos) s = utf8::insert_at_cp(s, (size_t)p, kAcute);
    return s;
}

} // namespace holos
