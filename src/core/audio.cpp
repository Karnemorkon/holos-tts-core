#include "audio.h"

#include <algorithm>
#include <cmath>
#include <fstream>

namespace holos {

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

void write_wav(const std::string& path, const std::vector<int16_t>& pcm, int sample_rate) {
    std::ofstream f(path, std::ios::binary);
    uint32_t data_bytes = static_cast<uint32_t>(pcm.size() * 2);
    uint32_t byte_rate = static_cast<uint32_t>(sample_rate) * 2;
    uint32_t riff_size = 36 + data_bytes;
    auto w32 = [&](uint32_t v) {
        char b[4] = {char(v & 0xFF), char((v >> 8) & 0xFF), char((v >> 16) & 0xFF),
                     char((v >> 24) & 0xFF)};
        f.write(b, 4);
    };
    auto w16 = [&](uint16_t v) {
        char b[2] = {char(v & 0xFF), char((v >> 8) & 0xFF)};
        f.write(b, 2);
    };
    f.write("RIFF", 4); w32(riff_size); f.write("WAVE", 4);
    f.write("fmt ", 4); w32(16); w16(1); w16(1);
    w32(static_cast<uint32_t>(sample_rate)); w32(byte_rate);
    w16(2); w16(16);
    f.write("data", 4); w32(data_bytes);
    f.write(reinterpret_cast<const char*>(pcm.data()), static_cast<std::streamsize>(data_bytes));
}

} // namespace holos
