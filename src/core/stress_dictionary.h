#pragma once
#include <string>
#include <string_view>
#include <vector>
#include <cstdint>
#include <optional>

namespace holos {

// Словниковий стресифікатор (порт ukrainian-word-stress без stanza/accentor).
// 98.7% словникових форм мають один наголос -> пошук у marisa-trie, без нейро.
// Неоднозначні записи: якщо всі POS-варіанти вказують однакові позиції або є
// запис без тегів (займенник/службове слово) — наголос береться зі словника;
// інакше слово повертається БЕЗ наголосу (Python on_ambiguity=Skip) і його
// обробляє accentor. Апострофи ʼ/’ згортаються до ASCII-апострофа словника.
//
// Опційно завантажується словник користувача (текстовий файл, див. stress_user.txt):
// рядки на кшталт «ка+ва» («+» після наголошеної голосної), коментарі «#».
// Записи користувача мають ПЕРЕВАГУ над stress.trie — можна виправляти
// невірні наголоси основного словника без перезбірки trie.
class StressDictionary {
public:
    // Завантажує stress.trie (marisa BytesTrie від ukrainian-word-stress) та,
    // за наявності, словник користувача user_path (порожній рядок = без нього).
    explicit StressDictionary(const std::string& trie_path,
                              const std::string& user_path = "");
    ~StressDictionary();
    StressDictionary(const StressDictionary&) = delete;
    StressDictionary& operator=(const StressDictionary&) = delete;

    bool loaded() const;
    size_t size() const; // кількість ключів

    // Кількість слів у словнику користувача (0 = файл не завантажено).
    size_t user_size() const;

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

    // Перечитує словник користувача з user_path (після редагування файлу,
    // напр. через веб-інтерфейс). Потокобезпечно: синтез може працювати в цей
    // момент. Якщо user_path порожній або файл зник — записів стає 0.
    void reload_user();

    // Шлях до файлу словника користувача ("" = не використовується).
    const std::string& user_path() const;

    // ---- Робота з файлом словника користувача (для веб-інтерфейсу/API) ----

    // Один запис словника для відображення.
    struct UserRecord {
        std::string raw;      // рядок як у файлі (без коментаря)
        std::string word;     // слово без '+' і без наголосу
        std::string stressed; // слово з наголосом (U+0301)
    };

    // Результат валідації нового запису («ка+ва»).
    struct UserEntryCheck {
        bool ok = false;
        std::string raw;      // вхідний рядок, обрізаний (без коментаря)
        std::string word;     // слово без '+' і без наголосу
        std::string stressed; // слово з U+0301
        std::string error;    // пояснення проблеми (українською), якщо !ok
    };

    // Валідує запис «слово+»: одне слово без пробілів, '+' дає рівно один
    // наголос («прощаюча» обробка як у тексті: к+ава -> кáва).
    static UserEntryCheck check_user_entry(std::string_view input);

    // Читає записи файлу (файл не існує -> порожній вектор; некоректні рядки
    // та коментарі не повертаються).
    static std::vector<UserRecord> user_file_read(const std::string& path);

    // Додає або замінює запис у файлі (рівність — за словом); створює файл за
    // потреби. Зберігає коментарі та інші рядки. Запис атомарний (tmp+rename).
    // false + error (українською) при відмові.
    static bool user_file_upsert(const std::string& path, std::string_view input,
                                 std::string* error = nullptr);

    // Видаляє записи слова з файлу (зберігаючи коментарі та інші рядки).
    // false + error, якщо слова немає у файлі.
    static bool user_file_remove(const std::string& path, std::string_view word,
                                 std::string* error = nullptr);

private:
    struct Impl;
    Impl* impl_;

    // Пошук у словнику користувача (ключ: lower + згорнуті апострофи);
    // нічого не вказує в мапу — повертає копію під shared-локом (безпечніше
    // за reload, який міняє мапу під exclusive-локом).
    std::optional<std::vector<int>> user_find(std::string_view word) const;
};

} // namespace holos
