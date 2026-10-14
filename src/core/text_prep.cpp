#include "text_prep.h"

namespace holos {

// M0-заглушка: реальний порт (межі нарізки, тире, пунктуація) — у M1.
std::vector<std::string> TextPrep::split_to_parts(std::string_view text) {
    std::vector<std::string> parts;
    if (text.empty()) return parts;
    parts.emplace_back(text);
    return parts;
}

std::string TextPrep::strip_wrappers(std::string_view part) {
    return std::string(part);
}

} // namespace holos
