#pragma once
#include <string>
#include <vector>

namespace holos {

// ONNX voice encoder (voice_encoder.onnx): wave float32 [1,N] @24kHz -> [1,256].
// Порожній/відсутній шлях -> вимкнено (ready()==false, зайвий RSS не платимо).
// ORT-сесія створюється ліниво при першому encode() (потокобезпечно).
class VoiceEncoder {
public:
    explicit VoiceEncoder(const std::string& onnx_path, int intra_threads = 0);
    ~VoiceEncoder();
    VoiceEncoder(const VoiceEncoder&) = delete;
    VoiceEncoder& operator=(const VoiceEncoder&) = delete;

    bool ready() const { return impl_ != nullptr; }
    // 256-float ембеддінг; кидає std::runtime_error.
    std::vector<float> encode(const std::vector<float>& wave24k) const;

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace holos
