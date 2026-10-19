#pragma once

#include <memory>
#include <string>

#include "verbalizer_model.h"

namespace holos {

// Neural verbalizer: M2M100 (ONNX int8) + SentencePiece.
// Конвертує цифри/дати/час в українські слова (паритет якості з Python CT2).
// Потрібні файли в model_dir: encoder_model_quantized.onnx,
// decoder_model_quantized.onnx, decoder_with_past_model_quantized.onnx,
// sentencepiece.bpe.model, verbalizer_config.json.
class NeuralVerbalizer : public NeuralVerbalizerBase {
public:
    explicit NeuralVerbalizer(const std::string& model_dir);
    ~NeuralVerbalizer() override;

    NeuralVerbalizer(const NeuralVerbalizer&) = delete;
    NeuralVerbalizer& operator=(const NeuralVerbalizer&) = delete;

    bool ready() const override;

    // Вербалізує текст; якщо цифр немає — повертає текст без змін.
    std::string verbalize(const std::string& text) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Фабрика: обирає backend за вмістом model_dir:
//   model.onnx + tokenizer.model          -> Gemma (decoder-only, KV-кеш)
//   encoder/decoder onnx + sentencepiece   -> M2M100
// nullptr, якщо model_dir порожній або моделі не завантажились.
std::unique_ptr<NeuralVerbalizerBase> make_neural_verbalizer(const std::string& model_dir);

}  // namespace holos
