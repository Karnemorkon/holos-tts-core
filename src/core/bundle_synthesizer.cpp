#include "bundle_synthesizer.h"
#include "bundle.h"
#include "cpu.h"
#include "ort_path.h"
#include "synth.h"

#include <onnxruntime_cxx_api.h>

#include <algorithm>
#include <stdexcept>
#include <cmath>
#include <numeric>

namespace holos {

constexpr int64_t kVoiceDim = 256;
constexpr float kStreamSilenceMarginSeconds = 0.010f;
constexpr size_t kStreamFadeInSamples = 120;

// Helper to convert vector<string> to const char* const*
static std::vector<const char*> to_cstr_vec(const std::vector<std::string>& vec) {
    std::vector<const char*> result;
    result.reserve(vec.size());
    for (const auto& s : vec) result.push_back(s.c_str());
    return result;
}

struct SessionWrapper {
    Ort::Env env;
    Ort::SessionOptions opts;
    std::unique_ptr<Ort::Session> session;
    std::vector<std::string> input_names;
    std::vector<std::string> output_names;
    std::vector<Ort::TypeInfo> input_types;
    std::vector<Ort::TypeInfo> output_types;
    
    SessionWrapper(const std::string& path, int intra_threads) {
        env = Ort::Env(ORT_LOGGING_LEVEL_WARNING, "holos-bundle");
        opts.SetIntraOpNumThreads(intra_threads > 0 ? intra_threads : default_onnx_threads());
        opts.SetInterOpNumThreads(1);
        opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        session = std::make_unique<Ort::Session>(env, holos::ort_path(path).c_str(), opts);
        
        Ort::AllocatorWithDefaultOptions alloc;
        size_t n_in = session->GetInputCount();
        input_names.reserve(n_in);
        input_types.reserve(n_in);
        for (size_t i = 0; i < n_in; ++i) {
            auto name = session->GetInputNameAllocated(i, alloc);
            input_names.emplace_back(name.get());
            input_types.push_back(session->GetInputTypeInfo(i));
        }
        size_t n_out = session->GetOutputCount();
        output_names.reserve(n_out);
        output_types.reserve(n_out);
        for (size_t i = 0; i < n_out; ++i) {
            auto name = session->GetOutputNameAllocated(i, alloc);
            output_names.emplace_back(name.get());
            output_types.push_back(session->GetOutputTypeInfo(i));
        }
    }
    
    int64_t find_input_index(const std::string& name) const {
        for (size_t i = 0; i < input_names.size(); ++i) {
            if (input_names[i] == name) return static_cast<int64_t>(i);
        }
        return -1;
    }
    
    int64_t find_output_index(const std::string& name) const {
        for (size_t i = 0; i < output_names.size(); ++i) {
            if (output_names[i] == name) return static_cast<int64_t>(i);
        }
        return -1;
    }
    
    bool has_input(const std::string& name) const {
        return find_input_index(name) >= 0;
    }
    
    bool has_output(const std::string& name) const {
        return find_output_index(name) >= 0;
    }
};

struct BundleSynthesizer::Impl {
    std::shared_ptr<Bundle> bundle;
    int intra;
    
    std::unique_ptr<SessionWrapper> encoder;
    std::unique_ptr<SessionWrapper> prosody;
    std::unique_ptr<SessionWrapper> decoder_exact;
    std::unique_ptr<SessionWrapper> decoder_window;
    std::unique_ptr<SessionWrapper> source;
    
    int64_t enc_acoustic = -1;
    int64_t enc_prosodic = -1;
    int64_t enc_durations = -1;
    
    int64_t pros_f0 = -1;
    int64_t pros_energy = -1;
    
    int64_t dec_audio = -1;
    int64_t dec_audio_len = -1;
    
    int64_t win_audio = -1;
    int64_t win_audio_len = -1;
    int64_t src_output = -1;
    
    StreamingConfig stream_cfg;
    bool has_streaming = false;
    
