// Кастомні голоси: валідація назв (як hybrid), .npy roundtrip (numpy-сумісний),
// рескан каталогу. Моделі не потрібні.
#include "core/custom_voices.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

int g_failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        ++g_failures;
        std::printf("FAIL: %s\n", what.c_str());
    } else {
        std::printf("ok: %s\n", what.c_str());
    }
}

std::string utf8_repeat(const std::string& ch, int n) {
    std::string s;
    for (int i = 0; i < n; ++i) s += ch;
    return s;
}

} // namespace

int main(int argc, char** argv) {
    using holos::CustomVoices;

    // --- Валідація назв: ^[\w \-]{1,40}$ (UTF+UCP), повний збіг. ---
    check(CustomVoices::valid_name("Іван"), "valid: кирилиця");
    check(CustomVoices::valid_name("my voice_1-test"), "valid: латиниця+пробіл+_-");
    check(CustomVoices::valid_name(utf8_repeat("ї", 40)), "valid: 40 кодпойнтів");
    check(!CustomVoices::valid_name(utf8_repeat("ї", 41)), "invalid: 41 кодпойнт");
    check(!CustomVoices::valid_name(""), "invalid: порожня");
    check(!CustomVoices::valid_name("a/b"), "invalid: слеш");
    check(!CustomVoices::valid_name("a.b"), "invalid: крапка");
    check(!CustomVoices::valid_name("a\nb"), "invalid: новий рядок всередині");
    check(!CustomVoices::valid_name("імʼя!"), "invalid: знак оклику");
    check(!CustomVoices::valid_name("a\n"), "invalid: трейлінг-новий рядок (повний збіг)");

    // --- Фікстура numpy: np.arange(256)/256 - 0.5 ---
    if (argc > 1) {
        bool ok = true;
        try {
            auto v = holos::load_npy_f32(argv[1]);
            ok = v.size() == 256;
            for (size_t i = 0; ok && i < v.size(); ++i)
                ok = std::fabs(double(v[i]) - (double(i) / 256.0 - 0.5)) < 1e-7;
        } catch (const std::exception& e) {
            std::printf("load_npy_f32(%s): %s\n", argv[1], e.what());
            ok = false;
        }
        check(ok, "numpy .npy фікстура читається (256 float32)");
    } else {
        check(false, "не передано шлях до voice_npy_fixture.npy");
    }

    // --- Roundtrip: save -> get/names/has -> rescan -> remove ---
    const std::string stamp = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const std::string dir = fs::temp_directory_path().string() + "/holos_voices_test_" + stamp;
    fs::remove_all(dir);
    {
        CustomVoices cv(dir);
        check(cv.enabled(), "каталог увімкнено");
        check(cv.names().empty(), "порожній каталог -> немає голосів");

        std::vector<float> emb(256);
        for (size_t i = 0; i < emb.size(); ++i) emb[i] = float(i) / 128.f - 1.f;

        cv.save("Тест голос", emb);
        check(cv.has("Тест голос"), "has() після save");
        auto got = cv.get("Тест голос");
        check(got == emb, "get() повертає ті самі 256 float");
        check(cv.names().size() == 1 && cv.names()[0] == "Тест голос",
              "names() містить збережений голос");

        // Файл має бути справжнім numpy .npy (magic + розмір 128+256*4).
        const std::string path = dir + "/Тест голос.npy";
        check(fs::exists(path), "файл <name>.npy створено");
        check(fs::file_size(path) == 128 + 256 * 4, "розмір .npy = 128 + 1024");

        // Перезапис.
        emb[0] = 42.f;
        cv.save("Тест голос", emb);
        check(cv.get("Тест голос")[0] == 42.f, "перезапис кастомного голосу");

        // Рескан: файл, створений повз API, зʼявляється.
        std::vector<float> ext(256, 0.25f);
        holos::save_npy_f32(dir + "/External Voice.npy", ext);
        check(cv.has("External Voice") && cv.get("External Voice")[5] == 0.25f,
              "рескан: зовнішній .npy підхоплено");

        // Пошкоджений .npy ігнорується (як hybrid), решта жива.
        {
            std::ofstream bad(dir + "/Broken.npy", std::ios::binary);
            bad << "not a numpy file";
        }
        auto names = cv.names();
        check(names.size() == 2, "пошкоджений .npy пропущено");

        // Помилки валідації/розмірності.
        bool threw = false;
        try { cv.save("bad/name", emb); } catch (const std::invalid_argument&) { threw = true; }
        check(threw, "save() з поганою назвою -> invalid_argument");
        threw = false;
        try { cv.save("short", std::vector<float>(100, 0.f)); }
        catch (const std::invalid_argument&) { threw = true; }
        check(threw, "save() з ембеддінгом != 256 -> invalid_argument");
        threw = false;
        try { cv.get("нема"); } catch (const std::out_of_range&) { threw = true; }
        check(threw, "get() невідомого -> out_of_range");

        // Видалення.
        check(cv.remove("External Voice"), "remove() існуючого -> true");
        check(!cv.has("External Voice"), "після remove() немає");
        check(!cv.remove("External Voice"), "повторний remove() -> false");
    }
    fs::remove_all(dir);

    // --- Вимкнений каталог (порожній шлях) ---
    {
        CustomVoices cv("");
        check(!cv.enabled(), "порожній шлях -> вимкнено");
        check(cv.names().empty(), "вимкнено -> names() порожній");
        bool threw = false;
        try { cv.save("x", std::vector<float>(256, 0.f)); }
        catch (const std::exception&) { threw = true; }
        check(threw, "вимкнено -> save() кидає");
    }

    if (g_failures) std::printf("\n%d перевірок провалено\n", g_failures);
    else std::printf("\nусі перевірки custom_voices пройдено\n");
    return g_failures ? 1 : 0;
}
