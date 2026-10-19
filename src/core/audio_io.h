#pragma once
#include <cstddef>
#include <vector>

namespace holos {

// Декодування WAV (PCM16/24/32 або IEEE float, будь-яка кількість каналів)
// у mono float32 24 kHz: береться перший канал, за потреби ресемплиться.
struct AudioDecodeLimits {
    size_t max_bytes = 16u << 20; // ліміт розміру вхідних даних (16 МБ)
    double max_seconds = 60.0;    // ліміт довжини аудіо
};

// Кидає std::runtime_error із зрозумілим текстом (для HTTP 400).
std::vector<float> decode_wav_24k(const char* data, size_t size,
                                  const AudioDecodeLimits& limits = {});

// Ресемплінг mono float32 зі sr у 24 kHz: Kaiser-віконний sinc (64 тапи,
// fc = min(0.5, 11 kHz/sr)). sr == 24000 -> без змін.
std::vector<float> resample_to_24k(const std::vector<float>& in, int sr);

} // namespace holos
