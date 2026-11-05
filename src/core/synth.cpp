#include "synth.h"
#include "bundle.h"
#include "bundle_synthesizer.h"
#include "cpu.h"
#include "ort_path.h"

#include <onnxruntime_cxx_api.h>
#include <filesystem>
#include <fstream>

#include <algorithm>
#include <stdexcept>
#include <memory>

namespace holos {

namespace {

struct InputSpec {
    std::string name;
    ONNXTensorElementDataType elem;
    std::vector<int64_t> shape;
    size_t rank() const { return shape.size(); }
    int64_t last_dim() const { return shape.empty() ? -1 : shape.back(); }
};

struct Binding {
    std::string phonemes;
    std::string tokens;
    std::string lengths;
    std::string voice;
    std::vector<std::string> controls;
};

constexpr int64_t kVoiceDim = 256;

Binding bind_inputs(const std::vector<InputSpec>& specs) {
    Binding b;
    for (const auto& s : specs) {
        std::string low = s.name;
        for (char& c : low) c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
        if (s.elem == ONNX_TENSOR_ELEMENT_DATA_TYPE_STRING) {
            b.phonemes = s.name;
        } else if ((s.elem == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) &&
                   (s.last_dim() == kVoiceDim || low.find("voice") != std::string::npos ||
                    low.find("style") != std::string::npos)) {
            b.voice = s.name;
        } else if (s.elem == ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64 && s.rank() <= 1) {
            b.lengths = s.name;
        } else if (s.elem == ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64) {
            b.tokens = s.name;
        } else if (s.elem == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT && s.rank() <= 1) {
            b.controls.push_back(s.name);
        } else {
            throw std::runtime_error("unknown ONNX input node: " + s.name);
        }
    }
    if (b.phonemes.empty() && (b.tokens.empty() || b.voice.empty()))
        throw std::runtime_error("failed to bind ONNX inputs");
    return b;
}

struct LegacyImpl {
    Ort::Env env;
    Ort::SessionOptions opts;
    std::unique_ptr<Ort::Session> session;
    Binding binding;
    int intra = 0;

    LegacyImpl(const std::string& path, int threads) {
        intra = threads > 0 ? threads : default_onnx_threads();
        opts.SetIntraOpNumThreads(intra);
        opts.SetInterOpNumThreads(1);
        opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        // Як в апстрімі (patriotyk/HolosTTS): без spin-wait intra-op пул ORT.
        opts.AddConfigEntry("session.intra_op.allow_spinning", "0");
        session = std::make_unique<Ort::Session>(env, holos::ort_path(path).c_str(), opts);

        Ort::AllocatorWithDefaultOptions alloc;
        std::vector<InputSpec> specs;
        size_t n = session->GetInputCount();
        for (size_t i = 0; i < n; ++i) {
            auto nm = session->GetInputNameAllocated(i, alloc);
            auto typeinfo = session->GetInputTypeInfo(i);
            auto tensorinfo = typeinfo.GetTensorTypeAndShapeInfo();
            auto elem = tensorinfo.GetElementType();
            auto shape = tensorinfo.GetShape();
            specs.push_back(InputSpec{std::string(nm.get()), elem, std::move(shape)});
        }
        binding = bind_inputs(specs);
    }

