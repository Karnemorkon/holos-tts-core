#pragma once

#include <memory>
#include <string>

#include "verbalizer_model.h"

namespace holos {

// Детермінований препроцесинг Gemma-нормалізатора (порт normalize.py).
namespace gemma_prep {

// Голий рядок цифр -> поцифрове читання (MIN_DIGITS=7 або провідний нуль).
bool match_bare_digits(const std::string& text, std::string* digits);
std::string spell_digits(const std::string& digits);

// Роздільники розрядів у довгих "кількісних" числах (10-13 цифр, групи по 3).
std::string add_spacing(const std::string& text);

}  // namespace gemma_prep

// Gemma-3-270M verbalizer (ONNX int8, decoder-only, KV-кеш).
// Потрібні файли в model_dir: model.onnx, tokenizer.model.
class GemmaVerbalizer : public NeuralVerbalizerBase {
public:
    explicit GemmaVerbalizer(const std::string& model_dir);
    ~GemmaVerbalizer() override;

    GemmaVerbalizer(const GemmaVerbalizer&) = delete;
    GemmaVerbalizer& operator=(const GemmaVerbalizer&) = delete;

    bool ready() const override;

    std::string verbalize(const std::string& text) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

std::unique_ptr<GemmaVerbalizer> make_gemma_verbalizer(const std::string& model_dir);

}  // namespace holos