    Impl(const std::string& bundle_dir, int intra_threads)
        : intra(intra_threads > 0 ? intra_threads : default_onnx_threads()) {
        
        bundle = std::make_shared<Bundle>(bundle_dir);
        
        encoder = std::make_unique<SessionWrapper>(bundle->stage(bundle->manifest().encoder).string(), intra);
        if (!encoder->has_input("tokens") || !encoder->has_input("voice") || !encoder->has_input("speed")) {
            throw std::runtime_error("encoder missing required inputs");
        }
        enc_acoustic = encoder->find_output_index("acoustic_tokens");
        enc_prosodic = encoder->find_output_index("prosodic_tokens");
        enc_durations = encoder->find_output_index("durations");
        if (enc_acoustic < 0 || enc_prosodic < 0 || enc_durations < 0) {
            throw std::runtime_error("encoder missing required outputs");
        }
        
        prosody = std::make_unique<SessionWrapper>(bundle->stage(bundle->manifest().prosody).string(), intra);
        if (!prosody->has_input("prosodic_tokens") || !prosody->has_input("frame_to_token") || !prosody->has_input("voice")) {
            throw std::runtime_error("prosody missing required inputs");
        }
        pros_f0 = prosody->find_output_index("f0");
        pros_energy = prosody->find_output_index("energy");
        if (pros_f0 < 0 || pros_energy < 0) {
            throw std::runtime_error("prosody missing required outputs");
        }
        
        decoder_exact = std::make_unique<SessionWrapper>(bundle->stage(bundle->manifest().decoder).string(), intra);
        if (!decoder_exact->has_input("acoustic_tokens") || !decoder_exact->has_input("frame_to_token") ||
            !decoder_exact->has_input("f0") || !decoder_exact->has_input("energy") || !decoder_exact->has_input("voice")) {
            throw std::runtime_error("exact decoder missing required inputs");
        }
        dec_audio = decoder_exact->find_output_index("audio");
        dec_audio_len = decoder_exact->find_output_index("audio_lengths");
        if (dec_audio < 0) throw std::runtime_error("exact decoder missing audio output");
        
        if (bundle->manifest().streaming.has_value()) {
            const auto& s = bundle->manifest().streaming.value();
            stream_cfg.chunk_frames = s.chunk_frames;
            stream_cfg.context_frames = s.context_frames;
            
            try {
                source = std::make_unique<SessionWrapper>(bundle->stage(s.source).string(), intra);
                if (!source->has_input("f0")) throw std::runtime_error("source missing f0 input");
                src_output = source->find_output_index("source");
                if (src_output < 0) throw std::runtime_error("source missing source output");
                
                decoder_window = std::make_unique<SessionWrapper>(bundle->stage(s.decoder).string(), intra);
                if (!decoder_window->has_input("acoustic_tokens") || !decoder_window->has_input("frame_to_token") ||
                    !decoder_window->has_input("f0") || !decoder_window->has_input("energy") ||
                    !decoder_window->has_input("source") || !decoder_window->has_input("voice")) {
                    throw std::runtime_error("window decoder missing required inputs");
                }
                win_audio = decoder_window->find_output_index("audio");
                win_audio_len = decoder_window->find_output_index("audio_lengths");
                if (win_audio < 0) throw std::runtime_error("window decoder missing audio output");
                
                has_streaming = true;
            } catch (const std::exception&) {
                source.reset();
                decoder_window.reset();
                has_streaming = false;
            }
        }
    }
    
    static std::vector<int64_t> build_frame_to_token(const std::vector<int64_t>& durations) {
        size_t total_frames = 0;
        for (int64_t d : durations) {
            if (d < 0) throw std::runtime_error("negative duration");
            total_frames += static_cast<size_t>(d);
        }
        if (total_frames == 0) throw std::runtime_error("zero frames predicted");
        
        std::vector<int64_t> frame_to_token;
        frame_to_token.reserve(total_frames);
        for (size_t i = 0; i < durations.size(); ++i) {
            for (int64_t f = 0; f < durations[i]; ++f) {
                frame_to_token.push_back(static_cast<int64_t>(i));
            }
        }
        return frame_to_token;
    }
    
