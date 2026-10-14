#pragma once
#include <string>
#include <string_view>

namespace holos {

// Фонемізація українського тексту в IPA (порт ipa_uk / Wiktionary uk-pronunciation).
namespace ipa {
    // Повертає IPA-транскрипцію наголошеного тексту (без перевірки наголосу).
    std::string transcribe(std::string_view text);
    // Той самий виклик з явною увімкненням перевірки наголосу (check_accent).
    std::string transcribe(std::string_view text, bool check_accent);
}

} // namespace holos
