#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/stat.h>

#include "../src/core/stress_dictionary.h"
#include "../src/core/utf8.h"

// M0: self-contained перевірки UTF-8-утиліт (12.5 МБ stress.trie у репо немає).
// Перевірки словника виконуються лише коли stress.trie доступний (локально або
// в CI з завантаженими моделями); інакше тест gracefully skip-ає.
namespace {

int failures = 0;

void check(bool cond, const std::string& what) {
    if (!cond) {
        std::cout << "FAIL: " << what << "\n";
        ++failures;
    }
}

bool file_exists(const std::string& path) {
    struct stat st {};
    return ::stat(path.c_str(), &st) == 0;
}

std::string acute_at(std::string_view word, int cp_pos) {
    using namespace holos::utf8;
    return insert_at_cp(std::string(word), static_cast<size_t>(cp_pos), encode_u32(0x301));
}

int acute_count(const std::string& s) {
    int n = 0;
    for (size_t i = 0; i + 1 < s.size(); ++i)
        if (static_cast<unsigned char>(s[i]) == 0xCC &&
            static_cast<unsigned char>(s[i + 1]) == 0x81)
            ++n;
    return n;
}

void test_utf8() {
    using namespace holos::utf8;
    std::string w = "\xD0\xB2\xD0\xBE\xD0\xB4\xD0\xB0"; // "вода" (4 codepoint, 8 bytes)
    check(len(w) == 4, "utf8 len(вода) == 4");
    check(byte_offset(w, 0) == 0, "byte_offset(вода,0) == 0");
    check(byte_offset(w, 1) == 2, "byte_offset(вода,1) == 2");
    check(byte_offset(w, 4) == 8, "byte_offset(вода,4) == 8");

    // insert_at_cp: вставка ПЕРЕД codepoint-позицією (як Python s[:p]+ins+s[p:])
    std::string s = insert_at_cp(w, 4, encode_u32(0x301));
    check(len(s) == 5, "insert_at_cp(вода,4) -> 5 codepoints");
    std::string s2 = insert_at_cp(w, 1, encode_u32(0x301));
    check(len(s2) == 5 && s2.substr(0, 2) == w.substr(0, 2), "insert_at_cp(вода,1) перед 'о'");
    std::string s3 = insert_at_cp(w, 2, encode_u32(0x301));
    check(len(s3) == 5 && s3.substr(0, 4) == w.substr(0, 4), "insert_at_cp(вода,2) перед 'д'");
}

void test_dictionary(const std::string& trie_path) {
    holos::StressDictionary dict(trie_path);

    // Однозначне слово: stress.trie зберігає позицію ПІСЛЯ наголошеного
    // голосного (вода́ -> вставка після 'а'), як Python s[:p] + acute + s[p:].
    check(dict.accent_positions("вода") == std::vector<int>{4}, "accent_positions(вода) == {4}");
    check(dict.stress("вода") == acute_at("вода", 4), "stress(вода) == вода́");
    check(dict.stress("Вода") == acute_at("Вода", 4), "stress(Вода) == Вода́ (регістр)");

    // Багатозаписне слово з ОДНАКОВИМИ позиціями в усіх POS-варіантах: раніше
    // поверталося без наголосу (variant A), тепер наголос є — і їх два, як у
    // Python (поли́вки -> по́ли́вки).
    auto polyvky = dict.accent_positions("поливки");
    check(!polyvky.empty(), "accent_positions(поливки) не порожній (усі варіанти збігаються)");
    check(acute_count(dict.stress("поливки")) == 2, "stress(поливки) має два наголоси");

    // Апостроф: stress.trie зберігає лише ASCII-апостроф ('), у тексті
    // трапляються типографські ʼ (U+02BC) і ’ (U+2019). Пошук згортає їх,
    // але stress() повертає слово з ОРИГІНАЛЬНИМ апострофом.
    check(dict.accent_positions("м’який") == dict.accent_positions("м'який"),
          "accent_positions(м’який) == accent_positions(м'який)");
    check(dict.accent_positions("мʼякий") == dict.accent_positions("м'який"),
          "accent_positions(мʼякий) == accent_positions(м'який)");
    check(acute_count(dict.stress("м’який")) == 1, "stress(м’який) має наголос");
    check(dict.stress("м’який").find("\u2019") != std::string::npos,
          "stress(м’який) зберігає оригінальний апостроф ’");

    // Запис без тегів (займенник) перемагає обмежені NOUN-варіанти: вона́, вони́.
    check(dict.stress("вона") == acute_at("вона", 4), "stress(вона) == вона́");
    check(dict.stress("вони") == acute_at("вони", 4), "stress(вони) == вони́");

    // Справжня омонімія (усі варіанти з тегами, різні наголоси): accent_positions
    // порожній (наголос визначає accentor), але accent_variants їх повертає.
    check(dict.accent_positions("галузі").empty(), "accent_positions(галузі) порожній (омонім)");
    check(dict.accent_variants("галузі").size() > 1, "accent_variants(галузі) має кілька записів");
    check(dict.stress("галузі").find("\u0301") == std::string::npos, "stress(галузі) без наголосу");

    // OOV: жодних даних.
    check(dict.accent_positions("блаблабла").empty(), "accent_positions(OOV) порожній");
    check(dict.accent_variants("блаблабла").empty(), "accent_variants(OOV) порожній");
    check(dict.stress("блаблабла") == "блаблабла", "stress(OOV) не змінює слово");

    // stress_at: порожні позиції -> слово без змін; довільні позиції -> вставка.
    check(dict.stress_at("вода", {}) == "вода", "stress_at(вода,{}) == вода");
    check(dict.stress_at("вода", {2}) == acute_at("вода", 2), "stress_at(вода,{2})");

    // Словник користувача (stress_user.txt): формат «слово+», коментарі '#',
    // слова без '+' ігноруються, записи мають перевагу над trie.
    {
        const std::string upath = "stress_user_test.tmp.txt";
        {
            std::ofstream uf(upath);
            uf << "# коментар\n"
                  "вода+\n"
                  "к+ава\n"
                  "блаблабла+\n"
                  "без_наголосу\n";
        }
        holos::StressDictionary udict(trie_path, upath);
        check(udict.user_size() == 3, "user dict: 3 валідних записів (4-й без '+')");
        check(udict.accent_positions("вода") == std::vector<int>{4}, "user: вода == {4}");
        check(udict.accent_positions("кава") == std::vector<int>{2}, "user: кава == {2} (к+ава)");
        check(udict.accent_positions("Кава") == std::vector<int>{2}, "user: Кава (регістр)");
        check(udict.accent_positions("блаблабла") == std::vector<int>{9}, "user: OOV з наголосом");
        check(udict.stress("кава") == acute_at("кава", 2), "user: stress(кава) == кáва");
        check(udict.accent_positions("галузі").empty(),
              "user: слово без запису -> як у trie (омонім порожній)");
        std::remove(upath.c_str());
    }
}

} // namespace

int main(int argc, char** argv) {
    test_utf8();
    std::cout << "utf8 ok\n";

    // Шукаємо stress.trie: явний аргумент, потім типові місця (build/, repo root).
    std::vector<std::string> candidates;
    if (argc > 1) candidates.emplace_back(argv[1]);
    candidates.emplace_back("models/stress.trie");
    candidates.emplace_back("../models/stress.trie");
    std::string trie;
    for (const auto& c : candidates)
        if (file_exists(c)) { trie = c; break; }

    if (!trie.empty()) {
        test_dictionary(trie);
        std::cout << "dictionary ok (" << trie << ")\n";
    } else {
        std::cout << "dictionary skipped (немає stress.trie)\n";
    }

    if (failures) {
        std::cout << failures << " failure(s)\n";
        return 1;
    }
    return 0;
}
