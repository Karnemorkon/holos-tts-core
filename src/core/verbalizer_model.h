#pragma once

#include <string>

namespace holos {

// Спільний інтерфейс нейронних вербалізаторів (M2M100, Gemma).
class NeuralVerbalizerBase {
public:
    virtual ~NeuralVerbalizerBase() = default;

    virtual bool ready() const = 0;

    // Вербалізує текст; якщо цифр немає — повертає текст без змін.
    virtual std::string verbalize(const std::string& text) = 0;
};

}  // namespace holos
