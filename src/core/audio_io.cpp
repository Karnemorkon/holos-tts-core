#include "audio_io.h"

#include "audio.h" // kSampleRate

#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace holos {

namespace {

constexpr double kPi = 3.14159265358979323846;

// Таблиця Kaiser-віконного sinc-ядра для інтерполяції (x у вхідних семплах,
// |x| < half; fc — нормована частота зрізу в одиницях вхідної частоти).
std::vector<float> kaiser_sinc_table(int half, double fc, double beta, int sub) {
    const int n = 2 * half * sub + 1;
    std::vector<float> t(static_cast<size_t>(n));
    const double i0b = std::cyl_bessel_i(0.0, beta);
    for (int i = 0; i < n; ++i) {
        const double x = double(i - half * sub) / double(sub);
        double w = 0.0;
        if (std::fabs(x) < double(half)) {
            const double r = x / double(half);
            w = std::cyl_bessel_i(0.0, beta * std::sqrt(std::max(0.0, 1.0 - r * r))) / i0b;
        }
        const double s = (std::fabs(x) < 1e-12)
                             ? 2.0 * fc
                             : std::sin(2.0 * kPi * fc * x) / (kPi * x);
        t[size_t(i)] = float(s * w);
    }
    return t;
}

float kernel_lookup(const std::vector<float>& table, int half, int sub, double x) {
    const double p = (x + double(half)) * double(sub);
    if (p < 0.0 || p > double(2 * half * sub)) return 0.f;
    const size_t i = size_t(p);
    const float a = table[i];
    const float b = (i + 1 < table.size()) ? table[i + 1] : 0.f;
    return float(double(a) + (double(b) - double(a)) * (p - double(i)));
}

} // namespace

std::vector<float> resample_to_24k(const std::vector<float>& in, int sr) {
    if (sr == kSampleRate) return in;
    if (sr <= 0) throw std::runtime_error("некоректна частота дискретизації");
    if (in.empty()) return {};

    const double ratio = double(sr) / double(kSampleRate);
    const double fc = std::min(0.5, 11000.0 / double(sr));
    constexpr int half = 32;
    constexpr int sub = 512;
    const auto table = kaiser_sinc_table(half, fc, 8.6, sub);

    const size_t n_out = size_t(std::floor((double(in.size()) - 1.0) / ratio)) + 1;
    std::vector<float> out(n_out);
    const long n_in = long(in.size());
    for (size_t n = 0; n < n_out; ++n) {
        const double c = double(n) * ratio;
        const long i0 = long(std::floor(c));
        double acc = 0.0;
        for (long k = -half + 1; k <= half; ++k) {
            const long idx = i0 + k;
            if (idx < 0 || idx >= n_in) continue;
            acc += double(in[size_t(idx)]) * kernel_lookup(table, half, sub, c - double(idx));
        }
        out[n] = float(acc);
    }
    return out;
}

std::vector<float> decode_wav_24k(const char* data, size_t size,
                                  const AudioDecodeLimits& limits) {
    if (!data || size == 0) throw std::runtime_error("порожнє аудіо");
    if (size > limits.max_bytes)
        throw std::runtime_error("аудіо завелике (максимум " +
                                 std::to_string(limits.max_bytes >> 20) + " МБ)");

    drwav wav;
    if (!drwav_init_memory(&wav, data, size, nullptr))
        throw std::runtime_error(
            "не вдалося розібрати WAV (підтримуються PCM16/24/32 та float32)");
    struct Guard {
        drwav* w;
        ~Guard() { drwav_uninit(w); }
    } guard{&wav};

    const int channels = int(wav.channels);
    const int sr = int(wav.sampleRate);
    if (channels <= 0 || sr <= 0) throw std::runtime_error("некоректний WAV");
    const uint64_t frames = wav.totalPCMFrameCount;
    if (frames == 0) throw std::runtime_error("WAV без аудіо-фреймів");
    if (double(frames) / double(sr) > limits.max_seconds)
        throw std::runtime_error("аудіо довше " +
                                 std::to_string(int(limits.max_seconds)) + " с");

    std::vector<float> inter(size_t(frames) * size_t(channels));
    const uint64_t got = drwav_read_pcm_frames_f32(&wav, frames, inter.data());
    if (got == 0) throw std::runtime_error("не вдалося прочитати семпли WAV");
    inter.resize(size_t(got) * size_t(channels));

    std::vector<float> mono(static_cast<size_t>(got));
    for (uint64_t i = 0; i < got; ++i)
        mono[size_t(i)] = inter[size_t(i) * size_t(channels)]; // перший канал
    return resample_to_24k(mono, sr);
}

} // namespace holos
