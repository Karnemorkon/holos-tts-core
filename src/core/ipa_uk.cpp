#include "ipa_uk.h"

namespace holos::ipa {

// M0-заглушка: реальний порт правил (voicing, orthographic replacements, тощо) — M1.
std::string transcribe(std::string_view text) {
    return std::string(text);
}

} // namespace holos::ipa