    Encoded encode(const std::vector<int64_t>& phoneme_ids, const float* voice256, float speed) const {
        if (phoneme_ids.empty()) throw std::runtime_error("empty phoneme_ids");
        
        std::vector<int64_t> tokens;
        tokens.reserve(phoneme_ids.size() + 2);
        tokens.push_back(0);
        tokens.insert(tokens.end(), phoneme_ids.begin(), phoneme_ids.end());
        tokens.push_back(0);
        size_t token_count = tokens.size();
        
        size_t vocab_len = bundle->vocab().size();
        for (int64_t id : phoneme_ids) {
            if (id < 0 || static_cast<size_t>(id) >= vocab_len) {
                throw std::runtime_error("phoneme id out of vocab range");
            }
        }
        
        Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        Ort::AllocatorWithDefaultOptions alloc;
        
        std::vector<Ort::Value> inputs;
        std::vector<const char*> in_names;
        
        std::vector<int64_t> tokens_shape = {1, static_cast<int64_t>(token_count)};
        inputs.emplace_back(Ort::Value::CreateTensor<int64_t>(mem, tokens.data(), tokens.size(), tokens_shape.data(), 2));
        in_names.push_back("tokens");
        
        std::vector<int64_t> voice_shape = {1, kVoiceDim};
        inputs.emplace_back(Ort::Value::CreateTensor<float>(mem, const_cast<float*>(voice256), kVoiceDim, voice_shape.data(), 2));
        in_names.push_back("voice");
        
        float speed_val = speed;
        std::vector<int64_t> speed_shape = {1};
        inputs.emplace_back(Ort::Value::CreateTensor<float>(mem, &speed_val, 1, speed_shape.data(), 1));
        in_names.push_back("speed");
        
        auto outputs = encoder->session->Run(Ort::RunOptions{nullptr}, in_names.data(), inputs.data(), inputs.size(),
                                              to_cstr_vec(encoder->output_names).data(), encoder->output_names.size());
        
        auto acoustic_tensor = outputs[static_cast<size_t>(enc_acoustic)].GetTensorMutableData<float>();
        auto acoustic_info = outputs[static_cast<size_t>(enc_acoustic)].GetTensorTypeAndShapeInfo();
        auto acoustic_shape = acoustic_info.GetShape();
        if (acoustic_shape.size() != 3 || acoustic_shape[0] != 1 || acoustic_shape[2] != static_cast<int64_t>(token_count)) {
            throw std::runtime_error("unexpected acoustic_tokens shape");
        }
        size_t feature_dim = static_cast<size_t>(acoustic_shape[1]);
        size_t acoustic_count = acoustic_info.GetElementCount();
        std::vector<float> acoustic_tokens(acoustic_tensor, acoustic_tensor + acoustic_count);
        
        auto prosodic_tensor = outputs[static_cast<size_t>(enc_prosodic)].GetTensorMutableData<float>();
        auto prosodic_info = outputs[static_cast<size_t>(enc_prosodic)].GetTensorTypeAndShapeInfo();
        auto prosodic_shape = prosodic_info.GetShape();
        if (prosodic_shape.size() != 3 || prosodic_shape[0] != 1 || prosodic_shape[1] != static_cast<int64_t>(token_count) ||
            prosodic_shape[2] != static_cast<int64_t>(feature_dim)) {
            throw std::runtime_error("unexpected prosodic_tokens shape");
        }
        size_t prosodic_count = prosodic_info.GetElementCount();
        std::vector<float> prosodic_tokens(prosodic_tensor, prosodic_tensor + prosodic_count);
        
        auto dur_tensor = outputs[static_cast<size_t>(enc_durations)].GetTensorMutableData<int64_t>();
        auto dur_info = outputs[static_cast<size_t>(enc_durations)].GetTensorTypeAndShapeInfo();
        auto dur_shape = dur_info.GetShape();
        if (dur_shape.size() != 2 || dur_shape[0] != 1 || dur_shape[1] != static_cast<int64_t>(token_count)) {
            throw std::runtime_error("unexpected durations shape");
        }
        std::vector<int64_t> durations(dur_tensor, dur_tensor + token_count);
        
        auto frame_to_token = build_frame_to_token(durations);
        size_t frames = frame_to_token.size();
        
        if (frames > bundle->max_frames()) {
            throw std::runtime_error("predicted frames exceed max_frames");
        }
        
        inputs.clear();
        in_names.clear();
        
        std::vector<int64_t> pros_shape = {1, static_cast<int64_t>(token_count), static_cast<int64_t>(feature_dim)};
        inputs.emplace_back(Ort::Value::CreateTensor<float>(mem, prosodic_tokens.data(), prosodic_tokens.size(), pros_shape.data(), 3));
        in_names.push_back("prosodic_tokens");
        
        std::vector<int64_t> ft_shape = {static_cast<int64_t>(frames)};
        inputs.emplace_back(Ort::Value::CreateTensor<int64_t>(mem, frame_to_token.data(), frame_to_token.size(), ft_shape.data(), 1));
        in_names.push_back("frame_to_token");
        
        std::vector<float> voice_vec(voice256, voice256 + kVoiceDim);
        inputs.emplace_back(Ort::Value::CreateTensor<float>(mem, voice_vec.data(), kVoiceDim, voice_shape.data(), 2));
        in_names.push_back("voice");
        
        outputs = prosody->session->Run(Ort::RunOptions{nullptr}, in_names.data(), inputs.data(), inputs.size(),
                                         to_cstr_vec(prosody->output_names).data(), prosody->output_names.size());
        
        auto f0_tensor = outputs[static_cast<size_t>(pros_f0)].GetTensorMutableData<float>();
        auto f0_info = outputs[static_cast<size_t>(pros_f0)].GetTensorTypeAndShapeInfo();
        auto f0_shape = f0_info.GetShape();
        if (f0_shape.size() != 2 || f0_shape[0] != 1 || f0_shape[1] != static_cast<int64_t>(2 * frames)) {
            throw std::runtime_error("unexpected f0 shape");
        }
        size_t f0_count = f0_info.GetElementCount();
        std::vector<float> f0(f0_tensor, f0_tensor + f0_count);
        
        auto energy_tensor = outputs[static_cast<size_t>(pros_energy)].GetTensorMutableData<float>();
        auto energy_info = outputs[static_cast<size_t>(pros_energy)].GetTensorTypeAndShapeInfo();
        auto energy_shape = energy_info.GetShape();
        if (energy_shape.size() != 2 || energy_shape[0] != 1 || energy_shape[1] != static_cast<int64_t>(2 * frames)) {
            throw std::runtime_error("unexpected energy shape");
        }
        size_t energy_count = energy_info.GetElementCount();
        std::vector<float> energy(energy_tensor, energy_tensor + energy_count);
        
        std::vector<float> voice_copy(voice256, voice256 + kVoiceDim);
        
        return Encoded{
            std::move(voice_copy),
            std::move(acoustic_tokens),
            std::move(frame_to_token),
            std::move(f0),
            std::move(energy),
            std::move(durations),
            feature_dim,
            token_count
        };
    }
    
