#pragma once
#include <string>
#include <string_view>
#include <vector>
#include <cstdint>

namespace holos {

// Словниковий стресифікатор (порт ukrainian-word-stress без stanza/accentor).
// 98.7% словникових форм мають один наголос -> пошук у marisa-trie, без нейро.
// Неоднозначні записи: якщо всі POS-варіанти вказують однакові позиції або є
// запис без тегів (займенник/службове слово) — наголос береться зі словника;
// інакше слово повертається БЕЗ наголосу (Python on_ambiguity=Skip) і його
// обробляє accentor. Апострофи ʼ/’ згортаються до ASCII-апострофа словника.
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

    // Сирі позиції наголосу (codepoint-індекси) для слова; порожній вектор =
    // або OOV, або справжня омонімія (тоді наголос визначає accentor).
    std::vector<int> accent_positions(std::string_view word) const;

    // Усі словникові записи наголосу для слова (порожній = OOV). Кожен запис —
    // список codepoint-позицій наголосу. Потрібен, щоб accentor не вигадав
    // позицію, якої немає в жодному варіанті.
    std::vector<std::vector<int>> accent_variants(std::string_view word) const;

    // Вставляє U+0301 у задані codepoint-позиції (порожні позиції -> слово як є).
    std::string stress_at(std::string_view word, const std::vector<int>& positions) const;

private:
    struct Impl;
    Impl* impl_;
};

} // namespace holos
