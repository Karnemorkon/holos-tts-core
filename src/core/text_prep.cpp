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

// Голосна української (великий/маленький регістр) + латиниця — для перевірки
// місця позначки '+' у apply_stress_markup.
bool is_vowel_cp(uint32_t cp) {
    switch (cp) {
        case 0x0430: case 0x0435: case 0x0454: case 0x0438: case 0x0456:
        case 0x0457: case 0x043E: case 0x0443: case 0x044E: case 0x044F:
        case 0x0410: case 0x0415: case 0x0404: case 0x0418: case 0x0406:
        case 0x0407: case 0x041E: case 0x0423: case 0x042E: case 0x042F:
        case 'a': case 'e': case 'i': case 'o': case 'u': case 'y':
        case 'A': case 'E': case 'I': case 'O': case 'U': case 'Y':
            return true;
        default:
            return false;
    }
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
    while ((p = s.find('+', p)) != std::string::npos) {
        // '+' після голосної (зам+ок) — ставимо наголос як є.
        size_t prev = p;
        if (prev > 0) {
            --prev;
            while (prev > 0 && (static_cast<unsigned char>(s[prev]) & 0xC0) == 0x80) --prev;
        }
        bool after_vowel = false;
        if (prev < p) {
            utf8proc_int32_t prev_cp = 0;
            utf8proc_ssize_t n = utf8proc_iterate(
                reinterpret_cast<const utf8proc_uint8_t*>(s.data()) + prev,
                static_cast<utf8proc_ssize_t>(p - prev), &prev_cp);
            after_vowel = n > 0 && is_vowel_cp(static_cast<uint32_t>(prev_cp));
        }
        if (after_vowel) {
            s.replace(p, 1, "\u0301");
            p += 2; // U+0301 = 2 байти
            continue;
        }
        // '+' перед голосною (к+ава) — переносимо наголос на наступну голосну;
        // якщо голосної немає — просто прибираємо '+' (раніше він ставив наголос
        // на приголосну, напр. «к+ава» -> «ќава»).
        s.erase(p, 1);
        size_t q = p, acute_at = std::string::npos;
        while (q < s.size()) {
            utf8proc_int32_t cp = 0;
            utf8proc_ssize_t n = utf8proc_iterate(
                reinterpret_cast<const utf8proc_uint8_t*>(s.data()) + q,
                static_cast<utf8proc_ssize_t>(s.size() - q), &cp);
            if (n <= 0) { ++q; continue; }
            if (is_vowel_cp(static_cast<uint32_t>(cp))) { acute_at = q + n; break; }
            q += n;
        }
        if (acute_at != std::string::npos) {
            s.insert(acute_at, "\u0301");
            p = acute_at + 2;
        }
    }
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
    s = transliterate_english(s);
    if (s.empty()) return "";
    s = apply_stress_markup(s);
    s = standardize_dashes(s);
    s = ensure_terminal_punct(s);
    size_t p = 0;
    while ((p = s.find(" - ", p)) != std::string::npos) { s.replace(p, 3, ": "); p += 2; }
    return s;
}


