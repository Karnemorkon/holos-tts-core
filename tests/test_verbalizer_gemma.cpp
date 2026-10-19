// Golden-тест Gemma-вербалізатора: C++ (ONNX int8) vs Python ONNX reference.
// Повертає 77 (CTest SKIP), якщо моделі недоступні.
#include "core/verbalizer_gemma.h"

#include <cstdio>
#include <fstream>
#include <string>

namespace {

std::string normalize_quotes(const std::string& s) {
    // ʼ (CB BC), ʻ (CB BB), ’ (E2 80 99) -> '
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == 0xCB && i + 1 < s.size() &&
            (static_cast<unsigned char>(s[i + 1]) == 0xBC ||
             static_cast<unsigned char>(s[i + 1]) == 0xBB)) {
            out += '\'';
            ++i;
            continue;
        }
        if (c == 0xE2 && i + 2 < s.size() &&
            static_cast<unsigned char>(s[i + 1]) == 0x80 &&
            static_cast<unsigned char>(s[i + 2]) == 0x99) {
            out += '\'';
            i += 2;
            continue;
        }
        out += s[i];
    }
    return out;
}

bool has_ascii_digits(const std::string& s) {
    for (char c : s)
        if (c >= '0' && c <= '9') return true;
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: test_verbalizer_gemma <golden.tsv> <models_dir>\n");
        return 2;
    }
    const std::string golden_path = argv[1];
    const std::string models_dir = argv[2];

    if (models_dir.empty()) {
        std::fprintf(stderr, "SKIP: HOLOS_TEST_VERBALIZER_GEMMA_DIR not set\n");
        return 77;
    }

    auto v = holos::make_gemma_verbalizer(models_dir);
    if (!v) {
        std::fprintf(stderr, "SKIP: models not available at %s\n", models_dir.c_str());
        return 77;
    }

    std::ifstream f(golden_path);
    if (!f) {
        std::fprintf(stderr, "cannot open golden: %s\n", golden_path.c_str());
        return 2;
    }

    int total = 0, match = 0, broken = 0;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        auto tab = line.find('\t');
        if (tab == std::string::npos) continue;
        std::string input = line.substr(0, tab);
        std::string expected = line.substr(tab + 1);

        std::string got = v->verbalize(input);
        ++total;

        if (got.empty() || has_ascii_digits(got)) {
            ++broken;
            std::fprintf(stderr, "[BROKEN] in : %s\n         out: %s\n",
                         input.c_str(), got.c_str());
        }
        if (normalize_quotes(got) == normalize_quotes(expected)) {
            ++match;
        } else {
            std::fprintf(stderr, "[DIFF] in   : %s\n       got  : %s\n       want : %s\n",
                         input.c_str(), got.c_str(), expected.c_str());
        }
    }

    std::fprintf(stderr, "match: %d/%d (%.0f%%), broken: %d\n", match, total,
                 total ? 100.0 * match / total : 0.0, broken);

    if (total == 0) return 2;
    if (broken > 0) return 1;
    if (match < static_cast<int>(0.9 * total)) return 1;
    return 0;
}
