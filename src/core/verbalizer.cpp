#include "verbalizer.h"

#include <cctype>
#include <string>

namespace holos {

namespace {

const char* kUnits[] = {
    "нуль", "один", "два", "три", "чотири", "п'ять", "шість", "сім", "вісім", "дев'ять",
    "десять", "одинадцять", "дванадцять", "тринадцять", "чотирнадцять", "п'ятнадцять",
    "шістнадцять", "сімнадцять", "вісімнадцять", "дев'ятнадцять"};
const char* kUnitsFem[] = {
    "нуль", "одна", "дві", "три", "чотири", "п'ять", "шість", "сім", "вісім", "дев'ять"};
const char* kTens[] = {
    "", "", "двадцять", "тридцять", "сорок", "п'ятдесят", "шістьдесят",
    "сімдесят", "вісімдесят", "дев'яносто"};
const char* kHundreds[] = {
    "", "сто", "двісті", "триста", "чотириста", "п'ятсот", "шістьсот",
    "сімсот", "вісімсот", "дев'ятсот"};

std::string say_upto999(int n, bool fem) {
    if (n <= 0) return {};
    std::string out;
    int h = n / 100, r = n % 100;
    if (h) out += kHundreds[h];
    if (r >= 20) {
        int t = r / 10, u = r % 10;
        if (!out.empty()) out += " ";
        out += kTens[t];
        if (u) { out += " "; out += (fem ? kUnitsFem : kUnits)[u]; }
    } else if (r >= 10) {
        if (!out.empty()) out += " ";
        out += kUnits[r];
    } else if (r > 0) {
        if (!out.empty()) out += " ";
        out += (fem ? kUnitsFem : kUnits)[r];
    }
    return out;
}

const char* plural_form(int n, const char* one, const char* few, const char* many) {
    int last2 = n % 100, last1 = n % 10;
    if (last1 == 1 && last2 != 11) return one;
    if (last1 >= 2 && last1 <= 4 && (last2 < 10 || last2 > 20)) return few;
    return many;
}

std::string say_int(long long n) {
    if (n == 0) return "нуль";
    if (n < 0) return "мінус " + say_int(-n);

    // Групи: мільярди, мільйони, тисячі, одиниці
    int billions = (int)(n / 1000000000LL); n %= 1000000000LL;
    int millions = (int)(n / 1000000LL);    n %= 1000000LL;
    int thousands = (int)(n / 1000LL);      n %= 1000LL;
    int units = (int)n;

    std::string out;
    auto add = [&](const std::string& s) {
        if (s.empty()) return;
        if (!out.empty()) out += " ";
        out += s;
    };

    if (billions) {
        add(say_upto999(billions, false));
        add(plural_form(billions, "мільярд", "мільярди", "мільярдів"));
    }
    if (millions) {
        add(say_upto999(millions, false));
        add(plural_form(millions, "мільйон", "мільйони", "мільйонів"));
    }
    if (thousands) {
        add(say_upto999(thousands, true)); // тисячі — жіночий
        add(plural_form(thousands, "тисяча", "тисячі", "тисяч"));
    }
    if (units) add(say_upto999(units, false));
    return out;
}

int atoi_n(const std::string& s, size_t pos, size_t len) {
    int v = 0;
    for (size_t i = 0; i < len && pos + i < s.size(); i++) {
        char c = s[pos + i];
        if (!std::isdigit(static_cast<unsigned char>(c))) return -1;
        v = v * 10 + (c - '0');
    }
    return v;
}

} // namespace

bool needs_verbalize(std::string_view text) {
    for (char c : text)
        if (std::isdigit(static_cast<unsigned char>(c))) return true;
    return false;
}

std::string verbalize(std::string_view text_in) {
    std::string text(text_in);
    if (!needs_verbalize(text)) return text;

    std::string out;
    out.reserve(text.size() * 2);
    size_t i = 0, n = text.size();

    while (i < n) {
        // Час: ГГ:MM (2+2), Г:MM (1+2), ГГ-MM, Г-MM
        {
            size_t hlen = 0;
            while (i + hlen < n && std::isdigit(static_cast<unsigned char>(text[i + hlen])) && hlen < 2) hlen++;
            if (hlen >= 1 && i + hlen < n && (text[i + hlen] == ':' || text[i + hlen] == '-')) {
                size_t mpos = i + hlen + 1;
                size_t mlen = 0;
                while (mpos + mlen < n && std::isdigit(static_cast<unsigned char>(text[mpos + mlen])) && mlen < 2) mlen++;
                if (mlen == 2) {
                    int hh = atoi_n(text, i, hlen);
                    int mm = atoi_n(text, mpos, mlen);
                    if (hh >= 0 && hh <= 23 && mm >= 0 && mm <= 59) {
                        if (mm == 0) {
                            int h12 = (hh == 0) ? 12 : (hh > 12 ? hh - 12 : hh);
                            out += say_upto999(h12, true) + " годині";
                        } else {
                            out += say_int(hh) + " годині " + say_upto999(mm, true) + " хвилин";
                        }
                        i = mpos + mlen;
                        continue;
                    }
                }
            }
        }

        // Дата ДД.ММ.РРРР або ДД/ММ/РРРР
        if (i + 10 <= n &&
            std::isdigit(static_cast<unsigned char>(text[i])) &&
            std::isdigit(static_cast<unsigned char>(text[i + 1])) &&
            (text[i + 2] == '.' || text[i + 2] == '/') &&
            std::isdigit(static_cast<unsigned char>(text[i + 3])) &&
            std::isdigit(static_cast<unsigned char>(text[i + 4])) &&
            (text[i + 5] == '.' || text[i + 5] == '/') &&
            std::isdigit(static_cast<unsigned char>(text[i + 6])) &&
            std::isdigit(static_cast<unsigned char>(text[i + 7])) &&
            std::isdigit(static_cast<unsigned char>(text[i + 8])) &&
            std::isdigit(static_cast<unsigned char>(text[i + 9]))) {
            int dd = atoi_n(text, i, 2);
            int mm = atoi_n(text, i + 3, 2);
            int yyyy = atoi_n(text, i + 6, 4);
            if (dd >= 1 && dd <= 31 && mm >= 1 && mm <= 12 && yyyy >= 1000) {
                out += say_upto999(dd, true) + " числа " + say_upto999(mm, true) +
                       " місяця " + say_int(yyyy) + " року";
                i += 10;
                continue;
            }
        }

        // Кардинальне число
        if (std::isdigit(static_cast<unsigned char>(text[i]))) {
            size_t j = i;
            while (j < n && std::isdigit(static_cast<unsigned char>(text[j]))) j++;
            std::string num = text.substr(i, j - i);
            if (num.size() > 12) num = num.substr(0, 12); // обрізка до мільярдів
            try {
                long long val = std::stoll(num);
                out += say_int(val);
            } catch (...) {
                out += text.substr(i, j - i);
            }
            i = j;
            continue;
        }

        out += text[i];
        i++;
    }
    return out;
}

} // namespace holos
