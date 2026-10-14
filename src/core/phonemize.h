#pragma once
#include <memory>
#include <string>
#include <vector>

#include "core/accentor.h"
#include "core/stress_dictionary.h"

namespace holos {

// Текст -> фонеми: split_to_parts -> normalize_part -> auto_stress (словник +
// neural accentor fallback) -> ipa_uk.transcribe. Повертає фонемний рядок на частину.
class Phonemizer {
public:
    // accentor_onnx: шлях до accentor-lite.onnx (нейрословниковий fallback);
    // порожній рядок = без accentor (тільки словник, варіант A).
    Phonemizer(const std::string& stress_trie_path, const std::string& accentor_onnx = "");
    ~Phonemizer();
    Phonemizer(const Phonemizer&) = delete;
    Phonemizer& operator=(const Phonemizer&) = delete;

    // raw_stress=true: пропустити автонаголос (у тексті вже є власні +/U+0301).
    std::vector<std::string> phonemize(const std::string& text, bool raw_stress = false) const;

private:
    StressDictionary dict_;
    std::unique_ptr<Accentor> accentor_; // nullptr якщо не завантажено
};

} // namespace holos
