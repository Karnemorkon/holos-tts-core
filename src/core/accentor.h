#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>

namespace holos {

// Neural stress fallback (ukrainian_accentor LSTM, ONNX).
// Використовується коли словниковий пошук не дав наголос (OOV/неоднозначні).
// Модель: accentor-lite.onnx, входи sequences int64 [batch,29], вихід logits [batch,29].
// Токенізація: символи рядка (lowercase) -> id за словником 35 символів; OOV=<UNK>=1; PAD=0.
class Accentor {
public:
    explicit Accentor(const std::string& onnx_path);
    ~Accentor();
    Accentor(const Accentor&) = delete;
    Accentor& operator=(const Accentor&) = delete;

    bool loaded() const;

    // Повертає слово з вставленим U+0301 (комбінуючим наголосом) або ж саме слово,
    // якщо модель не змогла визначити позицію (індекс поза межами слова).
    std::string stress(std::string_view word) const;

private:
    struct Impl;
    Impl* impl_;
};

} // namespace holos
