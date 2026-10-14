#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace holos {

// Текстовий препроцесинг: нарізка, нормалізація тире/пунктуації, обгортки.
// Порт розділу 4 app/tts_pipeline.py (split_to_parts, _strip_wrappers тощо).
// M0: мінімальна заглушка, повний порт у M1.
struct TextPrep {
    // Нарізує текст на частини для паралельної генерації (межа ~100 символів).
    static std::vector<std::string> split_to_parts(std::string_view text);
    // Прибирає службові обгортки з частини.
    static std::string strip_wrappers(std::string_view part);
};

} // namespace holos
