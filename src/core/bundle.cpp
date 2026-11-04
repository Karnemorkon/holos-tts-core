#include "bundle.h"
#include "cpu.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <stdexcept>
#include <zlib.h>
#include <algorithm>
#include <cstring>

namespace holos {

using json = nlohmann::json;
namespace fs = std::filesystem;

static std::vector<std::string> split_string(const std::string& s, char delim) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (start < s.size()) {
        size_t end = s.find(delim, start);
        if (end == std::string::npos) end = s.size();
        parts.push_back(s.substr(start, end - start));
        start = end + 1;
    }
    return parts;
}

bool BundleManifest::validate() const {
    if (format != "holos-onnx") return false;
    if (format_version != 2) return false;
    if (encoder.empty() || prosody.empty() || decoder.empty()) return false;
    if (vocab.empty()) return false;
    if (sample_rate == 0) return false;
    if (samples_per_frame == 0) return false;
    if (max_frames == 0) return false;
    if (decoder_mode != "exact") return false;
    if (alignment_format != "gather") return false;
    if (streaming.has_value()) {
        const auto& s = streaming.value();
        if (s.chunk_frames == 0) return false;
        if (s.source.empty() || s.decoder.empty()) return false;
    }
    return true;
}

std::vector<std::string> BundleManifest::required_files() const {
    std::vector<std::string> files = {encoder, prosody, decoder};
    if (voices.has_value()) files.push_back(voices.value());
    if (streaming.has_value()) {
        files.push_back(streaming->source);
        files.push_back(streaming->decoder);
    }
    return files;
}

fs::path BundleManifest::stage_path(const fs::path& bundle_dir, const std::string& file) const {
    fs::path p(file);
    if (p.is_absolute()) throw std::runtime_error("bundle file path must be relative: " + file);
    for (const auto& comp : p) {
        if (comp == ".." || comp == ".") {
            throw std::runtime_error("bundle file path must not contain . or ..: " + file);
        }
    }
    return bundle_dir / p;
}

Bundle::Bundle(const fs::path& dir, size_t expected_voice_dim)
    : dir_(fs::absolute(dir)), voice_dim_(expected_voice_dim) {
    
    const auto manifest_path = dir_ / "holos_onnx.json";
    if (!fs::exists(manifest_path)) {
        throw std::runtime_error("bundle manifest not found: " + manifest_path.string());
    }
    
    std::ifstream ifs(manifest_path);
    json j;
    ifs >> j;
    
    manifest_.format = j.value("format", "holos-onnx");
    manifest_.format_version = j.value("format_version", 2);
    manifest_.encoder = j.value("encoder", "");
    manifest_.prosody = j.value("prosody", "");
    manifest_.decoder = j.value("decoder", "");
    if (j.contains("voices")) manifest_.voices = j["voices"].get<std::string>();
    manifest_.vocab = j.value("vocab", std::vector<std::string>{});
    manifest_.sample_rate = j.value("sample_rate", 24000u);
    manifest_.samples_per_frame = j.value("samples_per_frame", 600u);
    manifest_.max_frames = j.value("max_frames", 1000u);
    manifest_.decoder_mode = j.value("decoder_mode", "exact");
    manifest_.alignment_format = j.value("alignment_format", "gather");
    manifest_.attention_format = j.value("attention_format", "");
    manifest_.projection_format = j.value("projection_format", "");
    manifest_.shape_inference_format = j.value("shape_inference_format", "");
    
    if (j.contains("streaming")) {
        const auto& s = j["streaming"];
        BundleManifest::Streaming stream;
        stream.source = s.value("source", "");
        stream.decoder = s.value("decoder", "");
        stream.chunk_frames = s.value("chunk_frames", 40u);
        stream.context_frames = s.value("context_frames", 20u);
        manifest_.streaming = stream;
    }
    
    if (!manifest_.validate()) {
        throw std::runtime_error("invalid bundle manifest");
    }
    
    if (manifest_.voices.has_value()) {
        load_voices(dir_ / manifest_.voices.value(), voice_dim_);
    }
}

