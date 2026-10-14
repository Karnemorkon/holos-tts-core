#pragma once
#include <string>
#include <string_view>

namespace holos {

// Rule-based вербалізація цифр/дат/часу в текст (українською).
// Покриває: кардинальні числа 0..999 999 999, дати ДД.ММ.РРРР/ДД/ММ/РРРР,
// час ГГ:ММ/ГГ-ММ, відсотки. НЕ потребує нейромоделей.
// Повертає текст з розгорнутими числами; якщо цифр немає — текст без змін.
std::string verbalize(std::string_view text);

// True, якщо текст містить цифри (перевірка потреби вербалізації).
bool needs_verbalize(std::string_view text);

} // namespace holos
