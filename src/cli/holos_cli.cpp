#include <iostream>
#include <string>
#include <vector>

#include "core/audio.h"
#include "core/phonemize.h"
#include "core/stress_dictionary.h"
#include "core/synth.h"
#include "core/tokenizer.h"
#include "core/voices.h"

#ifndef HOLOS_VERSION
#define HOLOS_VERSION "0.6.0"
#endif

static void usage() {
    std::cerr
        << "holos-cli " << HOLOS_VERSION << " — C++ native core for HolosTTS (Ukrainian)\n"
        << "Usage:\n"
        << "  holos-cli stress <stress.trie> <word> [word...]\n"
        << "  holos-cli synth --trie <stress.trie> --vocab <vocab.json> --voices <voices.json>\n"
        << "                  --model <holos.onnx> [--voice Speaker_0] [--speed 1.0]\n"
        << "                  [--threads N] [--raw-stress] [--out out.wav] <text>\n"
        << "  holos-cli version\n";
}

static std::string synth_cmd(int argc, char** argv) {
    std::string trie, vocab, voices, model, voice = "Speaker_0", out;
    float speed = 1.0f;
    int threads = 0;
    bool raw_stress = false;
    std::vector<std::string> text_parts;

    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) throw std::runtime_error("missing value for " + a);
            return argv[++i];
        };
        if (a == "--trie") trie = next();
        else if (a == "--vocab") vocab = next();
        else if (a == "--voices") voices = next();
        else if (a == "--model") model = next();
        else if (a == "--voice") voice = next();
        else if (a == "--out") out = next();
        else if (a == "--speed") speed = std::stof(next());
        else if (a == "--threads") threads = std::stoi(next());
        else if (a == "--raw-stress") raw_stress = true;
        else text_parts.push_back(a);
    }
    if (trie.empty() || vocab.empty() || voices.empty() || model.empty() || text_parts.empty())
        throw std::runtime_error("synth: --trie/--vocab/--voices/--model and <text> are required");

    std::string text;
    for (size_t i = 0; i < text_parts.size(); ++i) {
        if (i) text += ' ';
        text += text_parts[i];
    }

    holos::Phonemizer ph(trie);
    holos::Voices vs = holos::Voices::load(voices);
    holos::CharTokenizer tok = holos::CharTokenizer::from_vocab_json(vocab);
    holos::Synthesizer synth(model, threads);
    const auto& voice_arr = vs.get(voice);

    std::vector<float> audio;
    for (const auto& p : ph.phonemize(text, raw_stress)) {
        auto chunk = synth.generate(tok(p), voice_arr.data(), speed);
        audio.insert(audio.end(), chunk.begin(), chunk.end());
    }
    if (audio.empty()) throw std::runtime_error("no phonemes generated");

    auto pcm = holos::float_to_pcm16(audio);
    if (out.empty()) out = "holos.wav";
    holos::write_wav(out, pcm, holos::kSampleRate);
    std::cerr << "[holos] " << pcm.size() << " samples (" << (pcm.size() / double(holos::kSampleRate))
              << "s) @ " << holos::kSampleRate << "Hz -> " << out << "\n";
    return out;
}

int main(int argc, char** argv) {
    if (argc < 2) { usage(); return 2; }
    std::string cmd = argv[1];

    if (cmd == "version") {
        std::cout << HOLOS_VERSION << "\n";
        return 0;
    }

    if (cmd == "stress") {
        if (argc < 4) { usage(); return 2; }
        std::string trie_path = argv[2];
        try {
            holos::StressDictionary dict(trie_path);
            std::cerr << "[holos] stress.trie keys=" << dict.size() << "\n";
            for (int i = 3; i < argc; i++) {
                std::string w = argv[i];
                auto pos = dict.accent_positions(w);
                std::string s = dict.stress(w);
                std::cout << w << " -> " << s << "   [pos:";
                for (int p : pos) std::cout << " " << p;
                std::cout << "]\n";
            }
        } catch (const std::exception& e) {
            std::cerr << "error: " << e.what() << "\n";
            return 1;
        }
        return 0;
    }

    if (cmd == "synth") {
        try {
            synth_cmd(argc, argv);
            return 0;
        } catch (const std::exception& e) {
            std::cerr << "error: " << e.what() << "\n";
            return 1;
        }
    }

    usage();
    return 2;
}
