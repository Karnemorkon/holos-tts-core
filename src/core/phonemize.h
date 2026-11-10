#pragma once
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "core/accentor.h"
#include "core/stress_dictionary.h"

namespace holos {

// Типовий шлях до словника користувача: змінна середовища HOLOS_STRESS_USER
// (якщо задана) або stress_user.txt поряд зі stress.trie (каталог моделей).
// Файл опційний — без нього використовується лише основний словник.
std::string default_user_stress_path(const std::string& trie_path);

// Наголос (U+0301) для тексту без фонемізації — рівно той самий шлях, що
// використовує Phonemizer::phonemize (словник -> accentor для OOV/омонімів).
// accentor може бути nullptr (тоді тільки словник).
std::string stress_text(const StressDictionary& dict, const Accentor* accentor,
                        std::string_view text);

// Текст -> фонеми: split_to_parts (по реченнях) -> normalize_part -> auto_stress
// (словник + словник користувача + neural accentor fallback) -> ipa_uk.transcribe.
// Повертає фонемний рядок на частину (одне речення = одна частина).
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

    // Кількість слів у словнику користувача (stress_user.txt біля stress.trie).
    size_t user_stress_size() const;

private:
    StressDictionary dict_;
    std::unique_ptr<Accentor> accentor_; // nullptr якщо не завантажено
};

} // namespace holos
