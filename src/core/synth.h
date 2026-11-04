#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <functional>

#include "bundle.h"
#include "bundle_synthesizer.h"

namespace holos {

enum class SynthesizerMode {
    Auto,
    Legacy,
    Bundle
};

class Synthesizer {
public:
    explicit Synthesizer(const std::string& path, int intra_threads = 0, SynthesizerMode mode = SynthesizerMode::Auto);
    ~Synthesizer();
    Synthesizer(const Synthesizer&) = delete;
    Synthesizer& operator=(const Synthesizer&) = delete;

    SynthesizerMode mode() const;

    std::vector<float> generate(const std::vector<int64_t>& phoneme_ids,
                                const float* voice256, float speed = 1.0f) const;

    BundleSynthesizer* bundle_impl() const;

    template<typename F>
    bool generate_stream(const std::vector<int64_t>& phoneme_ids,
                         const float* voice256, float speed,
                         const StreamingConfig& cfg,
                         F&& on_chunk) const {
        if (this->mode() != SynthesizerMode::Bundle) return false;
        auto* b = this->bundle_impl();
        if (!b) return false;
        return b->synthesize_stream(phoneme_ids, voice256, speed, cfg,
                                    std::forward<F>(on_chunk));
    }

    std::optional<uint32_t> sample_rate() const;
    std::optional<size_t> samples_per_frame() const;
    std::optional<size_t> max_frames() const;
    std::optional<size_t> voice_dim() const;
    std::optional<std::vector<std::string>> voice_names() const;
    std::optional<const std::vector<float>*> voice(const std::string& name) const;
    std::optional<std::vector<std::string>> vocab() const;
    bool is_streaming_supported() const;

    int intra_threads() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace holos
