#include "text_prep.h"
#include "pcre2_util.h"

#include <unordered_map>


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
    s = transliterate_english(s);
    if (s.empty()) return "";
    s = apply_stress_markup(s);
    s = standardize_dashes(s);
    s = ensure_terminal_punct(s);
    size_t p = 0;
    while ((p = s.find(" - ", p)) != std::string::npos) { s.replace(p, 3, ": "); p += 2; }
    return s;
}


// Транслітерація англійських слів та абревіатур до українського вимови.
// Global map for English to Ukrainian transliteration
std::string transliterate_english_impl(std::string_view text) {
    // Simple abbreviation handling using if-else to avoid map issues
    auto get_uk = [](std::string_view w) -> const char* {
        if (w == "NATO") return "\u043d\u0430\u0442\u043e";
        if (w == "UN") return "\u044e \u0435\u043d";
        if (w == "EU") return "\u0454\u0432\u0440\u043e\u0441\u043e\u044e\u0437";
        if (w == "USA") return "\u044e \u0435\u0441 \u0435";
        if (w == "UK") return "\u044e \u043a\u0435\u0439";
        if (w == "USSR") return "\u0435\u0441 \u0435\u0441 \u0435\u0440";
        if (w == "UNESCO") return "\u044e\u043d\u0435\u0441\u043a\u043e";
        if (w == "WHO") return "\u0432\u043e";
        if (w == "HTML") return "\u0445 \u0442 \u043c \u043b";
        if (w == "CSS") return "\u0446 \u0441 \u0441";
        if (w == "JS") return "\u0434\u0436 \u0441";
        if (w == "API") return "\u0435 \u043f \u0456";
        if (w == "URL") return "\u044e \u0435\u0440 \u043b";
        if (w == "HTTP") return "\u0445 \u0442 \u0442 \u043f";
        if (w == "HTTPS") return "\u0445 \u0442 \u0442 \u043f \u0441";
        if (w == "SQL") return "\u0435\u0441 \u043a\u044e \u043b";
        if (w == "AI") return "\u0435 \u0456";
        if (w == "ML") return "\u0435\u043c \u043b";
        if (w == "GPU") return "\u0433 \u043f \u044e";
        if (w == "CPU") return "\u0446 \u043f \u044e";
        if (w == "RAM") return "\u0440\u0435\u043c";
        if (w == "SSD") return "\u0435\u0441 \u0435\u0441 \u0434\u0456";
        if (w == "USB") return "\u044e \u0435\u0441 \u0431";
        if (w == "WiFi") return "\u0432\u0430\u0439 \u0444\u0430\u0439";
        if (w == "Bluetooth") return "\u0431\u043b\u044e\u0442\u0443\u0437";
        if (w == "GPS") return "\u0433 \u043f \u0435\u0441";
        if (w == "PDF") return "\u043f \u0434 \u0444";
        if (w == "XML") return "\u0456\u043a\u0441 \u043c \u043b";
        if (w == "JSON") return "\u0434\u0436\u0435\u0439 \u0441\u0430\u043d";
        if (w == "FAQ") return "\u0444 \u0430 \u043a\u044e\u044e";
        if (w == "DIY") return "\u0434 \u0456 \u0432\u0430";
        if (w == "ASAP") return "\u0435\u0441 \u0435\u0439 \u043f";
        if (w == "CEO") return "\u0441\u0456 \u0456 \u043e";
        if (w == "CTO") return "\u0441\u0456 \u0442\u0456 \u043e";
        if (w == "CFO") return "\u0441\u0456 \u0435\u0444 \u043e";
        if (w == "HR") return "\u0445 \u0440";
        if (w == "PR") return "\u043f \u0440";
        if (w == "QA") return "\u043a\u044c\u044e \u0435";
        if (w == "DevOps") return "\u0434\u0435\u0432 \u043e\u043f\u0441";
        if (w == "Frontend") return "\u0444\u0440\u043e\u043d\u0442\u0435\u043d\u0434";
        if (w == "Backend") return "\u0431\u0435\u043a\u0435\u043d\u0434";
        if (w == "Fullstack") return "\u0444\u0443\u043b\u0441\u0442\u0435\u043a";
        if (w == "SDK") return "\u0435\u0441 \u0434\u0456 \u043a\u0435\u0439";
        if (w == "CLI") return "\u0446\u0456 \u0435\u043b\u044c \u0430\u0439";
        if (w == "GUI") return "\u0433\u044e \u0430\u0439";
        if (w == "IDE") return "\u0430\u0439 \u0434\u0456 \u0456";
        if (w == "OS") return "\u043e \u0435\u0441";
        if (w == "DB") return "\u0434 \u0431";
        if (w == "CRUD") return "\u043a\u0440\u0430\u0434";
        if (w == "ORM") return "\u043e\u0440\u043c";
        if (w == "MVC") return "\u0435\u043c \u0432\u0456 \u0446\u0456";
        if (w == "REST") return "\u0440\u0435\u0441\u0442";
        if (w == "GraphQL") return "\u0433\u0440\u0430\u0444 \u043a\u044e \u043b";
        if (w == "WebSocket") return "\u0432\u0435\u0431 \u0441\u043e\u043a\u0435\u0442";
        if (w == "OAuth") return "\u043e \u043e\u0442";
        if (w == "JWT") return "\u0434\u0436 \u0432 \u0442";
        if (w == "CI/CD") return "\u0441\u0456 \u0430\u0439 \u0446\u0456 \u0434\u0456";
        if (w == "Docker") return "\u0434\u043e\u043a\u0435\u0440";
        if (w == "Kubernetes") return "\u043a\u0443\u0431\u0435\u0440\u043d\u0435\u0442\u0435\u0441";
        if (w == "Linux") return "\u043b\u0456\u043d\u043a\u0441";
        if (w == "Windows") return " \u0432\u0456\u043d\u0434\u043e\u0443\u0441";
        if (w == "macOS") return "\u043c\u0430\u043a \u043e \u0435\u0441";
        if (w == "Android") return "\u0430\u043d\u0434\u0440\u043e\u0457\u0434";
        if (w == "iOS") return "\u0430\u0439 \u043e \u0435\u0441";
        if (w == "Python") return "\u043f\u0430\u0439\u0442\u043e\u043d";
        if (w == "JavaScript") return "\u044f\u0432\u0430\u0441\u043a\u0440\u0438\u043f\u0442";
        if (w == "TypeScript") return "\u0442\u0430\u0439\u043f\u0441\u043a\u0440\u0438\u043f\u0442";
        if (w == "React") return "\u0440\u0435\u0430\u043a\u0442";
        if (w == "Vue") return "\u0432\u0027\u044e";
        if (w == "Angular") return "\u0430\u043d\u0433\u0443\u043b\u044f\u0440";
        if (w == "Node") return "\u043d\u043e\u0443\u0434";
        if (w == "npm") return "\u0435\u043d \u043f \u043c";
        if (w == "yarn") return "\u044f\u0440\u043d";
        if (w == "Git") return "\u0433\u0456\u0442";
        if (w == "GitHub") return "\u0433\u0456\u0442 \u0445\u0430\u0431";
        if (w == "GitLab") return "\u0433\u0456\u0442 \u043b\u0430\u0431";
        if (w == "VS Code") return "\u0432\u0456 \u0435\u0441 \u043a\u043e\u0434";
        if (w == "IntelliJ") return "\u0456\u043d\u0442\u0435\u043b\u0438\u0434\u0436\u0435\u0439";
        if (w == "Postman") return "\u043f\u043e\u0441\u0442\u043c\u0435\u043d";
        if (w == "Swagger") return "\u0441\u0432\u0430\u0433\u0435\u0440";
        if (w == "Kafka") return "\u043a\u0430\u0444\u043a\u0430";
        if (w == "Redis") return "\u0440\u0435\u0434\u0456\u0441";
        if (w == "PostgreSQL") return "\u043f\u043e\u0441\u0442\u0433\u0440\u0435\u0441 \u043a\u044e \u043b";
        if (w == "MySQL") return "\u043c\u0430\u0439 \u043a\u044e \u043b";
        if (w == "MongoDB") return "\u043c\u043e\u043d\u0433\u043e \u0434 \u0431";
        if (w == "AWS") return "\u0435 \u0432\u0456 \u0435\u0441";
        if (w == "GCP") return "\u0433 \u0441\u0456 \u043f";
        if (w == "Azure") return "\u0430\u0436\u0443\u0440";
        if (w == "Terraform") return "\u0442\u0435\u0440\u0440\u0430\u0444\u043e\u0440\u043c";
        if (w == "Ansible") return "\u0435\u043d\u0441\u0438\u0431\u043b";
        if (w == "Jenkins") return "\u0434\u0436\u0435\u043a\u0438\u043d\u0441";
        if (w == "Prometheus") return "\u043f\u0440\u043e\u043c\u0435\u0442\u0435\u0443\u0441";
        if (w == "Grafana") return "\u0433\u0440\u0430\u0444\u0430\u043d\u0430";
        if (w == "Elasticsearch") return "\u0435\u043b\u0430\u0441\u0442\u0438\u043a \u0441\u0435\u0440\u0447";
        if (w == "Logstash") return "\u043b\u043e\u0433\u0441\u0442\u0435\u0448";
        if (w == "Kibana") return "\u043a\u0456\u0431\u0430\u043d\u0430";
        if (w == "Hello") return "\u0445\u0435\u043b\u043e";
        if (w == "World") return "\u0432\u043e\u0440\u043b\u0434";
        if (w == "Welcome") return "\u0432\u0435\u043b\u043a\u043e";
        if (w == "Thanks") return "\u0441\u0435\u043d\u043a\u0441";
        if (w == "Please") return "\u043f\u043b\u0456\u0437";
        if (w == "Sorry") return "\u0441\u043e\u0440\u0440\u0456";
        if (w == "Bye") return "\u0431\u0430\u0439";
        if (w == "Goodbye") return "\u0433\u0443\u0434\u0431\u0430\u0439";
        if (w == "Yes") return "\u0454\u0441";
        if (w == "No") return "\u043d\u043e\u0443";
        if (w == "OK") return "\u043e\u0443 \u043a\u0435\u0439";
        if (w == "Okay") return "\u043e\u0443 \u043a\u0435\u0439";
        if (w == "Sure") return "\u0448\u0443\u0440";
        if (w == "Great") return "\u0433\u0440\u0435\u0439\u0442";
        if (w == "Cool") return "\u043a\u0443\u043b";
        if (w == "Awesome") return "\u043e\u0441\u0435\u043c";
        if (w == "Perfect") return "\u043f\u0435\u0440\u0444\u0435\u043a\u0442";
        if (w == "Fantastic") return "\u0444\u0435\u043d\u0441\u0442\u0435\u0442\u0438\u043a";
        if (w == "Wonderful") return "\u0432\u0430\u043d\u0434\u0435\u0440\u0444\u0443\u043b";
        if (w == "Excellent") return "\u0435\u043a\u0441\u0435\u043b\u0435\u043d\u0442";
        if (w == "Server") return "\u0441\u0435\u0440\u0432\u0435\u0440";
        if (w == "Client") return "\u043a\u043b\u0456\u0454\u043d\u0442";
        if (w == "Database") return "\u0434\u0430\u0442\u0430\u0431\u0435\u0439\u0437";
        if (w == "Network") return "\u043d\u0435\u0442\u0432\u043e\u0440\u043a";
        if (w == "Protocol") return "\u043f\u0440\u043e\u0442\u043e\u043a\u043e\u043b";
        if (w == "Interface") return "\u0456\u043d\u0442\u0435\u0440\u0444\u0435\u0439\u0441";
        if (w == "Framework") return "\u0444\u0440\u0435\u0439\u043c\u0432\u043e\u0440\u043a";
        if (w == "Library") return "\u043b\u0430\u0439\u0431\u0440\u0438";
        if (w == "Module") return "\u043c\u043e\u0434\u0443\u043b\u044c";
        if (w == "Package") return "\u043f\u0435\u0439\u043a\u0435\u0436";
        if (w == "Function") return "\u0444\u0443\u043d\u043a\u0448\u043d";
        if (w == "Variable") return "\u0432\u0435\u0440\u0438\u0431\u043b";
        if (w == "Class") return "\u043a\u043b\u0430\u0441";
        if (w == "Object") return "\u043e\u0431\u0454\u043a\u0442";
        if (w == "Method") return "\u043c\u0435\u0442\u043e\u0434";
        if (w == "Property") return "\u043f\u0440\u043e\u043f\u0435\u0440\u0442\u0456";
        if (w == "Event") return "\u0456\u0432\u0435\u043d\u0442";
        if (w == "Handler") return "\u0445\u0435\u043d\u0434\u043b\u0435\u0440";
        if (w == "Callback") return "\u043a\u043e\u043b\u0431\u0435\u043a";
        if (w == "Promise") return "\u043f\u0440\u043e\u043c\u0438\u0441";
        if (w == "Async") return "\u0435\u0441\u0438\u043d\u043a";
        if (w == "Await") return "\u0430\u0432\u0435\u0439\u0442";
        if (w == "Import") return "\u0456\u043c\u043f\u043e\u0440\u0442";
        if (w == "Export") return "\u0435\u043a\u0441\u043f\u043e\u0440\u0442";
        if (w == "Default") return "\u0434\u0435\u0444\u043e\u043b\u0442";
        if (w == "Const") return "\u043a\u043e\u043d\u0441\u0442";
        if (w == "Let") return "\u043b\u0435\u0442";
        if (w == "Var") return "\u0432\u0430\u0440";
        if (w == "If") return "\u0456\u0444";
        if (w == "Else") return "\u0435\u043b\u0441";
        if (w == "For") return "\u0444\u043e\u0440";
        if (w == "While") return "\u0432\u0430\u0439\u043b";
        if (w == "Return") return "\u0440\u0438\u0442\u0435\u0440\u043d";
        if (w == "Try") return "\u0442\u0440\u0430\u0439";
        if (w == "Catch") return "\u043a\u0435\u0442\u0447";
        if (w == "Finally") return "\u0444\u0430\u0439\u043d\u043b";
        if (w == "Throw") return "\u0442\u0440\u043e\u0443";
        if (w == "New") return "\u043d\u044c\u044e";
        if (w == "This") return "\u0434\u0456\u0441";
        if (w == "Super") return "\u0441\u0443\u043f\u0435\u0440";
        if (w == "Extends") return "\u0435\u043a\u0441\u0442\u0435\u043d\u0434\u0437";
        if (w == "Implements") return "\u0456\u043c\u043f\u043b\u0435\u043c\u0435\u043d\u0441";
        if (w == "Abstract") return "\u0430\u0431\u0441\u0442\u0440\u0430\u043a\u0442";
        if (w == "Static") return "\u0441\u0442\u0430\u0442\u0438\u043a";
        if (w == "Public") return "\u043f\u0430\u0431\u043b\u0456\u043a";
        if (w == "Private") return "\u043f\u0440\u0430\u0439\u0432\u0430\u0442";
        if (w == "Protected") return "\u043f\u0440\u043e\u0442\u0435\u043a\u0442\u0435\u0434";
        return nullptr;
    };

    std::string result;
    result.reserve(text.size());
    size_t i = 0;
    while (i < text.size()) {
        if (static_cast<unsigned char>(text[i]) > 127) {
            result += text[i];
            ++i;
            continue;
        }
        size_t j = i;
        while (j < text.size() &&
               ((text[j] >= 'A' && text[j] <= 'Z') ||
                (text[j] >= 'a' && text[j] <= 'z') ||
                text[j] == '.' || text[j] == '-' || text[j] == '#' || text[j] == '+')) {
            ++j;
        }
        if (j > i) {
            std::string word(text.substr(i, j - i));
            const char* uk = get_uk(word);
            if (uk) {
                result += uk;
            } else {
                bool all_caps = true;
                for (char c : word) {
                    if (c >= 'a' && c <= 'z') { all_caps = false; break; }
                }
                if (all_caps && word.size() <= 6) {
                    for (size_t k = 0; k < word.size(); ++k) {
                        if (k > 0) result += ' ';
                        char c = word[k];
                        if (c >= 'A' && c <= 'Z') {
                            static const std::string letter_names[26] = {
                                "а", "б", "ц", "д", "е", "еф", "ґ", "га", "і", "й", "ка", "ель", "ем",
                                "ен", "о", "пе", "кю", "ер", "ес", "те", "у", "в", "дубль-в", "ікс", "ігрек", "зет"
                            };
                            result += letter_names[c - 'A'];
                        } else {
                            result += c;
                        }
                    }
                } else {
                    result += word;
                }
            }
            i = j;
        } else {
            result += text[i];
            ++i;
        }
    }
    return result;
}

std::string TextPrep::transliterate_english(std::string_view text) {
    return transliterate_english_impl(text);
}

} // namespace holos

// Global map for English to Ukrainian transliteration (must be outside anonymous namespace)


