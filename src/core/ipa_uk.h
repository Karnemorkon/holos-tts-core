#pragma once
#include <string>
#include <string_view>

namespace holos {

// Фонемізація українського тексту в IPA (порт ipa_uk / Wiktionary uk-pronunciation).
// M0: мінімальна заглушка, повний порт 404-рядкового алгоритму — у M1 (з golden-тестами).
namespace ipa {
    // Повертає IPA-транскрипцію наголошеного тексту.
    std::string transcribe(std::string_view text);
}

} // namespace holos
