// Golden-тест voice encoder: C++ ORT vs Python ORT на однаковому int8 .onnx.
// Сигнал детермінований (та сама формула у tools/gen_voice_encoder_golden.py).
// Без моделі -> skip (return 77, як test_verbalizer_gemma).
#include "core/voice_encoder.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace {

constexpr int kSampleRate = 24000;
constexpr double kPi = 3.14159265358979323846;

std::vector<float> make_signal(int n) {
    std::vector<float> x(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        const double t = double(i);
        const double s = 0.35 * std::sin(2.0 * kPi * 173.0 * t / kSampleRate) +
                         0.2 * std::sin(2.0 * kPi * 431.0 * t / kSampleRate) *
                             (0.5 + 0.5 * std::sin(2.0 * kPi * 2.7 * t / kSampleRate));
        x[size_t(i)] = float(s);
    }
    return x;
}

double cosine(const std::vector<float>& a, const std::vector<float>& b) {
    double dot = 0.0, na = 0.0, nb = 0.0;
    for (size_t i = 0; i < a.size(); ++i) {
        dot += double(a[i]) * double(b[i]);
        na += double(a[i]) * double(a[i]);
        nb += double(b[i]) * double(b[i]);
    }
    return dot / (std::sqrt(na) * std::sqrt(nb) + 1e-12);
}

} // namespace

int main(int argc, char** argv) {
    const std::string golden_path = argc > 1 ? argv[1] : "tests/voice_encoder_golden.txt";
    const std::string onnx_path = argc > 2 ? argv[2] : "";

    if (onnx_path.empty()) {
        std::printf("SKIP: HOLOS_TEST_VOICE_ENCODER не задано (модель відсутня)\n");
        return 77;
    }
    {
        std::ifstream probe(onnx_path, std::ios::binary);
        if (!probe) {
            std::printf("SKIP: немає файлу %s\n", onnx_path.c_str());
            return 77;
        }
    }
    std::vector<float> golden;
    {
        std::ifstream f(golden_path);
        if (!f) {
            std::printf("FAIL: немає golden-файлу %s\n", golden_path.c_str());
            return 1;
        }
        double v;
        while (f >> v) golden.push_back(float(v));
    }
    if (golden.size() != 256) {
        std::printf("FAIL: golden має %zu значень (очікується 256)\n", golden.size());
        return 1;
    }

    try {
        holos::VoiceEncoder enc(onnx_path);
        if (!enc.ready()) {
            std::printf("FAIL: енкодер не завантажився: %s\n", onnx_path.c_str());
            return 1;
        }
        const auto wave = make_signal(kSampleRate * 3); // 3 с
        const auto a = enc.encode(wave);
        if (a.size() != 256) {
            std::printf("FAIL: вихід %zu (очікується 256)\n", a.size());
            return 1;
        }
        const double cos = cosine(a, golden);
        std::printf("voice encoder golden cos = %.9f (поріг 0.995)\n", cos);
        if (cos < 0.995) {
            std::printf("FAIL: cos нижче порогу\n");
            return 1;
        }
        const auto b = enc.encode(wave);
        const double det = cosine(a, b);
        std::printf("детермінізм (два прогони) cos = %.9f\n", det);
        if (det < 0.9999) {
            std::printf("FAIL: нестабільний вихід\n");
            return 1;
        }
    } catch (const std::exception& e) {
        std::printf("FAIL: %s\n", e.what());
        return 1;
    }
    std::printf("voice encoder golden тест пройдено\n");
    return 0;
}