    std::vector<float> generate(const std::vector<int64_t>& phoneme_ids,
                                const float* voice256, float speed) const {
        auto& s = *session;
        const Binding& b = binding;
        Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        Ort::AllocatorWithDefaultOptions alloc;

        std::vector<int64_t> tokens;
        if (b.phonemes.empty()) {
            tokens.reserve(phoneme_ids.size() + 2);
            tokens.push_back(0);
            tokens.insert(tokens.end(), phoneme_ids.begin(), phoneme_ids.end());
            tokens.push_back(0);
        }

        std::vector<int64_t> len_buf;
        std::vector<float> ctl_buf;
        ctl_buf.reserve(b.controls.size());

        std::vector<Ort::Value> inputs;
        std::vector<const char*> in_names;
        auto add_named = [&](const std::string& name) { in_names.push_back(name.c_str()); };

        if (!b.tokens.empty()) {
            std::vector<int64_t> shape = {1, static_cast<int64_t>(tokens.size())};
            inputs.push_back(Ort::Value::CreateTensor<int64_t>(mem, tokens.data(), tokens.size(), shape.data(), 2));
            add_named(b.tokens);
        }
        if (!b.lengths.empty()) {
            len_buf.push_back(static_cast<int64_t>(tokens.size()));
            std::vector<int64_t> shape = {1};
            inputs.push_back(Ort::Value::CreateTensor<int64_t>(mem, len_buf.data(), 1, shape.data(), 1));
            add_named(b.lengths);
        }
        if (!b.voice.empty()) {
            std::vector<int64_t> shape = {1, kVoiceDim};
            inputs.push_back(Ort::Value::CreateTensor<float>(mem, const_cast<float*>(voice256), kVoiceDim, shape.data(), 2));
            add_named(b.voice);
        }
        for (const auto& cname : b.controls) {
            std::string low = cname;
            for (char& c : low) c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
            ctl_buf.push_back((low.find("speed") != std::string::npos) ? speed : 1.0f);
            std::vector<int64_t> shape = {1};
            inputs.push_back(Ort::Value::CreateTensor<float>(mem, &ctl_buf.back(), 1, shape.data(), 1));
            add_named(cname);
        }

        const char* out_name = "audio";
        auto outputs = s.Run(Ort::RunOptions{nullptr}, in_names.data(), inputs.data(), inputs.size(), &out_name, 1);
        auto& audio = outputs[0];
        size_t count = audio.GetTensorTypeAndShapeInfo().GetElementCount();
        const float* data = audio.GetTensorMutableData<float>();
        return std::vector<float>(data, data + count);
    }
};

SynthesizerMode detect_mode(const std::string& path, SynthesizerMode requested) {
    if (requested != SynthesizerMode::Auto) return requested;
    
    std::filesystem::path p(path);
    if (std::filesystem::is_directory(p)) {
        auto manifest = p / "holos_onnx.json";
        if (std::filesystem::exists(manifest)) return SynthesizerMode::Bundle;
    }
    return SynthesizerMode::Legacy;
}

} // namespace

struct Synthesizer::Impl {
    SynthesizerMode mode = SynthesizerMode::Legacy;
    int intra = 0;
    std::unique_ptr<LegacyImpl> legacy;
    std::unique_ptr<BundleSynthesizer> bundle;
    
    Impl(const std::string& path, int threads, SynthesizerMode requested)
        : mode(detect_mode(path, requested)) {
        
        if (mode == SynthesizerMode::Bundle) {
            bundle = std::make_unique<BundleSynthesizer>(path, threads);
        } else {
            legacy = std::make_unique<LegacyImpl>(path, threads);
        }
    }
};

Synthesizer::Synthesizer(const std::string& path, int intra_threads, SynthesizerMode mode)
    : impl_(new Impl(path, intra_threads, mode)) {}

Synthesizer::~Synthesizer() = default;

SynthesizerMode Synthesizer::mode() const { return impl_->mode; }

int Synthesizer::intra_threads() const { return impl_->intra; }

BundleSynthesizer* Synthesizer::bundle_impl() const { return impl_->bundle.get(); }

std::vector<float> Synthesizer::generate(const std::vector<int64_t>& phoneme_ids,
                                         const float* voice256, float speed) const {
    if (impl_->mode == SynthesizerMode::Bundle) {
        return impl_->bundle->synthesize(phoneme_ids, voice256, speed);
    } else {
        return impl_->legacy->generate(phoneme_ids, voice256, speed);
    }
}

std::optional<uint32_t> Synthesizer::sample_rate() const {
    if (impl_->mode == SynthesizerMode::Bundle) return impl_->bundle->sample_rate();
    return std::nullopt;
}

std::optional<size_t> Synthesizer::samples_per_frame() const {
    if (impl_->mode == SynthesizerMode::Bundle) return impl_->bundle->samples_per_frame();
    return std::nullopt;
}

std::optional<size_t> Synthesizer::max_frames() const {
    if (impl_->mode == SynthesizerMode::Bundle) return impl_->bundle->max_frames();
    return std::nullopt;
}

std::optional<size_t> Synthesizer::voice_dim() const {
    if (impl_->mode == SynthesizerMode::Bundle) return impl_->bundle->voice_dim();
    return std::nullopt;
}

std::optional<std::vector<std::string>> Synthesizer::voice_names() const {
    if (impl_->mode == SynthesizerMode::Bundle) return impl_->bundle->voice_names();
    return std::nullopt;
}

std::optional<const std::vector<float>*> Synthesizer::voice(const std::string& name) const {
    if (impl_->mode == SynthesizerMode::Bundle) return impl_->bundle->voice(name);
    return std::nullopt;
}

std::optional<std::vector<std::string>> Synthesizer::vocab() const {
    if (impl_->mode == SynthesizerMode::Bundle) return impl_->bundle->vocab();
    return std::nullopt;
}

bool Synthesizer::is_streaming_supported() const {
    if (impl_->mode == SynthesizerMode::Bundle) return impl_->bundle->is_streaming_supported();
    return false;
}

} // namespace holos
