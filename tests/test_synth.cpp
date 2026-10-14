// M1e smoke: повний ланцюжок text -> stress -> ipa -> tokens -> ORT -> PCM/WAV.
#include "../src/core/audio.h"
#include "../src/core/phonemize.h"
#include "../src/core/synth.h"
#include "../src/core/tokenizer.h"
#include "../src/core/voices.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    if (argc < 5) {
        std::cerr << "usage: test_synth <stress.trie> <vocab.json> <voices.json> <model.onnx> [text]\n";
        return 2;
    }
    std::string trie = argv[1], vocab = argv[2], voices = argv[3], onnx = argv[4];
    std::string text = argc > 5 ? argv[5] : "Привіт, це тест синтезу мовлення.";

    holos::Phonemizer ph(trie);
    holos::Voices vs = holos::Voices::load(voices);
    holos::CharTokenizer tok = holos::CharTokenizer::from_vocab_json(vocab);
    holos::Synthesizer synth(onnx, 4);

    auto parts = ph.phonemize(text);
    std::cerr << "parts=" << parts.size() << " intra=" << synth.intra_threads() << "\n";
    if (parts.empty()) { std::cerr << "no phonemes\n"; return 1; }

    const auto& voice = vs.get("Speaker_0");
    std::vector<float> audio;
    for (const auto& p : parts) {
        auto ids = tok(p);
        auto chunk = synth.generate(ids, voice.data(), 1.0f);
        audio.insert(audio.end(), chunk.begin(), chunk.end());
        std::cerr << "  ph[" << p << "] ids=" << ids.size() << " samples=" << chunk.size() << "\n";
    }
    auto pcm = holos::float_to_pcm16(audio);
    float peak = 0.f;
    for (float v : audio) peak = std::max(peak, std::fabs(v));
    std::cerr << "TOTAL samples=" << audio.size() << " pcm=" << pcm.size()
              << " peak=" << peak << " sec=" << (audio.size() / 24000.0) << "\n";
    holos::write_wav("/tmp/m1e_smoke.wav", pcm, holos::kSampleRate);
    std::cerr << "wrote /tmp/m1e_smoke.wav\n";
    return audio.empty() ? 1 : 0;
}
