#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace holos {

constexpr int kSampleRate = 24000;

// float32 [-1,1] -> int16 (пікова нормалізація + clip, як Python synthesize).
std::vector<int16_t> float_to_pcm16(const std::vector<float>& chunk);

// Запис WAV (PCM16 mono) у файл.
void write_wav(const std::string& path, const std::vector<int16_t>& pcm, int sample_rate);

} // namespace holos
