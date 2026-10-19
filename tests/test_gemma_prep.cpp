// Юніт-тести препроцесингу Gemma-нормалізатора (порт normalize.py).
#include "core/verbalizer_gemma.h"

#include <cstdio>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check_eq(const std::string& tag, const std::string& got,
              const std::string& want) {
    if (got != want) {
        ++failures;
        std::fprintf(stderr, "[FAIL] %s\n  got : %s\n  want: %s\n", tag.c_str(),
                     got.c_str(), want.c_str());
    }
}

void check_route(const std::string& text, bool want_handled,
                 const std::string& want_out) {
    std::string digits;
    bool handled = holos::gemma_prep::match_bare_digits(text, &digits);
    if (handled != want_handled) {
        ++failures;
        std::fprintf(stderr, "[FAIL] route(%s): handled=%d want=%d\n",
                     text.c_str(), (int)handled, (int)want_handled);
        return;
    }
    if (handled && !want_out.empty())
        check_eq("route(" + text + ")", holos::gemma_prep::spell_digits(digits),
                 want_out);
}

}  // namespace

int main() {
    using holos::gemma_prep::add_spacing;

    check_route("12345678", true,
                "один два три чотири п'ять шість сім вісім");
    check_route("10000001", true,
                "один нуль нуль нуль нуль нуль нуль один");
    check_route("000000", true, "нуль нуль нуль нуль нуль нуль");
    check_route("0000000", true, "нуль нуль нуль нуль нуль нуль нуль");
    check_route("123456789012", true, "");
    check_route("  918273645  ", true, "");
    check_route("42", false, "");
    check_route("007", true, "нуль нуль сім");
    check_route("0000", true, "нуль нуль нуль нуль");
    check_route("2026", false, "");
    check_route("10 000 001", false, "");
    check_route("0800 500 000", false, "");
    check_route("У звіті вказано число 12345678.", false, "");
    check_route("12345678.", false, "");
    check_route("№1024", false, "");
    check_route("", false, "");

    struct Case {
        const char* in;
        const char* out;
    };
    const Case cases[] = {
        {"Збитки оцінили в 7000000000 доларів.",
         "Збитки оцінили в 7 000 000 000 доларів."},
        {"Борг сягнув 2625414042726 гривень.",
         "Борг сягнув 2 625 414 042 726 гривень."},
        {"Оборот 9464922334 долари.", "Оборот 9 464 922 334 долари."},
        {"Картка 1234567890123456 заблокована.",
         "Картка 1234567890123456 заблокована."},
        {"Мій телефон 0671234567, дзвоніть.",
         "Мій телефон 0671234567, дзвоніть."},
        {"Наберіть 380671234567 звідусіль.",
         "Наберіть 380671234567 звідусіль."},
        {"Ідентифікатор 1234567890 у базі.",
         "Ідентифікатор 1234567890 у базі."},
        {"Рейс о 12:30 коштує 1234567890 гривень.",
         "Рейс о 12:30 коштує 1 234 567 890 гривень."},
        {"Артикул AB1234567890 на складі.",
         "Артикул AB1234567890 на складі."},
        {"Версія 1.2345678901 не існує.",
         "Версія 1.2345678901 не існує."},
        {"У нас 123456789 позицій.", "У нас 123456789 позицій."},
        {"IBAN UA123456789012345678901234567890.",
         "IBAN UA123456789012345678901234567890."},
        {"Рахунок на 1111 386,40 грн треба сплатити до 25.07.2026.",
         "Рахунок на 1 111 386,40 грн треба сплатити до 25.07.2026."},
        {"Борг 12 3456 789 гривень.", "Борг 123 456 789 гривень."},
        {"Оборот 1 111 386 гривень.", "Оборот 1 111 386 гривень."},
        {"Сума 10 000 001 грн.", "Сума 10 000 001 грн."},
        {"Оплата 4149 6293 1234 5678 пройшла.",
         "Оплата 4149 6293 1234 5678 пройшла."},
        {"Дзвони 067 123 45 67 зранку.", "Дзвони 067 123 45 67 зранку."},
        {"Рахунок 25.07.2026 і 12:30.", "Рахунок 25.07.2026 і 12:30."},
        {"", ""},
    };
    for (const auto& c : cases) check_eq(std::string("spacing(") + c.in + ")",
                                         add_spacing(c.in), c.out);

    if (failures) {
        std::fprintf(stderr, "FAILED: %d case(s)\n", failures);
        return 1;
    }
    std::printf("gemma prep: all cases OK\n");
    return 0;
}
