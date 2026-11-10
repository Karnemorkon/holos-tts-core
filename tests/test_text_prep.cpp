// Golden parity for the text-prep stage (vs Python app.tts_pipeline text funcs).
#include "../src/core/text_prep.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using json = nlohmann::json;
using holos::TextPrep;

int main(int argc, char** argv) {
    std::string path = argc > 1 ? argv[1] : "textprep_golden.json";
    std::ifstream f(path);
    if (!f) { std::cerr << "cannot open golden: " << path << "\n"; return 2; }
    json j; f >> j;
    int pass = 0, fail = 0;
    for (const auto& c : j) {
        std::string kind = c.at("kind").get<std::string>();
        std::string in = c.at("in").get<std::string>();
        if (kind == "split") {
            auto got = TextPrep::split_to_parts(in, true);
            auto exp = c.at("out").get<std::vector<std::string>>();
            if (got == exp) { ++pass; } else {
                ++fail;
                std::cerr << "FAIL split [" << in << "]\n  exp:";
                for (auto& e : exp) std::cerr << " {" << e << "}";
                std::cerr << "\n  got:";
                for (auto& g : got) std::cerr << " {" << g << "}";
                std::cerr << "\n";
            }
        } else if (kind == "normalize") {
            std::string got = TextPrep::normalize_part(in);
            std::string exp = c.at("out").get<std::string>();
            if (got == exp) { ++pass; } else {
                ++fail;
                std::cerr << "FAIL normalize [" << in << "]\n  exp{" << exp
                          << "}\n  got{" << got << "}\n";
            }
        }
    }

    // English transliteration regression checks.
    struct TrCase { std::string in, exp; };
    const TrCase tr_cases[] = {
        {"NATO.", "нато."},
        {"hello", "хело"},
        {"Hello,", "хело,"},
        {"API-ключ", "е п і-ключ"},
        {"CI/CD", "сі ай ці ді"},
        {"NASA", "ен а ес а"},
        {"Windows 10", "віндоус 10"},
        {"C++", "сі плюс плюс"},
        {"Привіт, світе", "Привіт, світе"},
        {"Python.", "пайтон."},
    };
    for (const auto& c : tr_cases) {
        std::string got = TextPrep::transliterate_english(c.in);
        if (got == c.exp) { ++pass; } else {
            ++fail;
            std::cerr << "FAIL translit [" << c.in << "]\n  exp{" << c.exp
                      << "}\n  got{" << got << "}\n";
        }
    }

    // Ручний наголос '+' («прощаюча» обробка): '+' після голосної — як є;
    // '+' перед голосною — переносимо на голосну; без голосної — прибираємо.
    struct SmCase { std::string in, exp; };
    const SmCase sm_cases[] = {
        {"зам+ок", "замо\u0301к"},  // замóк
        {"ка+ва", "ка\u0301ва"},    // кáва
        {"н+оги", "но\u0301ги"},    // нóги (раніше давало «н́оги»)
        {"к+ава", "ка\u0301ва"},    // к+ава -> кáва (раніше давало «ќава»)
        {"абв+", "абв"},            // голосної немає -> '+' прибрано
    };
    for (const auto& c : sm_cases) {
        std::string got = TextPrep::apply_stress_markup(c.in);
        if (got == c.exp) { ++pass; } else {
            ++fail;
            std::cerr << "FAIL stress-markup [" << c.in << "]\n  exp{" << c.exp
                      << "}\n  got{" << got << "}\n";
        }
    }

    std::cout << pass << " passed, " << fail << " failed\n";
    return fail ? 1 : 0;
}
