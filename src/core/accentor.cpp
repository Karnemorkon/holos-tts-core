#include "accentor.h"

#include <onnxruntime_cxx_api.h>

#include <algorithm>
#include <array>
#include <stdexcept>

namespace holos {

namespace {

constexpr int kMaxLen = 29;
constexpr const char* kAcute = "\u0301"; // U+0301 combining acute

// Словник символів accentor-lite: '<UNK>'=1, "'"=2, а-я/є/і/ї/ґ=3..35; PAD=0.
// Вбудований (той самий, що word2index у Python SequenceTokenizer).
int char_id(uint32_t cp) {
    switch (cp) {
        case '\'': return 2;  // ASCII apostrophe
        case 0x02BC: return 2; // modifier letter apostrophe ʼ
        case 0x2019: return 2; // right single quote ’
        case 0x0430: return 3;  case 0x0431: return 4;  case 0x0432: return 5;
        case 0x0433: return 6;  case 0x0434: return 7;  case 0x0435: return 8;
        case 0x0436: return 9;  case 0x0437: return 10; case 0x0438: return 11;
        case 0x0439: return 12; case 0x043A: return 13; case 0x043B: return 14;
        case 0x043C: return 15; case 0x043D: return 16; case 0x043E: return 17;
        case 0x043F: return 18; case 0x0440: return 19; case 0x0441: return 20;
        case 0x0442: return 21; case 0x0443: return 22; case 0x0444: return 23;
        case 0x0445: return 24; case 0x0446: return 25; case 0x0447: return 26;
        case 0x0448: return 27; case 0x0449: return 28; case 0x044C: return 29;
        case 0x044E: return 30; case 0x044F: return 31; case 0x0454: return 32;
        case 0x0456: return 33; case 0x0457: return 34; case 0x0491: return 35;
        default: return 1; // <UNK>
    }
}

// UTF-8 lowercase для кирилиці/латиниці; решта — без змін (як Python word.lower()).
uint32_t utf8_lower(uint32_t cp) {
    if (cp >= 'A' && cp <= 'Z') return cp + 0x20;
    if (cp >= 0x0410 && cp <= 0x042F) return cp + 0x20; // А-Я -> а-я
    if (cp >= 0x0400 && cp <= 0x040F) return cp + 0x50; // Ѐ-Џ -> ѐ-џ (Ё,Є,І,Ї)
    if (cp == 0x0490) return 0x0491;                     // Ґ -> ґ
    return cp;
}

} // namespace

struct Accentor::Impl {
    Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "accentor"};
    Ort::SessionOptions opts;
    std::unique_ptr<Ort::Session> session;
    Ort::AllocatorWithDefaultOptions alloc;

    Impl(const std::string& path) {
        opts.SetIntraOpNumThreads(1);
        opts.SetInterOpNumThreads(1);
        session = std::make_unique<Ort::Session>(env, path.c_str(), opts);
    }
};

Accentor::Accentor(const std::string& onnx_path) : impl_(new Impl(onnx_path)) {}
Accentor::~Accentor() { delete impl_; }

bool Accentor::loaded() const { return impl_ && impl_->session; }

std::string Accentor::stress(std::string_view word) const {
    if (!impl_ || !impl_->session || word.empty()) return std::string(word);

    // Токенізація: lowercase codepoint -> id; обрізаємо до kMaxLen.
    std::vector<int64_t> ids(kMaxLen, 0);
    int n = 0;
    size_t i = 0;
    while (i < word.size() && n < kMaxLen) {
        unsigned char c = static_cast<unsigned char>(word[i]);
        uint32_t cp; size_t adv;
        if (c < 0x80) { cp = c; adv = 1; }
        else if ((c & 0xE0) == 0xC0) { cp = (c & 0x1F) << 6; adv = 2; }
        else if ((c & 0xF0) == 0xE0) { cp = (c & 0x0F) << 12; adv = 3; }
        else { cp = (c & 0x07) << 18; adv = 4; }
        for (size_t k = 1; k < adv && i + k < word.size(); k++)
            cp |= (static_cast<unsigned char>(word[i + k]) & 0x3F) << (6 * (adv - 1 - k));
        i += adv;
        cp = utf8_lower(cp);
        ids[n++] = char_id(cp);
    }

    try {
        // sequences int64 [1, kMaxLen]
        std::array<int64_t, 2> shape{1, kMaxLen};
        Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        Ort::Value input = Ort::Value::CreateTensor<int64_t>(
            mem, ids.data(), ids.size(), shape.data(), shape.size());
        const char* in_names[] = {"sequences"};
        const char* out_names[] = {"logits"};
        auto outputs = impl_->session->Run(Ort::RunOptions{nullptr}, in_names, &input, 1, out_names, 1);
        const float* logits = outputs[0].GetTensorMutableData<float>();

        // argmax по першому (єдиному) рядку.
        int best = 0;
        float best_v = logits[0];
        for (int j = 1; j < kMaxLen; j++) {
            if (logits[j] > best_v) { best_v = logits[j]; best = j; }
        }

        // Python: word[:index+1] + U+0301 + word[index+1:]; shift=1.
        // Але індекс — це позиція символу у токенізованому (lowercase) слові;
        // вставляємо у ОРИГІНАЛЬНЕ слово (той самий codepoint-індекс).
        // Обрізаємо індекс до реальної довжини.
        int pos = best + 1; // shift=1
        if (pos < 0) pos = 0;
        // Порахувати codepoint-довжину слова.
        int cp_len = 0;
        for (unsigned char ch : word) {
            if ((ch & 0xC0) != 0x80) cp_len++; // non-continuation = new codepoint
        }
        if (pos > cp_len) pos = cp_len; // кліпінг до кінця слова

        // Знайти байт-позицію ПОЧАТКУ codepoint з індексом pos.
        // Проходимо по символах (а не по байтах), інакше U+0301 може
        // вставитись всередину 2-байтового символу і зламати UTF-8.
        size_t byte_pos = 0;
        int cp_idx = 0;
        while (byte_pos < word.size() && cp_idx < pos) {
            unsigned char ch = static_cast<unsigned char>(word[byte_pos]);
            size_t adv = 1;
            if ((ch & 0xE0) == 0xC0) adv = 2;
            else if ((ch & 0xF0) == 0xE0) adv = 3;
            else if ((ch & 0xF8) == 0xF0) adv = 4;
            if (adv > word.size() - byte_pos) adv = 1; // битий UTF-8
            byte_pos += adv;
            cp_idx++;
        }

        std::string out;
        out.reserve(word.size() + 3);
        out.append(word.substr(0, byte_pos));
        out += kAcute;
        out.append(word.substr(byte_pos));
        return out;
    } catch (const std::exception&) {
        return std::string(word);
    }
}

} // namespace holos
