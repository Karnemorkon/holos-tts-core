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

// WAV (PCM16 mono) у памʼяті — для HTTP-відповідей.
std::vector<char> wav_bytes(const std::vector<int16_t>& pcm, int sample_rate);

// RIFF-заголовок із невідомою довжиною (0xFFFFFFFF) для стрімінгу.
std::vector<char> wav_stream_header(int sample_rate);

} // namespace holos
