// Dev utility: dump stressed text (U+0301) for each stdin line.
//
// Важливо: це ТОЙ САМИЙ шлях, що Phonemizer::phonemize (holos::stress_text),
// а не копія логіки — інакше A/B-вимірювання наголосів міряють не те, що
// працює в продакшні.
#include "core/accentor.h"
#include "core/phonemize.h"
#include "core/stress_dictionary.h"

#include <iostream>
#include <memory>
#include <string>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: stress-dump <stress.trie> [accentor.onnx]\n";
        return 2;
    }
    holos::StressDictionary dict(argv[1]);
    std::unique_ptr<holos::Accentor> accentor;
    if (argc > 2) accentor = std::make_unique<holos::Accentor>(argv[2]);

    std::string line;
    while (std::getline(std::cin, line))
        std::cout << holos::stress_text(dict, accentor.get(), line) << '\n';
    return 0;
}
