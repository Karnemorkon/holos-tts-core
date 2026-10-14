#pragma once
#include <string>
#include <string_view>
#include <vector>
#include <cstdint>

namespace holos {

// Словниковий стресифікатор (порт ukrainian-word-stress без stanza/accentor).
// 98.7% словникових форм мають один наголос -> пошук у marisa-trie, без нейро.
// Неоднозначні (багато варіантів) та OOV слова повертаються БЕЗ наголосу (варіант A:
// TTS застосує дефолтну вимову). Відповідає поведінці Python on_ambiguity=Skip.
class StressDictionary {
public:
    // Завантажує stress.trie (marisa BytesTrie від ukrainian-word-stress).
    explicit StressDictionary(const std::string& trie_path);
    ~StressDictionary();
    StressDictionary(const StressDictionary&) = delete;
    StressDictionary& operator=(const StressDictionary&) = delete;

    bool loaded() const;
    size_t size() const; // кількість ключів

    // Повертає слово з вставленим U+0301 (комбінуючим наголосом) або ж саме слово,
    // якщо наголос не визначено (однозначний пропуск / OOV / неоднозначність).
    std::string stress(std::string_view word) const;

    // Сирі позиції наголосу (codepoint-індекси) для слова; порожній вектор = немає даних.
    std::vector<int> accent_positions(std::string_view word) const;

private:
    struct Impl;
    Impl* impl_;
};

} // namespace holos
