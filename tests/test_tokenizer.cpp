// Golden parity for the CharTokenizer (vs Python CharTokenizer on phoneme strings).
#include "../src/core/tokenizer.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using json = nlohmann::json;
using holos::CharTokenizer;

int main(int argc, char** argv) {
    std::string vocab_path = argc > 1 ? argv[1] : "vocab.json";
    std::string golden_path = argc > 2 ? argv[2] : "tokenizer_golden.json";
    CharTokenizer tok = CharTokenizer::from_vocab_json(vocab_path);
    std::ifstream f(golden_path);
    if (!f) { std::cerr << "cannot open golden: " << golden_path << "\n"; return 2; }
    json j; f >> j;
    int pass = 0, fail = 0;
    for (const auto& c : j) {
        std::string in = c.at("in").get<std::string>();
        auto exp = c.at("out").get<std::vector<int64_t>>();
        auto got = tok(in);
        if (got == exp) { ++pass; } else {
            ++fail;
            std::cerr << "FAIL [" << in << "]\n  exp:";
            for (auto e : exp) std::cerr << " " << e;
            std::cerr << "\n  got:";
            for (auto g : got) std::cerr << " " << g;
            std::cerr << "\n";
        }
    }
    std::cout << pass << " passed, " << fail << " failed (vocab_size=" << tok.vocab_size() << ")\n";
    return fail ? 1 : 0;
}