    std::vector<float> decode_exact(const Encoded& enc) const {
        size_t frames = enc.frame_to_token.size();
        size_t expected_samples = frames * bundle->samples_per_frame();
        
        Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        std::vector<Ort::Value> inputs;
        std::vector<const char*> in_names;
        
        std::vector<int64_t> acoustic_shape = {1, static_cast<int64_t>(enc.feature_dim), static_cast<int64_t>(enc.token_count)};
        inputs.emplace_back(Ort::Value::CreateTensor<float>(mem, const_cast<float*>(enc.acoustic_tokens.data()), enc.acoustic_tokens.size(), acoustic_shape.data(), 3));
        in_names.push_back("acoustic_tokens");
        
        std::vector<int64_t> ft_shape = {static_cast<int64_t>(frames)};
        inputs.emplace_back(Ort::Value::CreateTensor<int64_t>(mem, const_cast<int64_t*>(enc.frame_to_token.data()), enc.frame_to_token.size(), ft_shape.data(), 1));
        in_names.push_back("frame_to_token");
        
        std::vector<int64_t> f0_shape = {1, static_cast<int64_t>(2 * frames)};
        inputs.emplace_back(Ort::Value::CreateTensor<float>(mem, const_cast<float*>(enc.f0.data()), enc.f0.size(), f0_shape.data(), 2));
        in_names.push_back("f0");
        
        inputs.emplace_back(Ort::Value::CreateTensor<float>(mem, const_cast<float*>(enc.energy.data()), enc.energy.size(), f0_shape.data(), 2));
        in_names.push_back("energy");
        
        std::vector<int64_t> voice_shape = {1, kVoiceDim};
        inputs.emplace_back(Ort::Value::CreateTensor<float>(mem, const_cast<float*>(enc.voice.data()), enc.voice.size(), voice_shape.data(), 2));
        in_names.push_back("voice");
        
        auto outputs = decoder_exact->session->Run(Ort::RunOptions{nullptr}, in_names.data(), inputs.data(), inputs.size(),
                                                    to_cstr_vec(decoder_exact->output_names).data(), decoder_exact->output_names.size());
        
        auto audio_tensor = outputs[static_cast<size_t>(dec_audio)].GetTensorMutableData<float>();
        auto audio_info = outputs[static_cast<size_t>(dec_audio)].GetTensorTypeAndShapeInfo();
        size_t audio_count = audio_info.GetElementCount();
        std::vector<float> audio(audio_tensor, audio_tensor + audio_count);
        
        if (dec_audio_len >= 0) {
            auto len_tensor = outputs[static_cast<size_t>(dec_audio_len)].GetTensorMutableData<int64_t>();
            int64_t audio_len = len_tensor[0];
            if (audio_len != static_cast<int64_t>(expected_samples)) {
                throw std::runtime_error("decoder returned unexpected audio length");
            }
        }
        
        if (audio.size() < expected_samples) {
            throw std::runtime_error("decoder audio too short");
        }
        audio.resize(expected_samples);
        
        for (float s : audio) {
            if (!std::isfinite(s)) throw std::runtime_error("decoder produced NaN/Inf");
        }
        
        return audio;
    }
    
