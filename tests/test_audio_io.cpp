// WAV decode/resample (audio_io): PCM16/float32, 16/44.1/48 kHz, перший канал,
// ліміти та помилки. Моделі не потрібні.
#include "core/audio_io.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        ++g_failures;
        std::printf("FAIL: %s\n", what.c_str());
    } else {
        std::printf("ok: %s\n", what.c_str());
    }
}

void put_u16(std::string& s, uint16_t v) {
    s.push_back(char(v & 0xFF));
    s.push_back(char((v >> 8) & 0xFF));
}
void put_u32(std::string& s, uint32_t v) {
    for (int i = 0; i < 4; ++i) s.push_back(char((v >> (8 * i)) & 0xFF));
}

// Мінімальний WAV (RIFF): format 1=PCM16, 3=IEEE float32. samples — interleaved.
std::string make_wav(const std::vector<float>& samples, int sr, int channels,
                     int bits, int format) {
    const int bytes_per = bits / 8;
    const uint32_t data_size = uint32_t(samples.size()) * bytes_per;
    std::string w;
    w += "RIFF";
    put_u32(w, 36 + data_size);
    w += "WAVE";
    w += "fmt ";
    put_u32(w, 16);
    put_u16(w, uint16_t(format));
    put_u16(w, uint16_t(channels));
    put_u32(w, uint32_t(sr));
    put_u32(w, uint32_t(sr * channels * bytes_per));
    put_u16(w, uint16_t(channels * bytes_per));
    put_u16(w, uint16_t(bits));
    w += "data";
    put_u32(w, data_size);
    for (float v : samples) {
        if (format == 1) {
            float c = v < -1.f ? -1.f : (v > 1.f ? 1.f : v);
            int16_t q = int16_t(std::lround(c * 32767.0));
            put_u16(w, uint16_t(q));
        } else {
            uint32_t bitsv;
            std::memcpy(&bitsv, &v, 4);
            put_u32(w, bitsv);
        }
    }
    return w;
}

std::vector<float> sine(int n, double freq, int sr, double amp = 0.5) {
    std::vector<float> out(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
        out[size_t(i)] = float(amp * std::sin(2.0 * 3.14159265358979323846 * freq * i / sr));
    return out;
}

double rms_mid(const std::vector<float>& v) {
    if (v.size() < 100) return 0.0;
    const size_t b = v.size() / 10, e = v.size() - v.size() / 10;
    double acc = 0.0;
    for (size_t i = b; i < e; ++i) acc += double(v[i]) * v[i];
    return std::sqrt(acc / double(e - b));
}

void expect_throw(const std::string& wav, const std::string& what,
                  holos::AudioDecodeLimits lim = {}) {
    try {
        holos::decode_wav_24k(wav.data(), wav.size(), lim);
        check(false, what + " (очікувався виняток)");
    } catch (const std::exception&) {
        check(true, what);
    }
}

} // namespace

int main() {
    // 24k mono PCM16, 1 с: довжина та RMS зберігаються.
    {
        auto sig = sine(24000, 440.0, 24000);
        auto wav = make_wav(sig, 24000, 1, 16, 1);
        auto out = holos::decode_wav_24k(wav.data(), wav.size());
        check(std::llabs((long long)out.size() - 24000) <= 2,
              "24k PCM16: довжина " + std::to_string(out.size()));
        const double r = rms_mid(out);
        check(std::fabs(r - 0.35355) < 0.01, "24k PCM16: RMS " + std::to_string(r));
    }
    // 48k -> 24k: довжина вдвічі менша, RMS збережено.
    {
        auto sig = sine(48000, 440.0, 48000);
        auto wav = make_wav(sig, 48000, 1, 16, 1);
        auto out = holos::decode_wav_24k(wav.data(), wav.size());
        check(std::llabs((long long)out.size() - 24000) <= 2,
              "48k->24k: довжина " + std::to_string(out.size()));
        const double r = rms_mid(out);
        check(std::fabs(r - 0.35355) < 0.015, "48k->24k: RMS " + std::to_string(r));
    }
    // 44.1k stereo: перший канал = синус, другий = тиша -> RMS як у моно.
    {
        auto l = sine(44100, 440.0, 44100);
        std::vector<float> inter(size_t(44100) * 2, 0.f);
        for (size_t i = 0; i < l.size(); ++i) inter[i * 2] = l[i];
        auto wav = make_wav(inter, 44100, 2, 16, 1);
        auto out = holos::decode_wav_24k(wav.data(), wav.size());
        check(std::llabs((long long)out.size() - 24000) <= 2,
              "44.1k stereo->24k: довжина " + std::to_string(out.size()));
        const double r = rms_mid(out);
        check(std::fabs(r - 0.35355) < 0.015,
              "44.1k stereo: перший канал, RMS " + std::to_string(r));
    }
    // 16k -> 24k.
    {
        auto sig = sine(16000, 440.0, 16000);
        auto wav = make_wav(sig, 16000, 1, 16, 1);
        auto out = holos::decode_wav_24k(wav.data(), wav.size());
        check(std::llabs((long long)out.size() - 24000) <= 2,
              "16k->24k: довжина " + std::to_string(out.size()));
        const double r = rms_mid(out);
        check(std::fabs(r - 0.35355) < 0.015, "16k->24k: RMS " + std::to_string(r));
    }
    // float32 WAV.
    {
        auto sig = sine(24000, 440.0, 24000, 0.25);
        auto wav = make_wav(sig, 24000, 1, 32, 3);
        auto out = holos::decode_wav_24k(wav.data(), wav.size());
        const double r = rms_mid(out);
        check(std::llabs((long long)out.size() - 24000) <= 2 &&
                  std::fabs(r - 0.17678) < 0.01,
              "float32 24k: довжина/RMS " + std::to_string(r));
    }
    // Ліміт довжини (2 с аудіо, max 1 с) -> виняток.
    {
        auto sig = sine(48000, 440.0, 24000);
        auto wav = make_wav(sig, 24000, 1, 16, 1);
        holos::AudioDecodeLimits lim;
        lim.max_seconds = 1.0;
        expect_throw(wav, "max_seconds=1 для 2 с аудіо кидає", lim);
    }
    // Ліміт розміру.
    {
        auto sig = sine(2400, 440.0, 24000);
        auto wav = make_wav(sig, 24000, 1, 16, 1);
        holos::AudioDecodeLimits lim;
        lim.max_bytes = 1024;
        expect_throw(wav, "max_bytes=1К для більшого WAV кидає", lim);
    }
    // Сміттєві дані / порожні дані.
    {
        std::string garbage("this is not a wav file at all........");
        expect_throw(garbage, "сміттєві дані кидають");
        expect_throw(std::string(), "порожні дані кидають");
    }

    if (g_failures) std::printf("\n%d перевірок провалено\n", g_failures);
    else std::printf("\nусі перевірки audio_io пройдено\n");
    return g_failures ? 1 : 0;
}