std::vector<std::string> Bundle::voice_names() const {
    std::vector<std::string> names;
    names.reserve(voices_.size());
    for (const auto& [name, _] : voices_) names.push_back(name);
    return names;
}

const std::vector<float>* Bundle::voice(const std::string& name) const {
    auto it = voices_.find(name);
    if (it == voices_.end()) return nullptr;
    return &it->second;
}

void Bundle::load_voices(const fs::path& npz_path, size_t expected_dim) {
    std::ifstream file(npz_path, std::ios::binary);
    if (!file) throw std::runtime_error("cannot open voices.npz: " + npz_path.string());
    
    file.seekg(0, std::ios::end);
    size_t file_size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    std::vector<uint8_t> buffer(file_size);
    file.read(reinterpret_cast<char*>(buffer.data()), file_size);
    
    auto find_entry = [&](const std::string& name) -> std::vector<uint8_t> {
        size_t pos = 0;
        while (pos + 30 <= buffer.size()) {
            if (buffer[pos] == 0x50 && buffer[pos+1] == 0x4B && buffer[pos+2] == 0x03 && buffer[pos+3] == 0x04) {
                if (pos + 30 > buffer.size()) break;
                uint16_t name_len = *reinterpret_cast<const uint16_t*>(&buffer[pos + 26]);
                uint16_t extra_len = *reinterpret_cast<const uint16_t*>(&buffer[pos + 28]);
                uint32_t compressed_size = *reinterpret_cast<const uint32_t*>(&buffer[pos + 18]);
                uint32_t uncompressed_size = *reinterpret_cast<const uint32_t*>(&buffer[pos + 22]);
                uint16_t compression = *reinterpret_cast<const uint16_t*>(&buffer[pos + 8]);
                
                // Check for ZIP64 extra field
                uint64_t actual_compressed = compressed_size;
                uint64_t actual_uncompressed = uncompressed_size;
                if (compressed_size == 0xFFFFFFFF || uncompressed_size == 0xFFFFFFFF) {
                    // Parse ZIP64 extra field
                    size_t extra_start = pos + 30 + name_len;
                    size_t extra_end = extra_start + extra_len;
                    if (extra_end <= buffer.size()) {
                        size_t p = extra_start;
                        while (p + 4 <= extra_end) {
                            uint16_t header_id = *reinterpret_cast<const uint16_t*>(&buffer[p]);
                            uint16_t data_size = *reinterpret_cast<const uint16_t*>(&buffer[p + 2]);
                            if (header_id == 0x0001) {  // ZIP64 extended information
                                size_t data_start = p + 4;
                                if (data_start + 8 <= extra_end && uncompressed_size == 0xFFFFFFFF) {
                                    actual_uncompressed = *reinterpret_cast<const uint64_t*>(&buffer[data_start]);
                                    data_start += 8;
                                }
                                if (data_start + 8 <= extra_end && compressed_size == 0xFFFFFFFF) {
                                    actual_compressed = *reinterpret_cast<const uint64_t*>(&buffer[data_start]);
                                }
                            }
                            p += 4 + data_size;
                        }
                    }
                }
                
                std::string entry_name(reinterpret_cast<const char*>(&buffer[pos + 30]), name_len);
                
                size_t data_start = pos + 30 + name_len + extra_len;
                size_t data_end = data_start + actual_compressed;
                
                if (entry_name == name && data_end <= buffer.size()) {
                    std::vector<uint8_t> compressed_data(buffer.begin() + data_start, buffer.begin() + data_end);
                    if (compression == 0) {
                        // Store (no compression)
                        return compressed_data;
                    } else if (compression == 8) {
                        // DEFLATE - use zlib to decompress
                        std::vector<uint8_t> decompressed(actual_uncompressed);
                        z_stream strm = {0};
                        strm.next_in = compressed_data.data();
                        strm.avail_in = static_cast<uInt>(compressed_data.size());
                        strm.next_out = decompressed.data();
                        strm.avail_out = static_cast<uInt>(decompressed.size());
                        
                        int ret = inflateInit2(&strm, -MAX_WBITS);  // Negative wbits for raw DEFLATE
                        if (ret != Z_OK) {
                            throw std::runtime_error("inflateInit2 failed: " + std::to_string(ret));
                        }
                        ret = inflate(&strm, Z_FINISH);
                        if (ret != Z_STREAM_END) {
                            inflateEnd(&strm);
                            throw std::runtime_error("inflate failed: " + std::to_string(ret));
                        }
                        inflateEnd(&strm);
                        decompressed.resize(strm.total_out);
                        return decompressed;
                    }
                    // Other compression methods not supported
                }
                pos = data_end;
            } else {
                pos++;
            }
        }
        throw std::runtime_error("entry not found in npz: " + name);
    };
    
    auto names_data = find_entry("names.npy");
    auto values_data = find_entry("values.npy");
    
    auto names_array = NpyArray::parse(names_data);
    auto values_array = NpyArray::parse(values_data);
    
    auto names = names_array.unicode_strings();
    auto values = values_array.f32_matrix();
    
    if (names.size() != values.size()) {
        throw std::runtime_error("voices names and embeddings length mismatch");
    }
    
    for (size_t i = 0; i < names.size(); ++i) {
        if (values[i].size() != expected_dim) {
            throw std::runtime_error("voice " + names[i] + " has wrong dimension: " + 
                std::to_string(values[i].size()) + " != " + std::to_string(expected_dim));
        }
        for (float v : values[i]) {
            if (!std::isfinite(v)) {
                throw std::runtime_error("voice " + names[i] + " contains NaN or Inf");
            }
        }
        if (voices_.count(names[i])) {
            throw std::runtime_error("duplicate voice name: " + names[i]);
        }
        voices_[names[i]] = std::move(values[i]);
    }
}

