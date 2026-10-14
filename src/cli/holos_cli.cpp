#include <iostream>
#include <string>
#include <vector>

#include "core/stress_dictionary.h"

#ifndef HOLOS_VERSION
#define HOLOS_VERSION "0.1.0"
#endif

static void usage() {
    std::cerr
        << "holos-cli " << HOLOS_VERSION << " — C++ native core for HolosTTS (Ukrainian)\n"
        << "Usage:\n"
        << "  holos-cli stress <stress.trie> <word> [word...]\n"
        << "  holos-cli version\n"
        << "\nM0 scaffold: word-stress dictionary (marisa-trie, dictionary-only, option A).\n";
}

int main(int argc, char** argv) {
    if (argc < 2) { usage(); return 2; }
    std::string cmd = argv[1];

    if (cmd == "version") {
        std::cout << HOLOS_VERSION << "\n";
        return 0;
    }

    if (cmd == "stress") {
        if (argc < 4) { usage(); return 2; }
        std::string trie_path = argv[2];
        try {
            holos::StressDictionary dict(trie_path);
            std::cerr << "[holos] stress.trie keys=" << dict.size() << "\n";
            for (int i = 3; i < argc; i++) {
                std::string w = argv[i];
                auto pos = dict.accent_positions(w);
                std::string s = dict.stress(w);
                std::cout << w << " -> " << s << "   [pos:";
                for (int p : pos) std::cout << " " << p;
                std::cout << "]\n";
            }
        } catch (const std::exception& e) {
            std::cerr << "error: " << e.what() << "\n";
            return 1;
        }
        return 0;
    }

    usage();
    return 2;
}