    template<typename F>
    bool decode_streaming(const Encoded& enc, const StreamingConfig& cfg, F&& on_chunk) const {
        if (!has_streaming) return false;
        
        size_t frames = enc.frame_to_token.size();
        size_t step = bundle->samples_per_frame();
        size_t max_chunk = stream_cfg.chunk_frames;
        size_t context = stream_cfg.context_frames;
        
        size_t chunk = cfg.chunk_frames ? cfg.chunk_frames : max_chunk;
        if (chunk == 0 || chunk > max_chunk) throw std::runtime_error("invalid chunk_frames");
        
        Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        std::vector<Ort::Value> inputs;
        std::vector<const char*> in_names;
        
        std::vector<int64_t> f0_shape = {1, static_cast<int64_t>(2 * frames)};
        inputs.emplace_back(Ort::Value::CreateTensor<float>(mem, const_cast<float*>(enc.f0.data()), enc.f0.size(), f0_shape.data(), 2));
        in_names.push_back("f0");
        
        auto outputs = source->session->Run(Ort::RunOptions{nullptr}, in_names.data(), inputs.data(), inputs.size(),
                                             to_cstr_vec(source->output_names).data(), source->output_names.size());
        
        auto src_tensor = outputs[static_cast<size_t>(src_output)].GetTensorMutableData<float>();
        auto src_info = outputs[static_cast<size_t>(src_output)].GetTensorTypeAndShapeInfo();
        auto src_shape = src_info.GetShape();
        if (src_shape.size() != 2 || src_shape[0] != 1 || src_shape[1] != static_cast<int64_t>(frames * step)) {
            throw std::runtime_error("source output shape mismatch");
        }
        size_t src_count = src_info.GetElementCount();
        std::vector<float> source_signal(src_tensor, src_tensor + src_count);
        
        size_t skip_samples = stream_silence_skip(enc.durations, step, bundle->sample_rate());
        size_t fade_remaining = (skip_samples > 0) ? kStreamFadeInSamples : 0;
        size_t faded = 0;
        
        size_t start = 0;
        while (start < frames) {
            size_t end = std::min(start + chunk, frames);
            size_t window = chunk + 2 * context;
            size_t window_start = (start >= context) ? start - context : 0;
            if (window_start + window > frames) window_start = frames > window ? frames - window : 0;
            size_t window_end = std::min(window_start + window, frames);
            
            inputs.clear();
            in_names.clear();
            
            std::vector<int64_t> acoustic_shape = {1, static_cast<int64_t>(enc.feature_dim), static_cast<int64_t>(enc.token_count)};
            inputs.emplace_back(Ort::Value::CreateTensor<float>(mem, const_cast<float*>(enc.acoustic_tokens.data()), enc.acoustic_tokens.size(), acoustic_shape.data(), 3));
            in_names.push_back("acoustic_tokens");
            
            std::vector<int64_t> ft_window(enc.frame_to_token.begin() + window_start, enc.frame_to_token.begin() + window_end);
            std::vector<int64_t> ft_shape = {static_cast<int64_t>(ft_window.size())};
            inputs.emplace_back(Ort::Value::CreateTensor<int64_t>(mem, ft_window.data(), ft_window.size(), ft_shape.data(), 1));
            in_names.push_back("frame_to_token");
            
            std::vector<float> f0_window(enc.f0.begin() + 2 * window_start, enc.f0.begin() + 2 * window_end);
            std::vector<int64_t> f0w_shape = {1, static_cast<int64_t>(2 * (window_end - window_start))};
            inputs.emplace_back(Ort::Value::CreateTensor<float>(mem, f0_window.data(), f0_window.size(), f0w_shape.data(), 2));
            in_names.push_back("f0");
            
            std::vector<float> energy_window(enc.energy.begin() + 2 * window_start, enc.energy.begin() + 2 * window_end);
            inputs.emplace_back(Ort::Value::CreateTensor<float>(mem, energy_window.data(), energy_window.size(), f0w_shape.data(), 2));
            in_names.push_back("energy");
            
            std::vector<float> src_window(source_signal.begin() + window_start * step, source_signal.begin() + window_end * step);
            std::vector<int64_t> src_shape = {1, static_cast<int64_t>((window_end - window_start) * step)};
            inputs.emplace_back(Ort::Value::CreateTensor<float>(mem, src_window.data(), src_window.size(), src_shape.data(), 2));
            in_names.push_back("source");
            
            std::vector<int64_t> voice_shape = {1, kVoiceDim};
            inputs.emplace_back(Ort::Value::CreateTensor<float>(mem, const_cast<float*>(enc.voice.data()), enc.voice.size(), voice_shape.data(), 2));
            in_names.push_back("voice");
            
            outputs = decoder_window->session->Run(Ort::RunOptions{nullptr}, in_names.data(), inputs.data(), inputs.size(),
                                                    to_cstr_vec(decoder_window->output_names).data(), decoder_window->output_names.size());
            
            auto audio_tensor = outputs[static_cast<size_t>(win_audio)].GetTensorMutableData<float>();
            auto audio_info = outputs[static_cast<size_t>(win_audio)].GetTensorTypeAndShapeInfo();
            size_t audio_count = audio_info.GetElementCount();
            std::vector<float> audio(audio_tensor, audio_tensor + audio_count);
            
            size_t offset = (start - window_start) * step;
            size_t length = (end - start) * step;
            if (audio.size() < offset + length) {
                throw std::runtime_error("window decoder audio too short");
            }
            
            std::vector<float> chunk_audio(audio.begin() + offset, audio.begin() + offset + length);
            
            if (skip_samples > 0) {
                size_t drop = std::min(skip_samples, chunk_audio.size());
                chunk_audio.erase(chunk_audio.begin(), chunk_audio.begin() + drop);
                skip_samples -= drop;
            }
            
            if (fade_remaining > 0 && !chunk_audio.empty()) {
                size_t count = std::min(fade_remaining, chunk_audio.size());
                for (size_t i = 0; i < count; ++i) {
                    chunk_audio[i] *= static_cast<float>(faded + i + 1) / static_cast<float>(kStreamFadeInSamples);
                }
                faded += count;
                if (faded >= kStreamFadeInSamples) {
                    fade_remaining = 0;
                }
            }
            
            if (!chunk_audio.empty()) {
                if (!on_chunk(chunk_audio)) return false;
            }
            
            start = end;
        }
        return true;
    }
    
