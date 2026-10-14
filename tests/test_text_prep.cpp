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
    std::cout << pass << " passed, " << fail << " failed\n";
    return fail ? 1 : 0;
}
