#include "audio.h"

#include <algorithm>
#include <cmath>
#include <fstream>

namespace holos {

namespace {

void put_u32(std::vector<char>& b, uint32_t v) {
    b.push_back(char(v & 0xFF)); b.push_back(char((v >> 8) & 0xFF));
    b.push_back(char((v >> 16) & 0xFF)); b.push_back(char((v >> 24) & 0xFF));
}
void put_u16(std::vector<char>& b, uint16_t v) {
    b.push_back(char(v & 0xFF)); b.push_back(char((v >> 8) & 0xFF));
}
void put_tag(std::vector<char>& b, const char* t) { b.insert(b.end(), t, t + 4); }

// 44-байтний PCM16 mono WAV-заголовок; data_bytes = 0xFFFFFFFF для стрімінгу.
std::vector<char> wav_header(int sample_rate, uint32_t data_bytes, uint32_t riff_size) {
    std::vector<char> h;
    h.reserve(44);
    put_tag(h, "RIFF"); put_u32(h, riff_size); put_tag(h, "WAVE");
    put_tag(h, "fmt "); put_u32(h, 16); put_u16(h, 1); put_u16(h, 1);
    put_u32(h, uint32_t(sample_rate)); put_u32(h, uint32_t(sample_rate) * 2);
    put_u16(h, 2); put_u16(h, 16);
    put_tag(h, "data"); put_u32(h, data_bytes);
    return h;
}

} // namespace

std::vector<int16_t> float_to_pcm16(const std::vector<float>& chunk) {
    float peak = 0.f;
    for (float v : chunk) peak = std::max(peak, std::fabs(v));
    float scale = peak > 0.f ? 1.f / peak : 1.f;
    std::vector<int16_t> out;
    out.reserve(chunk.size());
    for (float v : chunk) {
        float x = v * scale;
        x = std::max(-1.f, std::min(1.f, x));
        out.push_back(static_cast<int16_t>(std::lround(x * 32767.f)));
    }
    return out;
}

std::vector<char> wav_bytes(const std::vector<int16_t>& pcm, int sample_rate) {
    uint32_t data_bytes = uint32_t(pcm.size() * 2);
    auto h = wav_header(sample_rate, data_bytes, 36 + data_bytes);
    h.insert(h.end(), reinterpret_cast<const char*>(pcm.data()),
             reinterpret_cast<const char*>(pcm.data()) + data_bytes);
    return h;
}

std::vector<char> wav_stream_header(int sample_rate) {
    return wav_header(sample_rate, 0xFFFFFFFFu, 0xFFFFFFFFu);
}

void write_wav(const std::string& path, const std::vector<int16_t>& pcm, int sample_rate) {
    auto bytes = wav_bytes(pcm, sample_rate);
    std::ofstream f(path, std::ios::binary);
    f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

} // namespace holos
