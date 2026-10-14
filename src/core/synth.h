#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace holos {

// ONNX-генератор (holos_cpu_int8.onnx) через ONNX Runtime C++.
// tokens int64 [1,N] (BOS=0 + токени фонем + EOS=0), voice float32 [1,256],
// speed float32 [1] -> audio float32 [1,S].
class Synthesizer {
public:
    explicit Synthesizer(const std::string& onnx_path, int intra_threads = 0);
    ~Synthesizer();
    Synthesizer(const Synthesizer&) = delete;
    Synthesizer& operator=(const Synthesizer&) = delete;

    // Токени фонем (БЕЗ BOS/EOS) + голос (256 float) + швидкість.
    // BOS/EOS=0 додаються всередині (як [0] + tokenizer(phonemes) + [0]).
    std::vector<float> generate(const std::vector<int64_t>& phoneme_ids,
                                const float* voice256, float speed = 1.0f) const;

    int intra_threads() const;

private:
    struct Impl;
    Impl* impl_;
};

} // namespace holos
