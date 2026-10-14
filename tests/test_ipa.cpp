#include "../src/core/ipa_uk.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

// Reads a TSV golden file: each line is "input<TAB>expected".
// Runs holos::ipa::transcribe(input) and compares to expected.
int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: test_ipa <golden.tsv>\n";
        return 2;
    }
    std::ifstream in(argv[1]);
    if (!in) { std::cerr << "cannot open " << argv[1] << "\n"; return 2; }

    int total = 0, pass = 0;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        size_t tab = line.find('\t');
        if (tab == std::string::npos) continue;
        std::string input = line.substr(0, tab);
        std::string expected = line.substr(tab + 1);
        std::string got;
        bool err = false;
        try { got = holos::ipa::transcribe(input); }
        catch (const std::exception& e) { got = std::string("ERR:") + e.what(); err = true; }
        ++total;
        if (got == expected) { ++pass; }
        else {
            std::cout << "FAIL in=[" << input << "]\n  exp=[" << expected
                      << "]\n  got=[" << got << "]\n";
        }
    }
    std::cout << "IPA golden: " << pass << "/" << total << " pass\n";
    return pass == total ? 0 : 1;
}
