#include "stress_dictionary.h"
#include "text_prep.h"
#include "utf8.h"

#include <marisa/trie.h>
#include <marisa/agent.h>

#include <utf8proc.h>

#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <unordered_map>

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

struct Payload {
    std::vector<int> chosen;                 // визначений наголос (порожньо — омонім)
    std::vector<std::vector<int>> variants;  // усі записи (потрібні для перевірки fallback)
};

// Порт _parse_dictionary_value: байти payload -> позиції наголосу.
// Кілька записів на слово — це омоніми/омографи, у яких наголос залежить від
// частини мови (Python резолвить його через Stanza-теги). Ми тегів не маємо,
// тому неоднозначність лишаємо без наголосу (як on_ambiguity=skip), АЛЕ лише
// справжню: якщо всі записи вказують однакові позиції, наголос від POS не
// залежить і його можна брати (це 98.7% багатозаписних слів).
Payload parse_payload(std::string_view payload) {
    struct Variant {
        std::vector<int> pos;
        bool has_tags;
    };
    std::vector<Variant> variants;
    size_t start = 0;
    while (start <= payload.size()) {
        size_t sep = payload.find(kRecSep, start);
        size_t end = (sep == std::string_view::npos) ? payload.size() : sep;
        std::string_view rec = payload.substr(start, end - start);
        if (!rec.empty()) {
            size_t psep = rec.find(kPosSep);
            std::string_view accent_bytes =
                (psep == std::string_view::npos) ? rec : rec.substr(0, psep);
            std::vector<int> pos;
            pos.reserve(accent_bytes.size());
            for (unsigned char b : accent_bytes) pos.push_back(int(b));
            variants.push_back({std::move(pos), psep != std::string_view::npos && psep + 1 < rec.size()});
        }
        if (sep == std::string_view::npos) break;
        start = sep + 1;
    }

    Payload out;
    if (variants.empty()) return out;
    out.variants.reserve(variants.size());
    for (const auto& v : variants) out.variants.push_back(v.pos);

    bool same = true;
    for (const auto& v : variants)
        if (v.pos != variants.front().pos) { same = false; break; }
    if (same) { out.chosen = variants.front().pos; return out; } // POS не впливає

    // Записи без тегів — читання, не обмежене частиною мови (зазвичай
    // займенник/службове слово, напр. вона́/вони́). Python вибирає його, коли
    // парс не збігається з жодним розміченим записом; без POS це найкращий
    // доступний сигнал, тож беремо його, якщо такі записи не суперечать один одному.
    std::vector<int> unconstrained;
    bool have = false, conflict = false;
    for (const auto& v : variants) {
        if (v.has_tags) continue;
        if (!have) { unconstrained = v.pos; have = true; }
        else if (v.pos != unconstrained) conflict = true;
    }
    if (have && !conflict) out.chosen = std::move(unconstrained);
    return out; // без chosen — справжня омонімія, слово піде в accentor
}

// Словник містить лише ASCII-апостроф ('); у текстах трапляються типографські
// ʼ (U+02BC) і ’ (U+2019). Для пошуку згортаємо їх до ASCII — позиції наголосу
// від цього не зміщуються (заміна 1 codepoint -> 1 byte).
std::string fold_apostrophes(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == 0xCA && i + 1 < s.size() && static_cast<unsigned char>(s[i + 1]) == 0xBC) {
            out.push_back('\''); i += 2;            // U+02BC
        } else if (c == 0xE2 && i + 2 < s.size() &&
                   static_cast<unsigned char>(s[i + 1]) == 0x80 &&
                   static_cast<unsigned char>(s[i + 2]) == 0x99) {
            out.push_back('\''); i += 3;            // U+2019
        } else {
            out.push_back(s[i]); ++i;
        }
    }
    return out;
}

// Пошук ключа "<word>\xFF<payload>" у trie. false — ключа немає (можна пробувати
// інший варіант регістру/апострофа); true — ключ є, payload розібрано.
bool lookup_key(const marisa::Trie& trie, const std::string& word, Payload& out) {
    std::string q;
    q.reserve(word.size() + 1);
    q.append(word);
    q.push_back(kTrieSep);
    marisa::Agent agent;
    agent.set_query(q.c_str(), q.size());
    while (trie.predictive_search(agent)) {
        const char* k = agent.key().ptr();
        std::size_t klen = agent.key().length();
        if (klen <= q.size()) continue;
        out = parse_payload(std::string_view(k + q.size(), klen - q.size()));
        return true;
    }
    return false;
}

// Спільний пошук: точне слово, lower, title і (за потреби) згорнуті апострофи.
bool find_payload(const marisa::Trie& trie, std::string_view word, Payload& out) {
    if (word.empty()) return false;
    std::vector<std::string> candidates;
    candidates.reserve(6);
    candidates.push_back(std::string(word));
    candidates.push_back(unicode_lower(word));
    candidates.push_back(unicode_title(word));
    if (word.find('\u02BC') != std::string_view::npos ||
        word.find('\u2019') != std::string_view::npos) {
        const std::string folded = fold_apostrophes(word);
        candidates.push_back(folded);
        candidates.push_back(unicode_lower(folded));
        candidates.push_back(unicode_title(folded));
    }
    for (const std::string& w : candidates)
        if (lookup_key(trie, w, out)) return true;
    return false;
}

// Обрізка пробілів по краях рядка.
std::string trim(std::string_view s) {
    size_t b = s.find_first_not_of(" \t\r");
    if (b == std::string_view::npos) return "";
    size_t e = s.find_last_not_of(" \t\r");
    return std::string(s.substr(b, e - b + 1));
}

