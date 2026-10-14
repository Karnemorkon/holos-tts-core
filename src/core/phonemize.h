#pragma once
#include <string>
#include <vector>

#include "core/stress_dictionary.h"

namespace holos {

// Текст -> фонеми: split_to_parts -> normalize_part -> auto_stress (словник,
// варіант A) -> ipa_uk.transcribe. Повертає фонемний рядок на частину
// (порожні частини пропускаються).
class Phonemizer {
public:
    Phonemizer(const std::string& stress_trie_path);

    // raw_stress=true: пропустити автонаголос (у тексті вже є власні +/U+0301).
    std::vector<std::string> phonemize(const std::string& text, bool raw_stress = false) const;

private:
    StressDictionary dict_;
};

} // namespace holos
