#include "ipa_uk.h"
#include "pcre2_util.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace holos::ipa {
namespace {

using holos::util::re_sub;
using holos::util::compile;
using holos::util::Pcre2Ptr;

constexpr const char* kAcute = "\u02C8"; // ˈ  (U+02C8)  -- NOTE: Python ACUTE=U+0301 is input mark; output stress=U+02C8
// Input accent marks (combining) used in the orthography:
constexpr const char* kInAcute = "\u0301";
constexpr const char* kInGrave = "\u0300";
constexpr const char* kSentinel = "\u2040"; // ⁀ monosyllabic stress sentinel
constexpr const char* kGraveMark = "\u02CC"; // ˌ output secondary stress

using Rules = std::vector<std::pair<std::string, std::string>>;
void apply(std::string& s, const Rules& r) { for (auto& [p, x] : r) s = re_sub(p, x, s); }

bool in_set(const std::vector<std::string>& set, const std::string& v) {
    return std::find(set.begin(), set.end(), v) != set.end();
}

// Unicode codepoint lowercase sufficient for Ukrainian Cyrillic + ASCII.
uint32_t cp_lower(uint32_t cp) {
    if (cp >= 'A' && cp <= 'Z') return cp + 32;
    if (cp >= 0x0410 && cp <= 0x042F) return cp + 0x20; // А-Я -> а-я
    if (cp >= 0x0400 && cp <= 0x040F) return cp + 0x50; // Ѐ-Џ (Є І Ї ...)
    if (cp == 0x0490) return 0x0491;                     // Ґ -> ґ
    return cp;
}

// In-place UTF-8 aware lowercase.
void utf8_tolower(std::string& s) {
    std::string out; out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        uint32_t cp; size_t n;
        if (c < 0x80) { cp = c; n = 1; }
        else if (c < 0xE0) { cp = ((c & 0x1F) << 6) | (s[i+1] & 0x3F); n = 2; }
        else if (c < 0xF0) { cp = ((c & 0x0F) << 12) | ((s[i+1] & 0x3F) << 6) | (s[i+2] & 0x3F); n = 3; }
        else { cp = ((c & 0x07) << 18) | ((s[i+1] & 0x3F) << 12) | ((s[i+2] & 0x3F) << 6) | (s[i+3] & 0x3F); n = 4; }
        uint32_t l = cp_lower(cp);
        if (l < 0x80) out.push_back(static_cast<char>(l));
        else if (l < 0x800) { out.push_back(static_cast<char>(0xC0 | (l >> 6))); out.push_back(static_cast<char>(0x80 | (l & 0x3F))); }
        else if (l < 0x10000) { out.push_back(static_cast<char>(0xE0 | (l >> 12))); out.push_back(static_cast<char>(0x80 | ((l >> 6) & 0x3F))); out.push_back(static_cast<char>(0x80 | (l & 0x3F))); }
        else { out.push_back(static_cast<char>(0xF0 | (l >> 18))); out.push_back(static_cast<char>(0x80 | ((l >> 12) & 0x3F))); out.push_back(static_cast<char>(0x80 | ((l >> 6) & 0x3F))); out.push_back(static_cast<char>(0x80 | (l & 0x3F))); }
        i += n;
    }
    s = std::move(out);
}

// Character inventories (exact codepoints, matching the Python reference).
const std::string VOWEL_NO_I = "a\u025B\u026Au\u0254\u0250o\u028Ae";   // aɛɪuɔɐoʊe
const std::string VOWEL = VOWEL_NO_I + "i";
const std::string VOWEL_C = "[" + VOWEL + "]";
const std::string CONS_NO_W = "bdz\u0292\u0261\u0266mnlrpftskx\u0283j"; // bdzʒɡɦmnlrpftskxʃj
const std::string CONS_NO_W_C = "[" + CONS_NO_W + "]";
const std::string CONS = CONS_NO_W + "\u028B\u03B2\u031Ew\u028D";       // +ʋβ̞wʍ
const std::string CONS_C = "[" + CONS + "]";
const std::string PALAT = "tdsznlrbp\u028Bf\u0261mk\u0266x\u0283\u0292"; // tdsznlrbpʋfɡmkɦxʃʒ
const std::string PALAT_C = "[" + PALAT + "]";
const std::string VOICED_OBSTR = "[bdz\u0292\u0261\u0266]";             // [bdzʒɡɦ]
const std::string STRESS_NEG = "[^" + std::string(kAcute) + kGraveMark + kSentinel + "]";

} // namespace