// Ключ словника користувача: без U+0301, згорнуті апострофи, lowercase —
// щоб збігався з пошуком у stress.trie (lower/title/апострофи).
std::string user_key(std::string_view stressed_word) {
    std::string bare;
    bare.reserve(stressed_word.size());
    size_t i = 0;
    while (i < stressed_word.size()) {
        size_t start = i;
        utf8proc_int32_t cp = 0;
        auto n = utf8proc_iterate(reinterpret_cast<const utf8proc_uint8_t*>(stressed_word.data()) + i,
                                  static_cast<utf8proc_ssize_t>(stressed_word.size() - i), &cp);
        if (n <= 0) { bare.push_back(stressed_word[i]); ++i; continue; }
        if (cp != 0x0301) bare.append(stressed_word.substr(start, static_cast<size_t>(n)));
        i += static_cast<size_t>(n);
    }
    return unicode_lower(fold_apostrophes(bare));
}

// Завантаження словника користувача (текстовий файл): один запис на рядок
// у форматі «слово+» ('+' після голосної, на якій наголос) або слово з уже
// вставленим U+0301. Рядки з '#' (коментарі) та порожні — ігноруються;
// запис без позначки наголосу пропускається.
void load_user_file(const std::string& path,
                    std::unordered_map<std::string, std::vector<int>>& out) {
    std::ifstream f(path);
    if (!f) return;
    std::string line;
    while (std::getline(f, line)) {
        if (size_t h = line.find('#'); h != std::string::npos) line.erase(h);
        std::string token = trim(line);
        if (token.empty()) continue;
        if (token.find_first_of(" \t") != std::string::npos) continue; // одне слово на рядок
        // '+' -> U+0301 (та сама «прощаюча» обробка, що й у тексті).
        std::string stressed = TextPrep::apply_stress_markup(token);
        // Позиція наголосу = codepoint-індекс U+0301 — та сама конвенція, що в trie
        // (stress_at вставляє U+0301 ПЕРЕД цією позицією, тобто одразу після голосного).
        int pos = -1;
        size_t i = 0;
        int cp_idx = 0;
        while (i < stressed.size()) {
            utf8proc_int32_t cp = 0;
            auto n = utf8proc_iterate(reinterpret_cast<const utf8proc_uint8_t*>(stressed.data()) + i,
                                      static_cast<utf8proc_ssize_t>(stressed.size() - i), &cp);
            if (n <= 0) { ++i; ++cp_idx; continue; }
            if (cp == 0x0301) { pos = cp_idx; break; }
            i += static_cast<size_t>(n);
            ++cp_idx;
        }
        if (pos < 0) continue; // без наголосу — не додаємо
        out[user_key(stressed)] = {pos};
    }
}

} // namespace

struct StressDictionary::Impl {
    marisa::Trie trie;
    // Словник користувача (stress_user.txt): ключ = lower+апострофи, значення =
    // codepoint-позиції наголосу (конвенція як у trie). Перевіряється ПЕРЕД trie.
    std::unordered_map<std::string, std::vector<int>> user;
};

StressDictionary::StressDictionary(const std::string& trie_path,
                                   const std::string& user_path) : impl_(new Impl) {
    try {
        impl_->trie.load(trie_path.c_str());
    } catch (const std::exception& e) {
        delete impl_; impl_ = nullptr;
        throw std::runtime_error("Не вдалося завантажити stress.trie: " + std::string(e.what()));
    }
    if (!user_path.empty()) load_user_file(user_path, impl_->user);
}

StressDictionary::~StressDictionary() { delete impl_; }

bool StressDictionary::loaded() const { return impl_ && impl_->trie.num_keys() > 0; }
size_t StressDictionary::size() const { return impl_ ? impl_->trie.num_keys() : 0; }
size_t StressDictionary::user_size() const { return impl_ ? impl_->user.size() : 0; }

const std::vector<int>* StressDictionary::user_find(std::string_view word) const {
    if (!impl_ || impl_->user.empty() || word.empty()) return nullptr;
    auto it = impl_->user.find(unicode_lower(fold_apostrophes(word)));
    return it == impl_->user.end() ? nullptr : &it->second;
}

std::vector<std::vector<int>> StressDictionary::accent_variants(std::string_view word) const {
    if (!impl_) return {};
    if (const auto* p = user_find(word)) return {*p}; // словник користувача: один запис
    Payload p2;
    if (!find_payload(impl_->trie, word, p2)) return {};
    return p2.variants;
}

std::vector<int> StressDictionary::accent_positions(std::string_view word) const {
    if (!impl_) return {};
    if (const auto* p = user_find(word)) return *p; // пріоритет користувача
    Payload p2;
    if (!find_payload(impl_->trie, word, p2)) return {};
    return p2.chosen;
}

std::string StressDictionary::stress_at(std::string_view word,
                                        const std::vector<int>& positions) const {
    if (positions.empty()) return std::string(word);
    // вставити U+0301 після кожної позиції (спадаючий порядок)
    std::vector<int> pos = positions;
    std::sort(pos.begin(), pos.end(), std::greater<int>());
    std::string s(word);
    for (int p : pos) s = utf8::insert_at_cp(s, (size_t)p, kAcute);
    return s;
}

std::string StressDictionary::stress(std::string_view word) const {
    return stress_at(word, accent_positions(word));
}

} // namespace holos