NpyArray NpyArray::parse(const std::vector<uint8_t>& bytes) {
    if (bytes.size() < 10 || bytes[0] != 0x93 || bytes[1] != 0x4E || bytes[2] != 0x55 || 
        bytes[3] != 0x4D || bytes[4] != 0x50 || bytes[5] != 0x59) {
        throw std::runtime_error("not a numpy .npy file");
    }
    
    uint8_t major = bytes[6];
    size_t header_start, header_len;
    
    if (major == 1) {
        if (bytes.size() < 10) throw std::runtime_error("truncated numpy header");
        header_start = 10;
        header_len = *reinterpret_cast<const uint16_t*>(&bytes[8]);
    } else if (major == 2 || major == 3) {
        if (bytes.size() < 12) throw std::runtime_error("truncated numpy header");
        header_start = 12;
        header_len = *reinterpret_cast<const uint32_t*>(&bytes[8]);
    } else {
        throw std::runtime_error("unsupported numpy version");
    }
    
    if (header_start + header_len > bytes.size()) {
        throw std::runtime_error("numpy header length out of bounds");
    }
    
    std::string header(reinterpret_cast<const char*>(&bytes[header_start]), header_len);
    
    auto extract = [&](const std::string& key) -> std::string {
        std::string pattern = "'" + key + "'";
        size_t pos = header.find(pattern);
        if (pos == std::string::npos) throw std::runtime_error("numpy header missing " + key);
        pos += pattern.size();
        size_t colon = header.find(':', pos);
        if (colon == std::string::npos) throw std::runtime_error("numpy header malformed " + key);
        std::string rest = header.substr(colon + 1);
        size_t p = rest.find_first_not_of(" \t\n\r");
        if (p != std::string::npos) rest = rest.substr(p);
        
        size_t end;
        if (!rest.empty() && rest[0] == '(') {
            end = rest.find(')');
            if (end == std::string::npos) end = rest.size();
            else end += 1;
        } else {
            end = rest.find(',');
            if (end == std::string::npos) end = rest.size();
        }
        return rest.substr(0, end);
    };
    
    NpyArray arr;
    arr.descr = extract("descr");
    arr.descr.erase(std::remove(arr.descr.begin(), arr.descr.end(), '\''), arr.descr.end());
    arr.descr.erase(std::remove(arr.descr.begin(), arr.descr.end(), '"'), arr.descr.end());
    
    std::string shape_str = extract("shape");
    shape_str = shape_str.substr(1, shape_str.size() - 2);
    auto parts = split_string(shape_str, ',');
    for (const auto& part : parts) {
        std::string trimmed = part;
        trimmed.erase(0, trimmed.find_first_not_of(" \t\n\r"));
        trimmed.erase(trimmed.find_last_not_of(" \t\n\r") + 1);
        if (!trimmed.empty()) {
            arr.shape.push_back(std::stoull(trimmed));
        }
    }
    
    arr.data.assign(bytes.begin() + header_start + header_len, bytes.end());
    return arr;
}