// Транслітерація англійських слів та абревіатур до української вимови.
// ASCII-токени (NATO, HTML, API, hello, ...) замінюються українськими
// відповідниками (порівняння без урахування регістру). Невідомі абревіатури
// (лише A-Z, до 6 літер) спеллюються по літерах; решта слів лишається як є.
static std::string transliterate_english_impl(std::string_view text) {
    // Порівняння ASCII-рядків без урахування регістру.
    auto eq = [](std::string_view a, std::string_view b) {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i) {
            char x = a[i], y = b[i];
            if (x >= 'A' && x <= 'Z') x = static_cast<char>(x + ('a' - 'A'));
            if (y >= 'A' && y <= 'Z') y = static_cast<char>(y + ('a' - 'A'));
            if (x != y) return false;
        }
        return true;
    };
    auto get_uk = [&eq](std::string_view w) -> const char* {
        if (eq(w, "NATO")) return "\u043d\u0430\u0442\u043e";
        if (eq(w, "UN")) return "\u044e \u0435\u043d";
        if (eq(w, "EU")) return "\u0454\u0432\u0440\u043e\u0441\u043e\u044e\u0437";
        if (eq(w, "USA")) return "\u044e \u0435\u0441 \u0435";
        if (eq(w, "UK")) return "\u044e \u043a\u0435\u0439";
        if (eq(w, "USSR")) return "\u0435\u0441 \u0435\u0441 \u0435\u0440";
        if (eq(w, "UNESCO")) return "\u044e\u043d\u0435\u0441\u043a\u043e";
        if (eq(w, "WHO")) return "\u0432\u043e";
        if (eq(w, "HTML")) return "\u0445 \u0442 \u043c \u043b";
        if (eq(w, "CSS")) return "\u0446 \u0441 \u0441";
        if (eq(w, "JS")) return "\u0434\u0436 \u0441";
        if (eq(w, "API")) return "\u0435 \u043f \u0456";
        if (eq(w, "URL")) return "\u044e \u0435\u0440 \u043b";
        if (eq(w, "HTTP")) return "\u0445 \u0442 \u0442 \u043f";
        if (eq(w, "HTTPS")) return "\u0445 \u0442 \u0442 \u043f \u0441";
        if (eq(w, "SQL")) return "\u0435\u0441 \u043a\u044e \u043b";
        if (eq(w, "AI")) return "\u0435 \u0456";
        if (eq(w, "ML")) return "\u0435\u043c \u043b";
        if (eq(w, "GPU")) return "\u0433 \u043f \u044e";
        if (eq(w, "CPU")) return "\u0446 \u043f \u044e";
        if (eq(w, "RAM")) return "\u0440\u0435\u043c";
        if (eq(w, "SSD")) return "\u0435\u0441 \u0435\u0441 \u0434\u0456";
        if (eq(w, "USB")) return "\u044e \u0435\u0441 \u0431";
        if (eq(w, "WiFi")) return "\u0432\u0430\u0439 \u0444\u0430\u0439";
        if (eq(w, "Bluetooth")) return "\u0431\u043b\u044e\u0442\u0443\u0437";
        if (eq(w, "GPS")) return "\u0433 \u043f \u0435\u0441";
        if (eq(w, "PDF")) return "\u043f \u0434 \u0444";
        if (eq(w, "XML")) return "\u0456\u043a\u0441 \u043c \u043b";
        if (eq(w, "JSON")) return "\u0434\u0436\u0435\u0439 \u0441\u0430\u043d";
        if (eq(w, "FAQ")) return "\u0444 \u0430 \u043a\u044e\u044e";
        if (eq(w, "DIY")) return "\u0434 \u0456 \u0432\u0430";
        if (eq(w, "ASAP")) return "\u0435\u0441 \u0435\u0439 \u043f";
        if (eq(w, "CEO")) return "\u0441\u0456 \u0456 \u043e";
        if (eq(w, "CTO")) return "\u0441\u0456 \u0442\u0456 \u043e";
        if (eq(w, "CFO")) return "\u0441\u0456 \u0435\u0444 \u043e";
        if (eq(w, "HR")) return "\u0445 \u0440";
        if (eq(w, "PR")) return "\u043f \u0440";
        if (eq(w, "QA")) return "\u043a\u044c\u044e \u0435";
        if (eq(w, "DevOps")) return "\u0434\u0435\u0432 \u043e\u043f\u0441";
        if (eq(w, "Frontend")) return "\u0444\u0440\u043e\u043d\u0442\u0435\u043d\u0434";
        if (eq(w, "Backend")) return "\u0431\u0435\u043a\u0435\u043d\u0434";
        if (eq(w, "Fullstack")) return "\u0444\u0443\u043b\u0441\u0442\u0435\u043a";
        if (eq(w, "SDK")) return "\u0435\u0441 \u0434\u0456 \u043a\u0435\u0439";
        if (eq(w, "CLI")) return "\u0446\u0456 \u0435\u043b\u044c \u0430\u0439";
        if (eq(w, "GUI")) return "\u0433\u044e \u0430\u0439";
        if (eq(w, "IDE")) return "\u0430\u0439 \u0434\u0456 \u0456";
        if (eq(w, "OS")) return "\u043e \u0435\u0441";
        if (eq(w, "DB")) return "\u0434 \u0431";
        if (eq(w, "CRUD")) return "\u043a\u0440\u0430\u0434";
        if (eq(w, "ORM")) return "\u043e\u0440\u043c";
        if (eq(w, "MVC")) return "\u0435\u043c \u0432\u0456 \u0446\u0456";
        if (eq(w, "REST")) return "\u0440\u0435\u0441\u0442";
        if (eq(w, "GraphQL")) return "\u0433\u0440\u0430\u0444 \u043a\u044e \u043b";
        if (eq(w, "WebSocket")) return "\u0432\u0435\u0431 \u0441\u043e\u043a\u0435\u0442";
        if (eq(w, "OAuth")) return "\u043e \u043e\u0442";
        if (eq(w, "JWT")) return "\u0434\u0436 \u0432 \u0442";
        if (eq(w, "CI/CD")) return "\u0441\u0456 \u0430\u0439 \u0446\u0456 \u0434\u0456";
        if (eq(w, "Docker")) return "\u0434\u043e\u043a\u0435\u0440";
        if (eq(w, "Kubernetes")) return "\u043a\u0443\u0431\u0435\u0440\u043d\u0435\u0442\u0435\u0441";
        if (eq(w, "Linux")) return "лінукс";
        if (eq(w, "Windows")) return "віндоус";
        if (eq(w, "macOS")) return "\u043c\u0430\u043a \u043e \u0435\u0441";
        if (eq(w, "Android")) return "\u0430\u043d\u0434\u0440\u043e\u0457\u0434";
        if (eq(w, "iOS")) return "\u0430\u0439 \u043e \u0435\u0441";
        if (eq(w, "Python")) return "\u043f\u0430\u0439\u0442\u043e\u043d";
        if (eq(w, "JavaScript")) return "\u044f\u0432\u0430\u0441\u043a\u0440\u0438\u043f\u0442";
        if (eq(w, "TypeScript")) return "\u0442\u0430\u0439\u043f\u0441\u043a\u0440\u0438\u043f\u0442";
        if (eq(w, "React")) return "\u0440\u0435\u0430\u043a\u0442";
        if (eq(w, "Vue")) return "\u0432\u0027\u044e";
        if (eq(w, "Angular")) return "\u0430\u043d\u0433\u0443\u043b\u044f\u0440";
        if (eq(w, "Node")) return "\u043d\u043e\u0443\u0434";
        if (eq(w, "npm")) return "\u0435\u043d \u043f \u043c";
        if (eq(w, "yarn")) return "\u044f\u0440\u043d";
        if (eq(w, "Git")) return "\u0433\u0456\u0442";
        if (eq(w, "GitHub")) return "\u0433\u0456\u0442 \u0445\u0430\u0431";
        if (eq(w, "GitLab")) return "\u0433\u0456\u0442 \u043b\u0430\u0431";
        if (eq(w, "IntelliJ")) return "\u0456\u043d\u0442\u0435\u043b\u0438\u0434\u0436\u0435\u0439";
        if (eq(w, "Postman")) return "\u043f\u043e\u0441\u0442\u043c\u0435\u043d";
        if (eq(w, "Swagger")) return "\u0441\u0432\u0430\u0433\u0435\u0440";
        if (eq(w, "Kafka")) return "\u043a\u0430\u0444\u043a\u0430";
        if (eq(w, "Redis")) return "\u0440\u0435\u0434\u0456\u0441";
        if (eq(w, "PostgreSQL")) return "\u043f\u043e\u0441\u0442\u0433\u0440\u0435\u0441 \u043a\u044e \u043b";
        if (eq(w, "MySQL")) return "\u043c\u0430\u0439 \u043a\u044e \u043b";
        if (eq(w, "MongoDB")) return "\u043c\u043e\u043d\u0433\u043e \u0434 \u0431";
        if (eq(w, "AWS")) return "\u0435 \u0432\u0456 \u0435\u0441";
        if (eq(w, "GCP")) return "\u0433 \u0441\u0456 \u043f";
        if (eq(w, "Azure")) return "\u0430\u0436\u0443\u0440";
        if (eq(w, "Terraform")) return "\u0442\u0435\u0440\u0440\u0430\u0444\u043e\u0440\u043c";
        if (eq(w, "Ansible")) return "\u0435\u043d\u0441\u0438\u0431\u043b";
        if (eq(w, "Jenkins")) return "\u0434\u0436\u0435\u043a\u0438\u043d\u0441";
        if (eq(w, "Prometheus")) return "\u043f\u0440\u043e\u043c\u0435\u0442\u0435\u0443\u0441";
        if (eq(w, "Grafana")) return "\u0433\u0440\u0430\u0444\u0430\u043d\u0430";
        if (eq(w, "Elasticsearch")) return "\u0435\u043b\u0430\u0441\u0442\u0438\u043a \u0441\u0435\u0440\u0447";
        if (eq(w, "Logstash")) return "\u043b\u043e\u0433\u0441\u0442\u0435\u0448";
        if (eq(w, "Kibana")) return "\u043a\u0456\u0431\u0430\u043d\u0430";
        if (eq(w, "Hello")) return "\u0445\u0435\u043b\u043e";
        if (eq(w, "World")) return "\u0432\u043e\u0440\u043b\u0434";
        if (eq(w, "Welcome")) return "велком";
        if (eq(w, "Thanks")) return "\u0441\u0435\u043d\u043a\u0441";
        if (eq(w, "Please")) return "\u043f\u043b\u0456\u0437";
        if (eq(w, "Sorry")) return "\u0441\u043e\u0440\u0440\u0456";
        if (eq(w, "Bye")) return "\u0431\u0430\u0439";
        if (eq(w, "Goodbye")) return "\u0433\u0443\u0434\u0431\u0430\u0439";
        if (eq(w, "Yes")) return "\u0454\u0441";
        if (eq(w, "No")) return "\u043d\u043e\u0443";
        if (eq(w, "OK")) return "\u043e\u0443 \u043a\u0435\u0439";
        if (eq(w, "Okay")) return "\u043e\u0443 \u043a\u0435\u0439";
        if (eq(w, "Sure")) return "шор";
        if (eq(w, "Great")) return "\u0433\u0440\u0435\u0439\u0442";
        if (eq(w, "Cool")) return "\u043a\u0443\u043b";
        if (eq(w, "Awesome")) return "осом";
        if (eq(w, "Perfect")) return "\u043f\u0435\u0440\u0444\u0435\u043a\u0442";
        if (eq(w, "Fantastic")) return "фантастик";
        if (eq(w, "Wonderful")) return "\u0432\u0430\u043d\u0434\u0435\u0440\u0444\u0443\u043b";
        if (eq(w, "Excellent")) return "\u0435\u043a\u0441\u0435\u043b\u0435\u043d\u0442";
        if (eq(w, "Server")) return "\u0441\u0435\u0440\u0432\u0435\u0440";
        if (eq(w, "Client")) return "\u043a\u043b\u0456\u0454\u043d\u0442";
        if (eq(w, "Database")) return "\u0434\u0430\u0442\u0430\u0431\u0435\u0439\u0437";
        if (eq(w, "Network")) return "\u043d\u0435\u0442\u0432\u043e\u0440\u043a";
        if (eq(w, "Protocol")) return "\u043f\u0440\u043e\u0442\u043e\u043a\u043e\u043b";
        if (eq(w, "Interface")) return "\u0456\u043d\u0442\u0435\u0440\u0444\u0435\u0439\u0441";
        if (eq(w, "Framework")) return "\u0444\u0440\u0435\u0439\u043c\u0432\u043e\u0440\u043a";
        if (eq(w, "Library")) return "лайбрері";
        if (eq(w, "Module")) return "\u043c\u043e\u0434\u0443\u043b\u044c";
        if (eq(w, "Package")) return "пекідж";
        if (eq(w, "Function")) return "\u0444\u0443\u043d\u043a\u0448\u043d";
        if (eq(w, "Variable")) return "веріабл";
        if (eq(w, "Class")) return "\u043a\u043b\u0430\u0441";
        if (eq(w, "Object")) return "\u043e\u0431\u0454\u043a\u0442";
        if (eq(w, "Method")) return "\u043c\u0435\u0442\u043e\u0434";
        if (eq(w, "Property")) return "\u043f\u0440\u043e\u043f\u0435\u0440\u0442\u0456";
        if (eq(w, "Event")) return "\u0456\u0432\u0435\u043d\u0442";
        if (eq(w, "Handler")) return "\u0445\u0435\u043d\u0434\u043b\u0435\u0440";
        if (eq(w, "Callback")) return "\u043a\u043e\u043b\u0431\u0435\u043a";
        if (eq(w, "Promise")) return "\u043f\u0440\u043e\u043c\u0438\u0441";
        if (eq(w, "Async")) return "ейсінк";
        if (eq(w, "Await")) return "\u0430\u0432\u0435\u0439\u0442";
        if (eq(w, "Import")) return "\u0456\u043c\u043f\u043e\u0440\u0442";
        if (eq(w, "Export")) return "\u0435\u043a\u0441\u043f\u043e\u0440\u0442";
        if (eq(w, "Default")) return "\u0434\u0435\u0444\u043e\u043b\u0442";
        if (eq(w, "Const")) return "\u043a\u043e\u043d\u0441\u0442";
        if (eq(w, "Let")) return "\u043b\u0435\u0442";
        if (eq(w, "Var")) return "\u0432\u0430\u0440";
        if (eq(w, "If")) return "\u0456\u0444";
        if (eq(w, "Else")) return "\u0435\u043b\u0441";
        if (eq(w, "For")) return "\u0444\u043e\u0440";
        if (eq(w, "While")) return "\u0432\u0430\u0439\u043b";
        if (eq(w, "Return")) return "\u0440\u0438\u0442\u0435\u0440\u043d";
        if (eq(w, "Try")) return "\u0442\u0440\u0430\u0439";
        if (eq(w, "Catch")) return "\u043a\u0435\u0442\u0447";
        if (eq(w, "Finally")) return "\u0444\u0430\u0439\u043d\u043b";
        if (eq(w, "Throw")) return "\u0442\u0440\u043e\u0443";
        if (eq(w, "New")) return "\u043d\u044c\u044e";
        if (eq(w, "This")) return "\u0434\u0456\u0441";
        if (eq(w, "Super")) return "\u0441\u0443\u043f\u0435\u0440";
        if (eq(w, "Extends")) return "\u0435\u043a\u0441\u0442\u0435\u043d\u0434\u0437";
        if (eq(w, "Implements")) return "\u0456\u043c\u043f\u043b\u0435\u043c\u0435\u043d\u0441";
        if (eq(w, "Abstract")) return "\u0430\u0431\u0441\u0442\u0440\u0430\u043a\u0442";
        if (eq(w, "Static")) return "\u0441\u0442\u0430\u0442\u0438\u043a";
        if (eq(w, "Public")) return "\u043f\u0430\u0431\u043b\u0456\u043a";
        if (eq(w, "Private")) return "\u043f\u0440\u0430\u0439\u0432\u0430\u0442";
        if (eq(w, "Protected")) return "\u043f\u0440\u043e\u0442\u0435\u043a\u0442\u0435\u0434";
        if (eq(w, "C++")) return "сі плюс плюс";
        if (eq(w, "C#")) return "сі шарп";
        if (eq(w, "PHP")) return "п х п";
        if (eq(w, "VPN")) return "в п н";
        if (eq(w, "IP")) return "і п";
        if (eq(w, "DNS")) return "д н ес";
        if (eq(w, "LAN")) return "лан";
        if (eq(w, "WAN")) return "ван";
        if (eq(w, "VSCode")) return "ві ес код";
        if (eq(w, "Wi-Fi")) return "вай фай";
        if (eq(w, "email")) return "імейл";
        if (eq(w, "Emoji")) return "емодзі";
        if (eq(w, "Google")) return "ґуґл";
        if (eq(w, "YouTube")) return "ютуб";
        if (eq(w, "Telegram")) return "телеграм";
        if (eq(w, "Instagram")) return "інстаграм";
        if (eq(w, "Facebook")) return "фейсбук";
        if (eq(w, "Twitter")) return "твітер";
        if (eq(w, "Wikipedia")) return "вікіпедія";
        if (eq(w, "ChatGPT")) return "чат дж п т";
        if (eq(w, "OpenAI")) return "опен ей ай";
        if (eq(w, "LLM")) return "ел ел ем";
        if (eq(w, "TTS")) return "ет те ес";
        if (eq(w, "STT")) return "ес те те";
        if (eq(w, "iPhone")) return "айфон";
        if (eq(w, "iPad")) return "айпад";
        return nullptr;
    };

    static const std::string_view letter_names[26] = {
        "а", "б", "ц", "д", "е", "еф", "ґ", "га", "і", "й", "ка", "ель", "ем",
        "ен", "о", "пе", "кю", "ер", "ес", "те", "у", "в", "дубль-в", "ікс",
        "ігрек", "зет"
    };

    std::string result;
    result.reserve(text.size());
    size_t i = 0;
    const size_t n = text.size();
    while (i < n) {
        const char c = text[i];
        const bool letter = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
        if (!letter) { result += c; ++i; continue; }

        // Токен: латиниця/цифри + символи, які бувають усередині (C++, C#, CI/CD, TCP/IP).
        size_t j = i;
        while (j < n) {
            const char d = text[j];
            const bool in_token =
                (d >= 'A' && d <= 'Z') || (d >= 'a' && d <= 'z') ||
                (d >= '0' && d <= '9') || d == '#' || d == '+' || d == '/' || d == '-';
            if (!in_token) break;
            ++j;
        }
        const std::string word(text.substr(i, j - i));
        if (const char* uk = get_uk(word)) {
            result += uk;
            i = j;
            continue;
        }

        // Відрізаємо хвостові #+/-, щоб "API-" і "NATO," не ламали розпізнавання.
        size_t core_len = word.size();
        while (core_len > 0) {
            const char d = word[core_len - 1];
            if (d == '#' || d == '+' || d == '/' || d == '-') --core_len;
            else break;
        }
        if (core_len > 0 && core_len < word.size()) {
            if (const char* uk = get_uk(std::string_view(word.data(), core_len))) {
                result += uk;
                result.append(word, core_len, std::string::npos);
                i = j;
                continue;
            }
        }

        // Невідома абревіатура з самих великих латинських літер -> по літерах.
        bool all_caps = core_len > 0 && core_len <= 6;
        for (size_t k = 0; all_caps && k < core_len; ++k)
            if (word[k] < 'A' || word[k] > 'Z') all_caps = false;
        if (all_caps) {
            for (size_t k = 0; k < core_len; ++k) {
                if (k) result += ' ';
                result.append(letter_names[word[k] - 'A']);
            }
            result.append(word, core_len, std::string::npos);
        } else {
            result += word;
        }
        i = j;
    }
    return result;
}

std::string TextPrep::transliterate_english(std::string_view text) {
    return transliterate_english_impl(text);
}

} // namespace holos


