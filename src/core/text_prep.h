#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace holos {

// Текстовий препроцесинг: нарізка, нормалізація тире/пунктуації, обгортки, NFKC.
// Порт розділу app/tts_pipeline.py (_collapse_newlines, split_to_parts,
// _strip_wrappers, _apply_stress_markup, _standardize_dashes,
// _ensure_terminal_punct, normalize-частина _prepare_part).
struct TextPrep {
    // Переноси рядка -> '. ' або пробіли (щоб не різати посеред слова).
    static std::string collapse_newlines(std::string_view text);
    // Нарізує текст на частини по реченнях (межа групування ~100 символів,
    // розрив лише після ".?!" із наступним пробілом).
    static std::vector<std::string> split_to_parts(std::string_view text, bool group = true);
    // Прибирає крайові пробіли та ASCII-лапки.
    static std::string strip_wrappers(std::string_view text);
    // '+' -> комбінований наголос (U+0301) + NFKC-нормалізація.
    static std::string apply_stress_markup(std::string_view text);
    // Екзотичні тире/мінуси -> звичайний '-'.
    static std::string standardize_dashes(std::string_view text);
    // Речення мусить завершуватися пунктуацією ('.?!:-'), інакше додає '.'.
    static std::string ensure_terminal_punct(std::string text);
    // Повна нормалізація частини перед наголосом/фонемізацією:
    // strip_wrappers -> apply_stress_markup -> standardize_dashes ->
    // ensure_terminal_punct -> " - " -> ": ". Порожні частини -> порожній рядок.
    static std::string normalize_part(std::string_view part);
};

} // namespace holos
