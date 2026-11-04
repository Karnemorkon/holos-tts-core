#pragma once
#include "bundle.h"
#include "ort_path.h"

#include <onnxruntime_cxx_api.h>

#include <memory>
#include <string>
#include <vector>
#include <optional>
#include <map>
#include <functional>

namespace holos {

struct Encoded {
    std::vector<float> voice;
    std::vector<float> acoustic_tokens;
    std::vector<int64_t> frame_to_token;
    std::vector<float> f0;
    std::vector<float> energy;
    std::vector<int64_t> durations;
    size_t feature_dim = 0;
    size_t token_count = 0;
};

class BundleSynthesizer {
public:
    explicit BundleSynthesizer(const std::string& bundle_dir, int intra_threads = 0);
    ~BundleSynthesizer();
    
    BundleSynthesizer(const BundleSynthesizer&) = delete;
    BundleSynthesizer& operator=(const BundleSynthesizer&) = delete;
    
    std::vector<float> synthesize(const std::vector<int64_t>& phoneme_ids,
                                   const float* voice256, float speed = 1.0f) const;
    
    bool synthesize_stream(const std::vector<int64_t>& phoneme_ids,
                           const float* voice256, float speed,
                           const StreamingConfig& cfg,
                           std::function<bool(const std::vector<float>&)> on_chunk) const;
    
    uint32_t sample_rate() const { return bundle_->sample_rate(); }
    size_t samples_per_frame() const { return bundle_->samples_per_frame(); }
    size_t max_frames() const { return bundle_->max_frames(); }
    size_t voice_dim() const { return bundle_->voice_dim(); }
    const std::vector<std::string>& vocab() const { return bundle_->vocab(); }
    std::vector<std::string> voice_names() const { return bundle_->voice_names(); }
    const std::vector<float>* voice(const std::string& name) const { return bundle_->voice(name); }
    bool is_streaming_supported() const { return streaming_loaded_; }
    int intra_threads() const { return intra_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    int intra_ = 0;
    std::shared_ptr<Bundle> bundle_;
    bool streaming_loaded_ = false;
};

} // namespace holos
