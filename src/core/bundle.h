#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <optional>
#include <filesystem>

namespace holos {

struct StreamingConfig {
    size_t chunk_frames = 40;
    size_t context_frames = 20;
};

// Маніфест бандла holos-onnx (format_version 2)
struct BundleManifest {
    std::string format = "holos-onnx";
    uint32_t format_version = 2;
    std::string encoder;
    std::string prosody;
    std::string decoder;
    std::optional<std::string> voices;
    std::vector<std::string> vocab;
    uint32_t sample_rate = 24000;
    size_t samples_per_frame = 600;
    size_t max_frames = 1000;
    std::string decoder_mode = "exact";
    std::string alignment_format = "gather";
    std::string attention_format;
    std::string projection_format;
    std::string shape_inference_format;
    
    struct Streaming {
        std::string source;
        std::string decoder;
        size_t chunk_frames = 40;
        size_t context_frames = 20;
    };
    std::optional<Streaming> streaming;

    bool validate() const;
    std::vector<std::string> required_files() const;
    std::filesystem::path stage_path(const std::filesystem::path& bundle_dir, const std::string& file) const;
};

// Завантажений бандл
class Bundle {
public:
    explicit Bundle(const std::filesystem::path& dir, size_t expected_voice_dim = 256);
    
    const BundleManifest& manifest() const { return manifest_; }
    const std::map<std::string, std::vector<float>>& voices() const { return voices_; }
    size_t voice_dim() const { return voice_dim_; }
    uint32_t sample_rate() const { return manifest_.sample_rate; }
    size_t samples_per_frame() const { return manifest_.samples_per_frame; }
    size_t max_frames() const { return manifest_.max_frames; }
    const std::vector<std::string>& vocab() const { return manifest_.vocab; }
    std::vector<std::string> voice_names() const;
    const std::vector<float>* voice(const std::string& name) const;
    std::filesystem::path stage(const std::string& file) const { return dir_ / file; }

private:
    std::filesystem::path dir_;
    BundleManifest manifest_;
    std::map<std::string, std::vector<float>> voices_;
    size_t voice_dim_ = 256;
    
    void load_voices(const std::filesystem::path& npz_path, size_t expected_dim);
};

// Читання .npz (zip з names.npy + values.npy)
class NpyArray {
public:
    std::string descr;
    std::vector<size_t> shape;
    std::vector<uint8_t> data;

    static NpyArray parse(const std::vector<uint8_t>& bytes);
    std::vector<std::string> unicode_strings() const;
    std::vector<std::vector<float>> f32_matrix() const;
    size_t element_count() const;
};

} // namespace holos