    static size_t stream_silence_skip(const std::vector<int64_t>& durations, size_t samples_per_frame, uint32_t sample_rate) {
        if (durations.empty()) return 0;
        int64_t first_dur = durations[0];
        if (first_dur < 0) first_dur = 0;
        size_t silence = static_cast<size_t>(first_dur) * samples_per_frame;
        size_t margin = static_cast<size_t>(std::round(kStreamSilenceMarginSeconds * sample_rate));
        return (silence > margin) ? silence - margin : 0;
    }
};

BundleSynthesizer::BundleSynthesizer(const std::string& bundle_dir, int intra_threads)
    : intra_(intra_threads > 0 ? intra_threads : default_onnx_threads()),
      impl_(std::make_unique<Impl>(bundle_dir, intra_)) {
    bundle_ = impl_->bundle;
    streaming_loaded_ = impl_->has_streaming;
}

BundleSynthesizer::~BundleSynthesizer() = default;

std::vector<float> BundleSynthesizer::synthesize(const std::vector<int64_t>& phoneme_ids,
                                                  const float* voice256, float speed) const {
    auto encoded = impl_->encode(phoneme_ids, voice256, speed);
    return impl_->decode_exact(encoded);
}

bool BundleSynthesizer::synthesize_stream(const std::vector<int64_t>& phoneme_ids,
                                            const float* voice256, float speed,
                                            const StreamingConfig& cfg,
                                            std::function<bool(const std::vector<float>&)> on_chunk) const {
    auto encoded = impl_->encode(phoneme_ids, voice256, speed);
    return impl_->decode_streaming(encoded, cfg, on_chunk);
}


} // namespace holos