std::vector<std::string> NpyArray::unicode_strings() const {
    if (descr.size() < 2 || (descr[0] != '<' && descr[0] != '=' && descr[0] != '|') || descr[1] != 'U') {
        throw std::runtime_error("expected numpy unicode array, got " + descr);
    }
    
    size_t width = std::stoull(descr.substr(2));
    size_t count = element_count();
    size_t expected = count * width * 4;
    if (data.size() != expected) {
        throw std::runtime_error("numpy unicode array size mismatch");
    }
    
    std::vector<std::string> result;
    result.reserve(count);
    
    for (size_t i = 0; i < count; ++i) {
        std::string text;
        text.reserve(width);
        for (size_t j = 0; j < width; ++j) {
            size_t offset = (i * width + j) * 4;
            uint32_t code = *reinterpret_cast<const uint32_t*>(&data[offset]);
            if (code == 0) continue;
            if (code > 0x10FFFF) throw std::runtime_error("invalid unicode code point");
            if (code <= 0x7F) {
                text.push_back(static_cast<char>(code));
            } else if (code <= 0x7FF) {
                text.push_back(static_cast<char>(0xC0 | (code >> 6)));
                text.push_back(static_cast<char>(0x80 | (code & 0x3F)));
            } else if (code <= 0xFFFF) {
                text.push_back(static_cast<char>(0xE0 | (code >> 12)));
                text.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                text.push_back(static_cast<char>(0x80 | (code & 0x3F)));
            } else {
                text.push_back(static_cast<char>(0xF0 | (code >> 18)));
                text.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
                text.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                text.push_back(static_cast<char>(0x80 | (code & 0x3F)));
            }
        }
        result.push_back(text);
    }
    return result;
}

std::vector<std::vector<float>> NpyArray::f32_matrix() const {
    if (descr != "<f4" && descr != "=f4" && descr != "|f4") {
        throw std::runtime_error("expected float32 numpy array, got " + descr);
    }
    if (shape.size() != 2) {
        throw std::runtime_error("expected 2D float32 array");
    }
    size_t expected = element_count() * 4;
    if (data.size() != expected) {
        throw std::runtime_error("numpy float array size mismatch");
    }
    
    size_t rows = shape[0];
    size_t cols = shape[1];
    std::vector<std::vector<float>> result(rows, std::vector<float>(cols));
    
    for (size_t i = 0; i < rows; ++i) {
        for (size_t j = 0; j < cols; ++j) {
            size_t offset = (i * cols + j) * 4;
            result[i][j] = *reinterpret_cast<const float*>(&data[offset]);
        }
    }
    return result;
}

size_t NpyArray::element_count() const {
    size_t count = 1;
    for (size_t dim : shape) count *= dim;
    return count;
}

} // namespace holos