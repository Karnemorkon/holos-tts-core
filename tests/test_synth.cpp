// M1e/M1f smoke + parity: text->...->audio; або --phonemes <str> --dump-f32 <path>
// (парність phoneme->audio проти Python _generate_part).
#include "../src/core/audio.h"
#include "../src/core/phonemize.h"
#include "../src/core/synth.h"
#include "../src/core/tokenizer.h"
#include "../src/core/voices.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>

static void write_f32(const std::string& path, const std::vector<float>& a) {
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(a.data()), (std::streamsize)(a.size() * 4));
}

int main(int argc, char** argv) {
    // Режим парності: test_synth --phonemes <str> --dump-f32 <path> --vocab V --voices V --model M
    std::string phonemes, phonemes_file, dump_f32, dump_phon, vocab, voices, onnx, trie, text = "Привіт, це тест синтезу мовлення.";
    std::string voice_name = "Speaker_0";
    float speed = 1.0f;
    std::vector<std::string> pos;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return (i + 1 < argc) ? argv[++i] : ""; };
        if (a == "--phonemes") phonemes = next();
        else if (a == "--phonemes-file") phonemes_file = next();
        else if (a == "--dump-f32") dump_f32 = next();
        else if (a == "--dump-phonemes") dump_phon = next();
        else if (a == "--vocab") vocab = next();
        else if (a == "--voices") voices = next();
        else if (a == "--model") onnx = next();
        else if (a == "--trie") trie = next();
        else if (a == "--voice") voice_name = next();
        else if (a == "--speed") speed = std::stof(next());
        else pos.push_back(a);
    }
    if (trie.empty() && vocab.empty() && voices.empty() && onnx.empty() && pos.size() >= 4) {
        // Позиційний стиль: trie vocab voices onnx [text]
        trie = pos[0]; vocab = pos[1]; voices = pos[2]; onnx = pos[3];
        if (pos.size() >= 5) {
            text.clear();
            for (size_t k = 4; k < pos.size(); ++k) { if (k > 4) text += ' '; text += pos[k]; }
        }
    } else if (!pos.empty()) {
        // Прапорці вже задані: усі позиційні аргументи = текст.
        text.clear();
        for (size_t k = 0; k < pos.size(); ++k) { if (k) text += ' '; text += pos[k]; }
    }

    holos::Voices vs = holos::Voices::load(voices);
    holos::CharTokenizer tok = holos::CharTokenizer::from_vocab_json(vocab);
    holos::Synthesizer synth(onnx, 4);
    const auto& voice_arr = vs.get(voice_name);

    std::vector<float> audio;
    if (!phonemes_file.empty()) {
        std::ifstream pf(phonemes_file);
        std::getline(pf, phonemes);
    }
    if (!phonemes.empty()) {
        // Парність: сира фонемна стрічка -> токени -> generate.
        auto chunk = synth.generate(tok(phonemes), voice_arr.data(), speed);
        audio = std::move(chunk);
        std::cerr << "PARITY phonemes[" << phonemes << "] samples=" << audio.size() << "\n";
    } else {
        holos::Phonemizer ph(trie);
        auto parts = ph.phonemize(text);
        if (!dump_phon.empty()) {
            std::ofstream pf(dump_phon);
            for (const auto& p : parts) pf << p << "\n";
        }
        for (const auto& p : parts) {
            auto chunk = synth.generate(tok(p), voice_arr.data(), speed);
            audio.insert(audio.end(), chunk.begin(), chunk.end());
            std::cerr << "  ph[" << p << "] samples=" << chunk.size() << "\n";
        }
    }
    if (audio.empty()) { std::cerr << "no audio\n"; return 1; }
    float peak = 0.f;
    for (float v : audio) peak = std::max(peak, std::fabs(v));
    std::cerr << "TOTAL samples=" << audio.size() << " peak=" << peak
              << " sec=" << (audio.size() / 24000.0) << "\n";
    if (!dump_f32.empty()) { write_f32(dump_f32, audio); std::cerr << "dumped " << dump_f32 << "\n"; }
    auto pcm = holos::float_to_pcm16(audio);
    holos::write_wav("/tmp/m1e_smoke.wav", pcm, holos::kSampleRate);
    return 0;
}