std::string transcribe_impl(std::string text, bool check_accent) {
    const std::vector<std::pair<std::string, std::string>> voicing = {
        {"p","b"},{"f","v"},{"t","d"},{"t\u02B2","d\u02B2"},
        {"s","z"},{"s\u02B2","z\u02B2"},{"\u0283","\u0292"},
        {"k","\u0261"},{"x","\u0266"},
        {"t\u0361s","d\u0361z"},{"t\u0361s\u02B2","d\u0361z\u02B2"},
        {"t\u0361\u0283","d\u0361\u0292"},{"\u0283t\u0361\u0283","\u0292d\u0361\u0292"},
    };
    const std::vector<std::string> onset = {
        "spr","str","skr","spl","skl","sp","st","sk","sf","sx",
        "pr","br","tr","dr","kr","gr","\u0266r","fr","xr",
        "pl","bl","kl","gl","\u0266l","fl","xl",
    };

    // Grave decomposition.
    for (auto& [from, to] : std::vector<std::pair<std::string, std::string>>{
             {"\u0450",std::string("e")+kInGrave},{"\u0400",std::string("E")+kInGrave},
             {"\u045D",std::string("i")+kInGrave},{"\u040D",std::string("I")+kInGrave}}) {
        size_t p = 0;
        while ((p = text.find(from, p)) != std::string::npos) { text.replace(p, from.size(), to); p += to.size(); }
    }
    utf8_tolower(text);

    if (check_accent && text.find(kInAcute) == std::string::npos &&
        text.find(kInGrave) == std::string::npos) {
        int n = 0;
        for (const char* v : {"\u0430","\u0435","\u0454","\u0438","\u0456","\u0457","\u043E","\u0443","\u044E","\u044F"}) {
            size_t p = 0; std::string vv = v;
            while ((p = text.find(vv, p)) != std::string::npos) { ++n; ++p; }
        }
        if (n > 1) throw std::runtime_error("AccentIsMissing");
    }

    text = re_sub("\\s+", " ", text);

    const Rules ortho = {
        {"\u043D\u0442\u0441\u044C\u043A","\u043D\u044C\u0441\u044C\u043A"},
        {"\u0441\u0442\u0441\u044C\u043A","\u0441\u044C\u043A"},
        {"\u043D\u0442\u0441\u0442","\u043D\u0441\u0442"},
        {"\u0441\u0442\u0447","\u0448\u0447"},
        {"\u0441\u0442\u0434","\u0437\u0434"},
        {"\u0441\u0442\u0441","\u0441\u02D0"},
        {"#\u0437\u0448","#\u0448\u02D0"},
        {"\u0437\u0448","\u0436\u0448"},
        {"#\u0437\u0447","#\u0448\u0447"},
        {"\u0437\u0447","\u0436\u0447"},
        {"([\u0431\u0432\u0433\u0491\u0434])\\1","$1\u02D0"},
        {"([^\u0434]+)\u0436\u0436","$1\u0436\u02D0"},
        {"([^\u0434]+)\u0437\u0437","$1\u0437\u02D0"},
        {"([\u0439\u043A\u043B\u043C\u043D\u043F\u0440\u0441\u0442\u0444\u0445\u0446\u0447\u0448\u0449])\\1","$1\u02D0"},
        {"\u0434\u0436\u0434\u0436","\u0434\u0436\u02D0"},
        {"\u0434\u0437\u0434\u0437","\u0434\u0437\u02D0"},
    };
    const Rules map3 = {
        {"\u0434\u0437\u044C","d\u0361z\u02B2"},
        {"\u0442\u044C\u0441","t\u0361s\u02B2\u02D0"},
    };
    const Rules map2 = {
        {"\u0434\u0436","d\u0361\u0292"},{"\u0434\u0437","d\u0361z"},
        {"\u0434\u0441","d\u0361zs"},{"\u0434\u0448","d\u0361\u0292\u0283"},
        {"\u0434\u0447","d\u0361\u0292t\u0361\u0283"},{"\u0434\u0446","d\u0361zt\u0361s"},
        {"\u0442\u0441","t\u0361s"},{"\u0442\u0448","t\u0361\u0283\u0283"},
        {"\u0442\u0447","t\u0361\u0283\u02D0"},{"\u0442\u0446","t\u0361s\u02D0"},
    };
    const Rules map1 = {
        {"\u0430","a"},{"\u0431","b"},{"\u0432","\u028B"},{"\u0433","\u0266"},
        {"\u0491","\u0261"},{"\u0434","d"},{"\u0435","\u025B"},{"\u0454","j\u025B"},
        {"\u0436","\u0292"},{"\u0437","z"},{"\u0438","\u026A"},{"\u0456","i"},
        {"\u0457","ji"},{"\u0439","j"},{"\u043A","k"},{"\u043B","l"},
        {"\u043C","m"},{"\u043D","n"},{"\u043E","\u0254"},{"\u043F","p"},
        {"\u0440","r"},{"\u0441","s"},{"\u0442","t"},{"\u0443","u"},
        {"\u0444","f"},{"\u0445","x"},{"\u0446","t\u0361s"},{"\u0447","t\u0361\u0283"},
        {"\u0448","\u0283"},{"\u0449","\u0283t\u0361\u0283"},{"\u044C","\u02B2"},
        {"\u044E","ju"},{"\u044F","ja"},{"\u2019","j"},
        {kInAcute, kAcute}, {kInGrave, kGraveMark},
    };

    // Split on whitespace/hyphen via temp delimiter 0x01.
    std::string norm = re_sub("[\\s\\-]+", "\x01", text);
    std::vector<std::string> parts;
    for (size_t s = 0; ; ) {
        size_t p = norm.find('\x01', s);
        if (p == std::string::npos) { parts.push_back(norm.substr(s)); break; }
        parts.push_back(norm.substr(s, p - s)); s = p + 1;
    }

    std::vector<std::string> pronuns;
    for (std::string ph : parts) {
        ph = "#" + ph + "#";
        apply(ph, ortho);
        ph = re_sub("'", "%", ph);
        apply(ph, map3); apply(ph, map2); apply(ph, map1);

        // Move stress before vowel: ([aɛiɪuɔ])([ˈˌ]) -> $2$1
        ph = re_sub("([a\u025Bi\u026Au\u0254])([" + std::string(kAcute) + kGraveMark + "])", "$2$1", ph);

        // Monosyllabic auto-stress.
        {
            Pcre2Ptr re = compile("[a\u025Bi\u026Au\u0254]");
            pcre2_match_data* md = pcre2_match_data_create_from_pattern(re.get(), nullptr);
            int cnt = 0; PCRE2_SIZE off = 0, len = ph.size();
            while (off < len) {
                int rc = pcre2_match(re.get(), reinterpret_cast<PCRE2_SPTR>(ph.data()), len, off, 0, md, nullptr);
                if (rc <= 0) break;
                PCRE2_SIZE* ov = pcre2_get_ovector_pointer(md);
                if (ov[1] == ov[0]) { ++off; continue; }
                ++cnt; off = ov[1];
            }
            pcre2_match_data_free(md);
            if (cnt == 1 && check_accent) ph = re_sub("([a\u025Bi\u026Au\u0254])", std::string(kSentinel) + "$1", ph);
        }

        // Palatalization before i / j.
        ph = re_sub("(" + PALAT_C + ")([\u02D0]?)([" + std::string(kAcute) + kGraveMark + kSentinel + "]?)i", "$1\u02B2$2$3i", ph);
        ph = re_sub("(" + PALAT_C + ")([\u02D0]?)j", "$1\u02B2$2", ph);
        ph = re_sub("\u02B2\u02D0j", "\u02B2\u02D0", ph);
        ph = re_sub("st\u0361s\u02B2([\u02D0]?)", "s\u02B2t\u0361s\u02B2", ph);

        // Voicing (ordered, regressive).
        for (auto& [vl, vd] : voicing) ph = re_sub(vl + "(" + VOICED_OBSTR + "+)", vd + "$1", ph);

        // Soft-consonant assimilation.
        ph = re_sub("([tdszn])(.)\u02B2", "$1\u02B2$2\u02B2", ph);
        ph = re_sub("([tdsznl])t\u0361s\u02B2", "$1\u02B2t\u0361s\u02B2", ph);
        ph = re_sub("([tdsznl])d\u0361z\u02B2", "$1\u02B2d\u0361z\u02B2", ph);
        ph = re_sub("t\u0361s(.)\u02B2", "t\u0361s\u02B2$1\u02B2", ph);
        ph = re_sub("d\u0361z(.)\u02B2", "d\u0361z\u02B2$1\u02B2", ph);
        ph = re_sub("d\u0361zt\u0361s\u02B2", "d\u0361z\u02B2t\u0361s\u02B2", ph);
        ph = re_sub("t\u0361sd\u0361z\u02B2", "t\u0361s\u02B2d\u0361z\u02B2", ph);

        // Hushing + hissing assimilation.
        ph = re_sub("\u0292t\u0361s\u02B2", "z\u02B2t\u0361s\u02B2", ph);
        ph = re_sub("t\u0361\u0283t\u0361s\u02B2", "t\u0361s\u02B2\u02D0", ph);
        ph = re_sub("\u0283t\u0361s\u02B2", "s\u02B2t\u0361s\u02B2", ph);
        ph = re_sub("\u0283s\u02B2", "s\u02B2\u02D0", ph);

        // Hissing before hushing.
        ph = re_sub("z\u0292", "\u0292\u02D0", ph);
        ph = re_sub("s\u0283", "\u0283\u02D0", ph);
        ph = re_sub("zt\u0361\u0283", "\u0292t\u0361\u0283", ph);
        ph = re_sub("zd\u0361\u0292", "\u0292d\u0361\u0292", ph);
        ph = re_sub("t\u0361\u0292", "d\u0361\u0292", ph);
        ph = re_sub("t\u0361z", "d\u0361z", ph);

        // Cleanup excessive palatalization.
        ph = re_sub("([^a\u025Bi\u026Au\u0254]+)\u02B2([^a\u025Bi\u026Au\u0254]+)\u02B2([^a\u025Bi\u026Au\u0254]+)\u02B2", "$1$2\u02B2$3\u02B2", ph);

        // Allophones.
        ph = re_sub("(" + STRESS_NEG + ")a", "$1\u0250", ph);
        ph = re_sub("(" + STRESS_NEG + ")u", "$1\u028A", ph);
        ph = re_sub("\u0254([bdz\u0292\u0261\u0266mnlrpftskx\u02B2\u0283\u0361]+)([" + std::string(kAcute) + kGraveMark + kSentinel + "][ui\u028A])", "o$1$2", ph);
        ph = re_sub("(" + STRESS_NEG + ")[\u025B]", "$1e", ph);

        ph = re_sub(kSentinel, "", ph);

        // ʋ allophones.
        ph = re_sub("(" + VOWEL_C + ")\u028B([" + CONS_NO_W + "#])", "$1u\u032F$2", ph);
        ph = re_sub("\u028B([" + std::string(kAcute) + kGraveMark + "]?[\u0254uo\u028Abdz\u0292\u0261\u0266mnlr])", "w$1", ph);
        ph = re_sub("\u028B([pftskx\u0283])", "\u028D$1", ph);

        // j allophones.
        ph = re_sub("(" + VOWEL_C + ")j([" + CONS_NO_W + "#])", "$1i\u032F$2", ph);
        ph = re_sub("#j(" + CONS_NO_W_C + ")", "#i\u032F$1", ph);

        ph = re_sub("%", "", ph);

        // Stress repositioning.
        ph = re_sub("([^#" + VOWEL + "]?[\u02B2\u02D0]*)([" + std::string(kAcute) + kGraveMark + "])", "$2$1", ph);
        ph = re_sub("([^#" + VOWEL + "]\u0361)([" + std::string(kAcute) + kGraveMark + "])", "$2$1", ph);

        // Onset callback.
        {
            std::string pat = "(.)(\u02B2?)(" + CONS_C + ")(\u02B2?)([" + std::string(kAcute) + kGraveMark + "])(" + CONS_C + ")";
            Pcre2Ptr re = compile(pat);
            pcre2_match_data* md = pcre2_match_data_create_from_pattern(re.get(), nullptr);
            std::string out; out.reserve(ph.size());
            PCRE2_SIZE off = 0, len = ph.size();
            while (off < len) {
                int rc = pcre2_match(re.get(), reinterpret_cast<PCRE2_SPTR>(ph.data()), len, off, 0, md, nullptr);
                if (rc <= 0) { out.append(ph, off, len - off); break; }
                PCRE2_SIZE* ov = pcre2_get_ovector_pointer(md);
                PCRE2_SIZE ms = ov[0], me = ov[1];
                out.append(ph, off, ms - off);
                auto g = [&](int i){ return ov[2*i] == PCRE2_UNSET ? std::string() : ph.substr(ov[2*i], ov[2*i+1]-ov[2*i]); };
                std::string a = g(1), aj = g(2), b = g(3), bj = g(4), stress = g(5), c = g(6);
                std::string cluster = a + b + c;
                if (in_set(onset, cluster)) out += stress + a + aj + b + bj + c;
                else if (in_set(onset, b + c) || c == "j") out += a + aj + stress + b + bj + c;
                else out += a + aj + b + bj + stress + c;
                off = me;
            }
            pcre2_match_data_free(md);
            ph = std::move(out);
        }

        ph = re_sub("([^#" + VOWEL + "]\u0361)([" + std::string(kAcute) + kGraveMark + "])(.\u02B2?j)", "$2$1$3", ph);
        ph = re_sub("([^#" + VOWEL + "]\u0361)([" + std::string(kAcute) + kGraveMark + "])(.\u02B2?)", "$1$3$2", ph);
        ph = re_sub("#([^#" + VOWEL + "]+)([" + std::string(kAcute) + kGraveMark + "])", "#$2$1", ph);
        ph = re_sub("#([ui]\u032F)([" + std::string(kAcute) + kGraveMark + "])", "#$2$1", ph);
        ph = re_sub("\u02B2?\u02D0\u02B2", "\u02B2\u02D0", ph);
        ph = re_sub("l([^\u02B2])", "\u026B$1", ph);

        pronuns.push_back(ph);
    }

    std::string result;
    for (size_t i = 0; i < pronuns.size(); ++i) { if (i) result += " "; result += pronuns[i]; }

    // Remove '#' (UTF-8 safe).
    std::string out; out.reserve(result.size());
    for (size_t i = 0; i < result.size(); ) {
        if (result[i] == '#') { ++i; continue; }
        unsigned char c = static_cast<unsigned char>(result[i]);
        size_t n = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
        out.append(result, i, n); i += n;
    }
    return out;
}

} // namespace holos::ipa

// Public API wrappers (defined here; declared in ipa_uk.h).
namespace holos::ipa {
std::string transcribe(std::string_view text) { return transcribe_impl(std::string(text), false); }
std::string transcribe(std::string_view text, bool check_accent) { return transcribe_impl(std::string(text), check_accent); }
} // namespace holos::ipa
