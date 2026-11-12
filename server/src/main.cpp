// M2: Drogon HTTP/WS server (:8004) — API-паритет HolosTTS-hybrid.
#include <drogon/drogon.h>
#include <drogon/WebSocketController.h>

#include <filesystem>

#include "core/audio.h"
#include "core/audio_io.h"
#include "core/custom_voices.h"
#include "core/phonemize.h"
#include "core/synth.h"
#include "core/tokenizer.h"
#include "core/verbalizer.h"
#ifdef HOLOS_WITH_VERBALIZER
#include "core/verbalizer_neural.h"
#endif
#include "core/voice_encoder.h"
#include "core/voices.h"

#include <json/json.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cctype>
#include <functional>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#ifndef HOLOS_VERSION
#define HOLOS_VERSION "0.7.1"
#endif

using namespace drogon;

namespace {

std::string env(const char* k, const std::string& def) {
    const char* v = std::getenv(k);
    return v && *v ? std::string(v) : def;
}
int env_int(const char* k, int def) {
    const char* v = std::getenv(k);
    return v && *v ? std::atoi(v) : def;
}

// HOLOS_MODEL_FORMAT: auto (дефолт) | legacy | bundle.
holos::SynthesizerMode parse_model_format(const std::string& fmt) {
    if (fmt == "legacy") return holos::SynthesizerMode::Legacy;
    if (fmt == "bundle") return holos::SynthesizerMode::Bundle;
    return holos::SynthesizerMode::Auto;
}

// HOLOS_ONNX: явний шлях має приоритет; інакше auto — бандл holos-onnx
// (якщо є маніфест), інакше legacy-граф у каталозі моделей.
std::string resolve_onnx(const std::string& models, const std::string& fmt) {
    const std::string legacy = models + "/holos_cpu_int8.onnx";
    const std::string bundle = models + "/holos-onnx";
    if (const char* v = std::getenv("HOLOS_ONNX"); v && *v) return v;
    if (fmt == "legacy") return legacy;
    if (fmt == "bundle") return bundle;
    std::error_code ec;
    return std::filesystem::is_regular_file(bundle + "/holos_onnx.json", ec) ? bundle : legacy;
}

// Інструментування латентності (TTFB/RTF). Типово ВИМКНЕНО (прод-режим):
// HOLOS_METRICS=1 -> лог [metrics] на запит + timings у відповідях/заголовках.
bool g_metrics = env_int("HOLOS_METRICS", 0) != 0;

using Clock = std::chrono::steady_clock;
const auto g_start = Clock::now();

double ms_since(Clock::time_point t) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t).count();
}

struct Timings {
    double vb_ms = 0, phon_ms = 0, ort_ms = 0, encode_ms = 0;
    double ttfb_ms = -1, total_ms = 0;
    int parts = 0;
};

// OpenAI-імена голосів -> мапінг на пресети (як _pick_voice у Python).
const char* kOpenAIVoices[] = {"alloy", "echo", "fable", "onyx", "nova", "shimmer"};

// Neural verbalizer (M2M100 ONNX int8); nullptr якщо недоступний.
#ifdef HOLOS_WITH_VERBALIZER
std::unique_ptr<holos::NeuralVerbalizerBase> g_neural;
#endif

// Режим автовербалізації: "neural" (типово, якість) | "rule" (швидкість) | false/Off.
enum class VbMode { Off, Rule, Neural };

VbMode parse_vb_mode(const Json::Value& j) {
    if (!j.isMember("auto_verbalize")) return VbMode::Neural;
    const Json::Value& v = j["auto_verbalize"];
    if (v.isBool()) return v.asBool() ? VbMode::Neural : VbMode::Off;
    if (v.isString()) {
        const auto s = v.asString();
        if (s == "neural" || s == "true" || s == "1") return VbMode::Neural;
        if (s == "rule") return VbMode::Rule;
        if (s == "off" || s == "false" || s == "0" || s == "none") return VbMode::Off;
    }
    return VbMode::Neural;
}

const char* vb_mode_name(VbMode m) {
    switch (m) {
        case VbMode::Off: return "off";
        case VbMode::Rule: return "rule";
        default: return "neural";
    }
}

void log_metrics(const char* api, VbMode mode, const Timings& t, double audio_sec) {
    if (!g_metrics) return;
    const double rtf = (t.total_ms > 0 && audio_sec > 0)
        ? (t.total_ms / 1000.0) / audio_sec : 0.0;
    LOG_INFO << "[metrics] api=" << api << " mode=" << vb_mode_name(mode)
             << " parts=" << t.parts
             << " vb_ms=" << (long long)std::llround(t.vb_ms)
             << " phon_ms=" << (long long)std::llround(t.phon_ms)
             << " ort_ms=" << (long long)std::llround(t.ort_ms)
             << " encode_ms=" << (long long)std::llround(t.encode_ms)
             << " ttfb_ms=" << (long long)std::llround(t.ttfb_ms)
             << " total_ms=" << (long long)std::llround(t.total_ms)
             << " audio_s=" << audio_sec
             << " rtf=" << rtf;
}

Json::Value timings_json(const Timings& t) {
    Json::Value j;
    j["parts"] = t.parts;
    j["vb_ms"] = t.vb_ms;
    j["phon_ms"] = t.phon_ms;
    j["ort_ms"] = t.ort_ms;
    j["encode_ms"] = t.encode_ms;
    j["ttfb_ms"] = t.ttfb_ms;
    j["total_ms"] = t.total_ms;
    return j;
}

void add_timing_headers(const HttpResponsePtr& resp, const Timings& t, double audio_sec) {
    if (!g_metrics) return;
    auto add = [&resp](const char* name, double v) {
        resp->addHeader(name, std::to_string((long long)std::llround(v)));
    };
    add("X-Holos-Timing-Vb-Ms", t.vb_ms);
    add("X-Holos-Timing-Phon-Ms", t.phon_ms);
    add("X-Holos-Timing-Ort-Ms", t.ort_ms);
    add("X-Holos-Timing-Total-Ms", t.total_ms);
    if (audio_sec > 0)
        resp->addHeader("X-Holos-Rtf", std::to_string(t.total_ms / 1000.0 / audio_sec));
}

std::string apply_verbalize(VbMode m, const std::string& text) {
    switch (m) {
        case VbMode::Off:
            return text;
        case VbMode::Neural:
#ifdef HOLOS_WITH_VERBALIZER
            if (g_neural) return g_neural->verbalize(text);
#endif
            return holos::verbalize(text); // fallback: rule
        case VbMode::Rule:
        default:
            return holos::verbalize(text);
    }
}

// Тиша між частинами синтезу (сек): гарантована пауза між реченнями навіть
// там, де модель сама не поставила паузу (Issue #8: «злиття» речень).
constexpr double kGapSec = 0.3;

// Точка рекурсивного розриву частини, що перевищує max_frames (Issue #8).
// Спочатку — межа речення (".!? " перед/після середини), щоб не ламати
// пунктуацію; інакше — найближчий пробіл; інакше — середина рядка.
// Повертає індекс пробілу для розриву; at_sentence — чи розрив на межі речення.
size_t find_long_part_split(const std::string& s, bool* at_sentence = nullptr) {
    auto set_flag = [&](bool v) { if (at_sentence) *at_sentence = v; };
    auto sentence_space = [&](size_t i) {
        return i > 0 && s[i] == ' ' &&
               (s[i - 1] == '.' || s[i - 1] == '!' || s[i - 1] == '?');
    };
    const size_t mid = s.size() / 2;
    for (size_t i = mid; i > 0; --i)
        if (sentence_space(i)) { set_flag(true); return i; }
    for (size_t i = mid + 1; i + 1 < s.size(); ++i)
        if (sentence_space(i)) { set_flag(true); return i; }
    set_flag(false);
    size_t split = s.find_last_of(' ', mid);
    if (split == std::string::npos || split == 0)
        split = s.find_first_of(' ', mid);
    if (split == std::string::npos || split == 0 || split + 1 >= s.size())
        split = mid;
    return split;
}

// Шлях до словника користувача (stress_user.txt) — заповнюється в Engine::load
// і використовується API /api/stress-user. g_user_file_mtx серіалізує
// читання/запис файлу між запитами (reload у StressDictionary має власний лок).
std::string g_user_stress_path;
std::mutex g_user_file_mtx;

// Глобальний движок (завантажується один раз при старті).
struct Engine {
    std::unique_ptr<holos::Phonemizer> phonemizer;
    holos::Voices voices;
    holos::CharTokenizer tokenizer;
    std::unique_ptr<holos::Synthesizer> synth;
    std::unique_ptr<holos::VoiceEncoder> encoder; // клонування (може бути вимкнено)
    std::unique_ptr<holos::CustomVoices> custom;  // speakers/*.npy (як hybrid)
    std::mutex infer_mu; // серіалізуємо фонемізацію+синтез (як _INFER_LOCK)
    std::mutex clone_mu; // серіалізуємо енролмент (пікові буфери енкодера)
    std::vector<std::string> voice_names;

    void load(const std::string& trie, const std::string& vocab,
              const std::string& voices_json, const std::string& onnx,
              const std::string& accentor, const std::string& voice_encoder,
              const std::string& speakers_dir, int threads,
              holos::SynthesizerMode mode = holos::SynthesizerMode::Auto) {
        phonemizer = std::make_unique<holos::Phonemizer>(trie, accentor);
        g_user_stress_path = phonemizer->user_stress_path();
        if (size_t n = phonemizer->user_stress_size(); n > 0)
            LOG_INFO << "словник користувача: " << n << " слів (stress_user.txt)";
        synth = std::make_unique<holos::Synthesizer>(onnx, threads, mode);
        // Бандл holos-onnx несе власні голоси (voices.npz) і фонемний словник
        // (vocab у маніфесті). Legacy voices.json/vocab.json з ним несумісні:
        // ембеддінги з іншого voice-енкодера дають іншу вимову й темп.
        if (synth->mode() == holos::SynthesizerMode::Bundle) {
            std::map<std::string, std::vector<float>> presets;
            if (auto names = synth->voice_names()) {
                for (const auto& n : *names) {
                    if (auto v = synth->voice(n)) presets[n] = **v;
                }
            }
            if (!presets.empty()) {
                voices = holos::Voices::from_map(std::move(presets));
            } else {
                voices = holos::Voices::load(voices_json);
            }
            if (auto v = synth->vocab(); v && !v->empty()) {
                tokenizer = holos::CharTokenizer(*v);
            } else {
                tokenizer = holos::CharTokenizer::from_vocab_json(vocab);
            }
            LOG_INFO << "bundle voices: " << voices.size()
                     << ", vocab size: " << tokenizer.vocab_size();
        } else {
            voices = holos::Voices::load(voices_json);
            tokenizer = holos::CharTokenizer::from_vocab_json(vocab);
        }
        encoder = std::make_unique<holos::VoiceEncoder>(voice_encoder, threads);
        custom = std::make_unique<holos::CustomVoices>(speakers_dir);
        voice_names = voices.names();
    }

    bool cloning_ready() const {
        return encoder && encoder->ready() && custom && custom->enabled();
    }

    // Мапінг voice-імені (пресет, кастомний голос або OpenAI-імʼя) -> дійсне імʼя.
    // Порожній = невідомий.
    std::string pick_voice(const std::string& name) const {
        if (name.empty()) return voice_names.empty() ? "" : voice_names.front();
        if (voices.has(name)) return name;
        if (custom && custom->has(name)) return name;
        for (int i = 0; i < 6; ++i) {
            if (name == kOpenAIVoices[i])
                return voice_names[size_t(i) % voice_names.size()];
        }
        return ""; // невідомий
    }

    // 256-float ембеддінг голосу (копія): пресет або кастомний.
    std::vector<float> voice_embedding(const std::string& name) const {
        if (voices.has(name)) return voices.get(name);
        if (custom && custom->has(name)) return custom->get(name);
        throw std::out_of_range("невідомий голос: " + name);
    }

    // Список: пресети (у порядку voices.json), потім кастомні (сортовані).
    std::vector<std::string> all_voice_names() const {
        std::vector<std::string> all = voice_names;
        if (custom) {
            for (auto& n : custom->names())
                if (std::find(all.begin(), all.end(), n) == all.end())
                    all.push_back(n);
        }
        return all;
    }

    // Внутрішній синтез однієї фонемної частини (без infer_mu, з рекурсивним розбиттям при перевищенні max_frames).
    // Для HTTP-шляху використовуємо не-стрімінговий generate (простіше, надійніше на великих текстах).
    // Стрімінг (generate_stream) залишаємо для WebSocket-шляху.
    std::vector<float> synth_part_pcm_impl(const std::string& phonemes, const std::string& voice,
                                           float speed) {
        const auto v = voice_embedding(voice);
        std::vector<float> audio;
        try {
            auto ids = tokenizer(phonemes);
            audio = synth->generate(ids, v.data(), speed);
        } catch (const std::runtime_error& e) {
            std::string msg = e.what();
            if (msg.find("predicted frames exceed max_frames") != std::string::npos
                && phonemes.size() > 4) {
                // Розриваємо на межі речення (щоб не ламати пунктуацію) і
                // рекурсивно синтезуємо кожну частину; між реченнями — тиша.
                bool at_sentence = false;
                size_t split = find_long_part_split(phonemes, &at_sentence);
                auto left = synth_part_pcm_impl(phonemes.substr(0, split), voice, speed);
                auto right = synth_part_pcm_impl(phonemes.substr(split + 1), voice, speed);
                audio.insert(audio.end(), left.begin(), left.end());
                if (at_sentence)
                    audio.insert(audio.end(),
                                 static_cast<size_t>(kGapSec * holos::kSampleRate), 0.f);
                audio.insert(audio.end(), right.begin(), right.end());
            } else {
                throw;
            }
        }
        return audio;
    }

    std::vector<std::string> phonemize(const std::string& text, bool raw_stress) {
        std::lock_guard<std::mutex> lk(infer_mu);
        return phonemizer->phonemize(text, raw_stress);
    }

    // Повний синтез (глобальна пікова нормалізація) -> PCM16.
    // vb_mode: розгорнути цифри/дати/час перед фонемізацією.
    // tm/vtext_out: опційне інструментування та розгорнутий текст (metrics).
    std::vector<int16_t> synthesize_pcm(const std::string& text, const std::string& voice,
                                        float speed, bool raw_stress,
                                        VbMode vb_mode = VbMode::Neural,
                                        Timings* tm = nullptr,
                                        std::string* vtext_out = nullptr) {
        auto t_vb = Clock::now();
        std::string t = apply_verbalize(vb_mode, text);
        if (vtext_out) *vtext_out = t;
        if (tm) tm->vb_ms = ms_since(t_vb);
        const auto v = voice_embedding(voice); // копія: безпечно щодо lifetime/рескану
        std::vector<float> audio;
        {
            std::lock_guard<std::mutex> lk(infer_mu);
            auto t_phon = Clock::now();
            auto parts = phonemizer->phonemize(t, raw_stress);
            if (tm) { tm->phon_ms = ms_since(t_phon); tm->parts = (int)parts.size(); }
            double ort = 0.0;
            bool first_part = true;
            for (const auto& p : parts) {
                auto t_ort = Clock::now();
                auto chunk = synth_part_pcm_impl(p, voice, speed);
                ort += ms_since(t_ort);
                // Тиша між реченнями (частинами) — пауза навіть тоді, коли
                // модель не поставила її сама.
                if (!first_part)
                    audio.insert(audio.end(),
                                 static_cast<size_t>(kGapSec * holos::kSampleRate), 0.f);
                first_part = false;
                audio.insert(audio.end(), chunk.begin(), chunk.end());
            }
            if (tm) tm->ort_ms = ort;
        }
        return holos::float_to_pcm16(audio);
    }

    std::vector<int16_t> synth_part_pcm(const std::string& phonemes, const std::string& voice,
                                        float speed) {
        const auto v = voice_embedding(voice);
        std::lock_guard<std::mutex> lk(infer_mu);
        auto audio = synth_part_pcm_impl(phonemes, voice, speed);
        return holos::float_to_pcm16(audio);
    }
};

Engine g_engine;

bool valid_speed(float s) { return s >= 0.25f && s <= 4.0f; }

// Обрізання пробілів (як name.strip() у hybrid save_voice).
std::string trim(const std::string& s) {
    const size_t b = s.find_first_not_of(" \t\n\r\f\v");
    if (b == std::string::npos) return "";
    const size_t e = s.find_last_not_of(" \t\n\r\f\v");
    return s.substr(b, e - b + 1);
}

// Валідація тексту (як _validate_text).
bool validate_text(const std::string& t, std::string& err) {
    std::string s = t;
    size_t b = s.find_first_not_of(" \t\n\r");
    size_t e = s.find_last_not_of(" \t\n\r");
    std::string trimmed = (b == std::string::npos) ? "" : s.substr(b, e - b + 1);
    if (trimmed.size() < 10) { err = "Текст закороткий (мінімум 10 символів)"; return false; }
    if (t.size() > 50000) { err = "Текст мусить бути менше 50k символів"; return false; }
    return true;
}

HttpResponsePtr json_error(int code, const std::string& detail) {
    Json::Value j; j["detail"] = detail;
    auto resp = HttpResponse::newHttpJsonResponse(j);
    resp->setStatusCode((HttpStatusCode)code);
    return resp;
}

// ---- /api/stress-user: перегляд і редагування словника користувача ----

// Декодування %XX у path-параметрі. '+' НЕ чіпається: drogon уже декодував
// шлях (його urlDecode робить '+' -> пробіл, що зламало б слова з '+'),
// а в нас '+' — частина запису (ка+ва).
std::string path_decode(std::string s) {
    if (s.find('%') == std::string::npos) return s;
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size() && std::isxdigit((unsigned char)s[i + 1]) &&
            std::isxdigit((unsigned char)s[i + 2])) {
            out += char(std::stoi(s.substr(i + 1, 2), nullptr, 16));
            i += 2;
        } else {
            out += s[i];
        }
    }
    return out;
}

// JSON зі вмістом словника користувача: {path, count, words:[{raw,word,stressed}]}.
// Викликати ПІД g_user_file_mtx (читання файлу).
Json::Value stress_user_json() {
    Json::Value j;
    j["path"] = g_user_stress_path;
    j["words"] = Json::arrayValue;
    if (g_user_stress_path.empty()) {
        j["count"] = 0;
        j["error"] = "Шлях до словника користувача не задано";
        return j;
    }
    auto recs = holos::StressDictionary::user_file_read(g_user_stress_path);
    for (auto& r : recs) {
        Json::Value w;
        w["raw"] = r.raw;
        w["word"] = r.word;
        w["stressed"] = r.stressed;
        j["words"].append(w);
    }
    j["count"] = (Json::UInt64)recs.size();
    return j;
}

// POST /api/stress-user — додати/замінити запис {word:"ка+ва"}; після успішного
// запису перечитує словник (живий reload) і повертає оновлений список.
void handle_stress_user_add(const HttpRequestPtr& req,
                            std::function<void(const HttpResponsePtr&)>&& callback) {
    const auto& body = req->getJsonObject();
    if (!body) { callback(json_error(400, "очікується JSON-тіло")); return; }
    const std::string word = body->get("word", "").asString();
    if (g_user_stress_path.empty()) {
        callback(json_error(503, "Шлях до словника користувача не задано (HOLOS_STRESS_USER)"));
        return;
    }
    std::lock_guard lk(g_user_file_mtx);
    std::string err;
    if (!holos::StressDictionary::user_file_upsert(g_user_stress_path, word, &err)) {
        callback(json_error(400, err));
        return;
    }
    if (g_engine.phonemizer) g_engine.phonemizer->reload_user_stress();
    LOG_INFO << "словник користувача: запис додано/оновлено ('" << word << "')";
    Json::Value resp = stress_user_json();
    resp["status"] = "ok";
    callback(HttpResponse::newHttpJsonResponse(resp));
}

// DELETE /api/stress-user/{1} — видалити слово (зі збереженням коментарів).
void handle_stress_user_delete(const std::string& raw_word,
                               std::function<void(const HttpResponsePtr&)>&& callback) {
    const std::string word = trim(path_decode(raw_word));
    if (word.empty()) { callback(json_error(400, "очікується слово")); return; }
    if (g_user_stress_path.empty()) {
        callback(json_error(503, "Шлях до словника користувача не задано (HOLOS_STRESS_USER)"));
        return;
    }
    std::lock_guard lk(g_user_file_mtx);
    std::string err;
    if (!holos::StressDictionary::user_file_remove(g_user_stress_path, word, &err)) {
        callback(json_error(404, err));
        return;
    }
    if (g_engine.phonemizer) g_engine.phonemizer->reload_user_stress();
    LOG_INFO << "словник користувача: запис видалено ('" << word << "')";
    Json::Value resp = stress_user_json();
    resp["status"] = "ok";
    callback(HttpResponse::newHttpJsonResponse(resp));
}

// Спільна валідація конфігу синтезу (HTTP+WS). Повертає помилку або порожній рядок.
std::string validate_synth_cfg(const Json::Value& j, std::string& text, std::string& voice,
                               float& speed, bool& raw_stress, VbMode& vb_mode) {
    text = j.get("text", j.get("input", "")).asString();
    std::string voice_name = j.get("voice", "").asString();
    speed = (float)j.get("speed", 1.0).asDouble();
    raw_stress = j.get("raw_stress", false).asBool();
    vb_mode = parse_vb_mode(j);

    std::string terr;
    if (!validate_text(text, terr)) return terr;
    if (!valid_speed(speed)) return "speed мусить бути в діапазоні 0.25-4.0";
    voice = g_engine.pick_voice(voice_name);
    if (voice.empty()) return "Невідомий голос: " + voice_name;
    return {};
}

// Синтез по частинах + відправлення PCM16-чанків через колбек (спільний для stream/WS).
// Повертає duration_sec. Колбек: (const int16_t* pcm, size_t n) -> bool (false = стоп).
// tm: опційне інструментування (TTFB = час до першого готового чанка).
// cancel: атомарний прапорець — перевірка перед кожною частиною (grain = 1 речення).
// progress: (phase, current, total); phase: "verbalize"|"phonemize"|"synth" (current/total
//   мають сенс лише для "synth"). *cancelled = true якщо зупинено через cancel/стоп-колбек.
using ProgressFn = std::function<void(const std::string& phase, int current, int total)>;

template <typename SendFn>
double synth_stream_send(const std::string& text, const std::string& voice, float speed,
                         bool raw_stress, VbMode vb_mode, SendFn&& send_chunk,
                         Timings* tm = nullptr, std::atomic<bool>* cancel = nullptr,
                         const ProgressFn& progress = nullptr, bool* cancelled = nullptr) {
    auto stopped = [&]() { return cancel && cancel->load(std::memory_order_relaxed); };
    auto t0 = Clock::now();
    if (progress) progress("verbalize", 0, 0);
    std::string t = apply_verbalize(vb_mode, text);
    if (tm) tm->vb_ms = ms_since(t0);
    double duration = 0.0;
    auto t_phon = Clock::now();
    if (progress) progress("phonemize", 0, 0);
    auto parts = g_engine.phonemize(t, raw_stress);
    if (tm) { tm->phon_ms = ms_since(t_phon); tm->parts = (int)parts.size(); }
    const int total = (int)parts.size();
    double ort = 0.0;
    bool first = true;
    bool stopped_flag = false;
    if (progress) progress("synth", 0, total);

    const bool use_stream =
        (g_engine.synth->mode() == holos::SynthesizerMode::Bundle)
        && g_engine.synth->is_streaming_supported();
    std::vector<int16_t> pcm;
    std::vector<float> voice_emb;
    if (use_stream) voice_emb = g_engine.voice_embedding(voice);

    // Одна фонемна частина -> стрім PCM-чанків. Якщо передбачена тривалість
    // перевищує max_frames, рекурсивно ділимо частину навпіл (Issue #8:
    // GUI/десктоп синтезує саме через WS, тому обробка потрібна і тут).
    std::function<bool(const std::string&, int)> stream_part =
        [&](const std::string& phonemes, int depth) -> bool {
        if (stopped()) return false;
        try {
            std::lock_guard<std::mutex> lk(g_engine.infer_mu);
            auto ids = g_engine.tokenizer(phonemes);
            return g_engine.synth->generate_stream(
                ids, voice_emb.data(), speed, holos::StreamingConfig{},
                [&](const std::vector<float>& chunk) -> bool {
                    if (stopped()) return false;
                    pcm = holos::float_to_pcm16(chunk);
                    duration += (double)pcm.size() / holos::kSampleRate;
                    if (first) {
                        if (tm) tm->ttfb_ms = ms_since(t0);
                        first = false;
                    }
                    return send_chunk(pcm.data(), pcm.size());
                });
        } catch (const std::runtime_error& e) {
            const std::string msg = e.what();
            if (msg.find("predicted frames exceed max_frames") == std::string::npos
                || phonemes.size() <= 4 || depth >= 24) {
                throw;
            }
            // Розрив на межі речення (не ламаємо пунктуацію); між реченнями — тиша.
            bool at_sentence = false;
            size_t split = find_long_part_split(phonemes, &at_sentence);
            if (!stream_part(phonemes.substr(0, split), depth + 1)) return false;
            if (at_sentence) {
                std::vector<int16_t> gap(static_cast<size_t>(kGapSec * holos::kSampleRate), 0);
                duration += (double)gap.size() / holos::kSampleRate;
                if (!send_chunk(gap.data(), gap.size())) return false;
            }
            return stream_part(phonemes.substr(split + 1), depth + 1);
        }
    };

    for (int i = 0; i < total; ++i) {
        if (stopped()) { stopped_flag = true; break; }
        // Тиша між реченнями (частинами) після першої відправленої частини.
        if (!first) {
            std::vector<int16_t> gap(static_cast<size_t>(kGapSec * holos::kSampleRate), 0);
            duration += (double)gap.size() / holos::kSampleRate;
            if (!send_chunk(gap.data(), gap.size())) { stopped_flag = true; break; }
        }
        auto t_ort = Clock::now();
        if (use_stream) {
            if (!stream_part(parts[i], 0)) { stopped_flag = true; break; }
        } else {
            auto pcm_chunk = g_engine.synth_part_pcm(parts[i], voice, speed);
            duration += (double)pcm_chunk.size() / holos::kSampleRate;
            if (first) {
                if (tm) tm->ttfb_ms = ms_since(t0);
                first = false;
            }
            if (!send_chunk(pcm_chunk.data(), pcm_chunk.size())) {
                stopped_flag = true;
                break;
            }
        }
        ort += ms_since(t_ort);
        if (progress) progress("synth", i + 1, total);
    }
    if (tm) { tm->ort_ms = ort; tm->total_ms = ms_since(t0); }
    if (cancelled) *cancelled = stopped_flag;
    return duration;
}

// /v1/audio/speech — OpenAI-сумісний Audio Speech API.
void handle_openai_speech(const HttpRequestPtr& req,
                          std::function<void(const HttpResponsePtr&)>&& callback) {
    const auto& body = req->getJsonObject();
    if (!body) { callback(json_error(400, "очікується JSON-тіло")); return; }
    const Json::Value& j = *body;

    std::string response_format = j.get("response_format", "mp3").asString();
    std::string input = j.get("input", "").asString();
    std::string voice_name = j.get("voice", "").asString();
    float speed = (float)j.get("speed", 1.0).asDouble();
    bool raw_stress = j.get("raw_stress", false).asBool();
    bool stream = j.get("stream", false).asBool();
    VbMode vb_mode = parse_vb_mode(j);

    if (response_format != "wav" && response_format != "pcm" && response_format != "mp3") {
        callback(json_error(400, "response_format: підтримуються wav, pcm, mp3")); return;
    }
    std::string terr;
    if (!validate_text(input, terr)) { callback(json_error(400, terr)); return; }
    if (!valid_speed(speed)) { callback(json_error(400, "speed мусить бути в діапазоні 0.25-4.0")); return; }
    std::string voice = g_engine.pick_voice(voice_name);
    if (voice.empty()) { callback(json_error(400, "Невідомий voice: " + voice_name)); return; }

    if (response_format == "mp3") {
        callback(json_error(400, "mp3: кодек не підключено у C++ (використайте wav/pcm)")); return;
    }

    if (!stream) {
        Timings tm;
        auto t_req = Clock::now();
        try {
            auto pcm = g_engine.synthesize_pcm(input, voice, speed, raw_stress, vb_mode, &tm);
            const double audio_sec = (double)pcm.size() / holos::kSampleRate;
            if (response_format == "wav") {
                auto t_enc = Clock::now();
                auto bytes = holos::wav_bytes(pcm, holos::kSampleRate);
                tm.encode_ms = ms_since(t_enc);
                tm.total_ms = ms_since(t_req);
                auto resp = HttpResponse::newHttpResponse();
                resp->addHeader("Content-Type", "audio/wav");
                add_timing_headers(resp, tm, audio_sec);
                resp->setBody(std::string(bytes.begin(), bytes.end()));
                log_metrics("/v1/audio/speech", vb_mode, tm, audio_sec);
                callback(resp);
            } else { // pcm
                tm.total_ms = ms_since(t_req);
                auto resp = HttpResponse::newHttpResponse();
                resp->setContentTypeCode(CT_APPLICATION_OCTET_STREAM);
                add_timing_headers(resp, tm, audio_sec);
                resp->setBody(std::string(reinterpret_cast<const char*>(pcm.data()),
                                          pcm.size() * 2));
                log_metrics("/v1/audio/speech", vb_mode, tm, audio_sec);
                callback(resp);
            }
        } catch (const std::exception& e) {
            callback(json_error(500, e.what()));
        }
        return;
    }

    // stream: true — chunked Transfer-Encoding; WAV: спершу 0xFFFFFFFF-заголовок.
    const bool is_wav = (response_format == "wav");
    auto resp = HttpResponse::newAsyncStreamResponse(
        [input, voice, speed, raw_stress, vb_mode, is_wav](ResponseStreamPtr stream) {
            std::thread([stream = std::move(stream), input, voice, speed, raw_stress, vb_mode, is_wav]() mutable {
                Timings tm;
                try {
                    if (is_wav) {
                        auto hdr = holos::wav_stream_header(holos::kSampleRate);
                        if (!stream->send(std::string(hdr.begin(), hdr.end())))
                            { stream->close(); return; }
                    }
                    double audio_sec = synth_stream_send(input, voice, speed, raw_stress, vb_mode,
                        [&stream](const int16_t* pcm, size_t n) {
                            std::string bytes(reinterpret_cast<const char*>(pcm), n * 2);
                            return stream->send(bytes);
                        },
                        &tm);
                    log_metrics("/v1/audio/speech", vb_mode, tm, audio_sec);
                } catch (...) {}
                stream->close();
            }).detach();
        },
        true); // disableKickoffTimeout
    if (is_wav)
        resp->addHeader("Content-Type", "audio/wav");
    else
        resp->setContentTypeCode(CT_APPLICATION_OCTET_STREAM);
    callback(resp);
}

// /api/synthesize — кастомний API (JSON + audio_base64).
void handle_synthesize(const HttpRequestPtr& req,
                       std::function<void(const HttpResponsePtr&)>&& callback) {
    const auto& body = req->getJsonObject();
    if (!body) { callback(json_error(400, "очікується JSON-тіло")); return; }
    const Json::Value& j = *body;

    std::string text = j.get("text", "").asString();
    std::string voice_name = j.get("voice", "").asString();
    std::string format = j.get("format", "wav").asString();
    float speed = (float)j.get("speed", 1.0).asDouble();
    bool raw_stress = j.get("raw_stress", false).asBool();
    VbMode vb_mode = parse_vb_mode(j);

    if (j.isMember("audio_base64") && !j.get("audio_base64", "").asString().empty()) {
        callback(json_error(404, "audio_base64 у /api/synthesize не підтримується: "
                                 "збережіть голос через POST /api/voices, потім вкажіть його у voice"));
        return;
    }
    if (format != "wav") { callback(json_error(400, "format: у C++ підтримується лише wav")); return; }
    if (!valid_speed(speed)) { callback(json_error(400, "speed мусить бути в діапазоні 0.25-4.0")); return; }
    std::string voice = g_engine.pick_voice(voice_name);
    if (voice.empty()) { callback(json_error(400, "Невідомий голос: " + voice_name)); return; }

    Timings tm;
    auto t_req = Clock::now();
    try {
        // Вербалізуємо/синтезуємо; vtext — розгорнутий текст для відповіді.
        std::string vtext;
        auto pcm = g_engine.synthesize_pcm(text, voice, speed, raw_stress, vb_mode, &tm, &vtext);
        const double audio_sec = (double)pcm.size() / holos::kSampleRate;
        auto t_enc = Clock::now();
        auto bytes = holos::wav_bytes(pcm, holos::kSampleRate);
        std::string b64 = drogon::utils::base64Encode(
            reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size());
        tm.encode_ms = ms_since(t_enc);
        tm.total_ms = ms_since(t_req);
        Json::Value resp;
        resp["format"] = format;
        resp["sample_rate"] = holos::kSampleRate;
        resp["duration_sec"] = audio_sec;
        resp["voice"] = voice;
        resp["text"] = text;
        resp["verbalized_text"] = vtext;
        resp["audio_base64"] = b64;
        if (g_metrics) resp["timings"] = timings_json(tm);
        log_metrics("/api/synthesize", vb_mode, tm, audio_sec);
        callback(HttpResponse::newHttpJsonResponse(resp));
    } catch (const std::exception& e) {
        callback(json_error(500, e.what()));
    }
}

// /api/synthesize_dialogue — мульти-спікерний синтез, у т.ч. поліфонія.
// Вхід: {segments: [{text, voice, speed?, raw_stress?, auto_verbalize?, start_offset_ms?}, ...],
//        format: "wav"}
//   start_offset_ms — абсолютний початок репліки від початку аудіо (ms).
//   Якщо не заданий, репліка йде послідовно за попередньою.
// Вихід: {audio_base64, duration_sec, sample_rate,
//         segments: [{index, voice, speed, text, verbalized_text, duration_sec,
//                     start_offset_ms, raw_stress, auto_verbalize}, ...]}
void handle_synthesize_dialogue(const HttpRequestPtr& req,
                                std::function<void(const HttpResponsePtr&)>&& callback) {
    const auto& body = req->getJsonObject();
    if (!body) { callback(json_error(400, "очікується JSON-тіло")); return; }
    const Json::Value& j = *body;

    if (!j.isMember("segments") || !j["segments"].isArray() || j["segments"].size() == 0) {
        callback(json_error(400, "очікується масив segments з принаймні одним елементом"));
        return;
    }
    std::string format = j.get("format", "wav").asString();
    if (format != "wav") { callback(json_error(400, "format: у C++ підтримується лише wav")); return; }

    const Json::Value& segs = j["segments"];
    constexpr int kMaxSegments = 200;
    constexpr size_t kMaxTotalChars = 50000;
    if (segs.size() > kMaxSegments) {
        callback(json_error(400, "забагато сегментів (максимум " + std::to_string(kMaxSegments) + ")"));
        return;
    }

    std::vector<std::vector<int16_t>> all_pcm;
    std::vector<size_t> offsets;               // початок сегмента у семплах
    Json::Value seg_results(Json::arrayValue);
    Timings tm_total;
    size_t total_chars = 0;
    size_t cursor = 0;                         // кінець останньої репліки

    try {
    for (int i = 0; i < (int)segs.size(); ++i) {
        const Json::Value& s = segs[i];
        std::string text = trim(s.get("text", "").asString());
        if (text.empty()) {
            callback(json_error(400, "segment " + std::to_string(i) + ": text is empty"));
            return;
        }
        total_chars += text.size();
        if (total_chars > kMaxTotalChars) {
            callback(json_error(400, "сумарний текст сегментів перевищує " +
                                         std::to_string(kMaxTotalChars) + " символів"));
            return;
        }
        std::string voice_name = s.get("voice", "").asString();
        float speed = (float)s.get("speed", 1.0).asDouble();
        bool raw_stress = s.get("raw_stress", false).asBool();
        VbMode vb_mode = parse_vb_mode(s);

        if (!valid_speed(speed)) {
            callback(json_error(400, "segment " + std::to_string(i) +
                                         ": speed мусить бути в діапазоні 0.25-4.0"));
            return;
        }
        std::string voice = g_engine.pick_voice(voice_name);
        if (voice.empty()) {
            callback(json_error(400, "segment " + std::to_string(i) + ": Невідомий голос: " + voice_name));
            return;
        }

        // start_offset_ms: абсолютний початок репліки (поліфонія). Без нього —
        // репліка йде послідовно за попередньою.
        size_t start = cursor;
        if (s.isMember("start_offset_ms") && !s["start_offset_ms"].isNull()) {
            double off_ms = s["start_offset_ms"].asDouble();
            if (!(off_ms >= 0.0) || off_ms > 600000.0) {
                callback(json_error(400, "segment " + std::to_string(i) +
                                             ": start_offset_ms мусить бути 0-600000"));
                return;
            }
            start = (size_t)std::llround(off_ms * (double)holos::kSampleRate / 1000.0);
        }

        std::string vtext;
        Timings tm;
        std::vector<int16_t> pcm =
            g_engine.synthesize_pcm(text, voice, speed, raw_stress, vb_mode, &tm, &vtext);
        if (pcm.empty()) {
            callback(json_error(500, "segment " + std::to_string(i) +
                                         ": синтез повернув порожній аудіопотік"));
            return;
        }
        cursor = std::max(cursor, start + pcm.size());
        offsets.push_back(start);
        all_pcm.push_back(std::move(pcm));

        tm_total.vb_ms += tm.vb_ms;
        tm_total.phon_ms += tm.phon_ms;
        tm_total.ort_ms += tm.ort_ms;
        tm_total.parts += tm.parts;

        Json::Value sr;
        sr["index"] = i;
        sr["voice"] = voice;
        sr["speed"] = speed;
        sr["text"] = text;
        sr["verbalized_text"] = vtext;
        sr["duration_sec"] = (double)all_pcm.back().size() / (double)holos::kSampleRate;
        sr["start_offset_ms"] = (double)start * 1000.0 / (double)holos::kSampleRate;
        sr["raw_stress"] = raw_stress;
        sr["auto_verbalize"] =
            (vb_mode == VbMode::Neural ? "neural" : (vb_mode == VbMode::Rule ? "rule" : "off"));
        seg_results.append(sr);
    }
    } catch (const std::exception& e) {
        callback(json_error(500, e.what()));
        return;
    }

    // Змішування з урахуванням оффсетів (перекриття = поліфонія).
    std::vector<int32_t> mix(cursor, 0);
    for (size_t k = 0; k < all_pcm.size(); ++k) {
        const auto& pcm = all_pcm[k];
        const size_t off = offsets[k];
        for (size_t n = 0; n < pcm.size(); ++n) mix[off + n] += (int32_t)pcm[n];
    }
    int32_t peak = 0;
    for (int32_t v : mix) peak = std::max(peak, std::abs(v));
    const double gain = (peak > 32767) ? 32767.0 / (double)peak : 1.0;
    std::vector<int16_t> concat_pcm(mix.size());
    for (size_t n = 0; n < mix.size(); ++n)
        concat_pcm[n] = (int16_t)std::llround((double)mix[n] * gain);

    auto bytes = holos::wav_bytes(concat_pcm, holos::kSampleRate);
    std::string b64 = drogon::utils::base64Encode(
        reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size());

    Json::Value resp;
    resp["format"] = format;
    resp["sample_rate"] = holos::kSampleRate;
    resp["duration_sec"] = (double)concat_pcm.size() / (double)holos::kSampleRate;
    resp["audio_base64"] = b64;
    resp["segments"] = seg_results;
    if (g_metrics) {
        tm_total.total_ms = tm_total.vb_ms + tm_total.phon_ms + tm_total.ort_ms;
        resp["timings"] = timings_json(tm_total);
    }
    log_metrics("/api/synthesize_dialogue", VbMode::Neural, tm_total, resp["duration_sec"].asDouble());
    callback(HttpResponse::newHttpJsonResponse(resp));
}

// /api/voices (POST) — зберегти клонований голос: {name, audio_base64(WAV)}.
// Аудіо декодується (будь-яка частота/формат WAV) -> 24k -> voice_encoder -> 256.
void handle_voices_save(const HttpRequestPtr& req,
                        std::function<void(const HttpResponsePtr&)>&& callback) {
    if (!g_engine.cloning_ready()) {
        callback(json_error(503, "клонування голосу вимкнено (немає voice_encoder.onnx)"));
        return;
    }
    const auto& body = req->getJsonObject();
    if (!body) { callback(json_error(400, "очікується JSON-тіло")); return; }
    const Json::Value& j = *body;

    const std::string name = trim(j.get("name", "").asString());
    if (name.empty()) { callback(json_error(400, "очікується name")); return; }
    if (!holos::CustomVoices::valid_name(name)) {
        callback(json_error(
            400, "Некоректна назва голосу: 1-40 символів (літери/цифри/пробіл/або _-)"));
        return;
    }
    if (g_engine.voices.has(name)) {
        callback(json_error(400, "Назва збігається з пресетом: " + name));
        return;
    }
    const std::string b64 = j.get("audio_base64", "").asString();
    if (b64.empty()) {
        callback(json_error(400, "очікується audio_base64 (WAV)"));
        return;
    }
    const std::string raw = drogon::utils::base64Decode(b64);
    if (raw.empty()) { callback(json_error(400, "некоректний base64")); return; }

    // Один енролмент за раз: пікові буфери енкодера великі (attention 2400²),
    // паралельні завантаження могли б вичерпати RAM контейнера.
    std::lock_guard<std::mutex> clone_lk(g_engine.clone_mu);

    std::vector<float> wave;
    try {
        wave = holos::decode_wav_24k(raw.data(), raw.size());
    } catch (const std::exception& e) {
        callback(json_error(400, e.what()));
        return;
    }

    Timings tm;
    const auto t_enc = Clock::now();
    std::vector<float> emb;
    try {
        emb = g_engine.encoder->encode(wave);
    } catch (const std::exception& e) {
        callback(json_error(500, e.what()));
        return;
    }
    tm.ort_ms = ms_since(t_enc);

    const bool existed = g_engine.custom->has(name);
    try {
        g_engine.custom->save(name, emb);
    } catch (const std::invalid_argument& e) {
        callback(json_error(400, e.what()));
        return;
    } catch (const std::exception& e) {
        callback(json_error(500, e.what()));
        return;
    }

    Json::Value resp;
    resp["status"] = "ok";
    resp["voice"] = name;
    resp["overwritten"] = existed;
    resp["sample_rate"] = holos::kSampleRate;
    resp["duration_sec"] = (double)wave.size() / holos::kSampleRate;
    if (g_metrics) resp["timings"] = timings_json(tm);
    callback(HttpResponse::newHttpJsonResponse(resp));
}

// /api/voices/{1} (DELETE) — видалити кастомний (клонований) голос.
void handle_voices_delete(const std::string& raw_name,
                          std::function<void(const HttpResponsePtr&)>&& callback) {
    if (!g_engine.custom || !g_engine.custom->enabled()) {
        callback(json_error(503, "збереження голосів вимкнено"));
        return;
    }
    const std::string name = trim(drogon::utils::urlDecode(raw_name));
    if (name.empty()) { callback(json_error(400, "очікується назва голосу")); return; }
    if (g_engine.voices.has(name)) {
        callback(json_error(400, "пресет не можна видалити: " + name));
        return;
    }
    bool removed = false;
    try {
        removed = g_engine.custom->remove(name);
    } catch (const std::exception& e) {
        callback(json_error(500, e.what()));
        return;
    }
    if (!removed) {
        callback(json_error(404, "невідомий кастомний голос: " + name));
        return;
    }
    Json::Value resp;
    resp["status"] = "ok";
    resp["voice"] = name;
    callback(HttpResponse::newHttpJsonResponse(resp));
}

} // namespace

// WS /api/ws/synthesize — потоковий синтез: JSON-конфіг -> бінарні PCM16 -> фінальний JSON.
// Між чанками надсилаються текстові кадри {"progress":{phase,current,total}}.
// Клієнт може надіслати {"cancel":true} або закрити з'єднання — синтез зупиняється
// перед наступною частиною (grain = 1 речення); фінальний JSON містить "cancelled".
namespace {
std::mutex g_ws_cancel_mu;
std::unordered_map<const drogon::WebSocketConnection*, std::shared_ptr<std::atomic<bool>>>
    g_ws_cancel;

std::shared_ptr<std::atomic<bool>> ws_cancel_slot(const drogon::WebSocketConnection* conn) {
    std::lock_guard<std::mutex> lk(g_ws_cancel_mu);
    auto it = g_ws_cancel.find(conn);
    if (it != g_ws_cancel.end()) return it->second;
    auto slot = std::make_shared<std::atomic<bool>>(false);
    g_ws_cancel.emplace(conn, slot);
    return slot;
}

void ws_cancel_release(const drogon::WebSocketConnection* conn) {
    std::shared_ptr<std::atomic<bool>> slot;
    {
        std::lock_guard<std::mutex> lk(g_ws_cancel_mu);
        auto it = g_ws_cancel.find(conn);
        if (it == g_ws_cancel.end()) return;
        slot = it->second;
        g_ws_cancel.erase(it);
    }
    slot->store(true); // якщо синтез ще триває — зупинити
}

} // namespace

class WSSynthesize : public drogon::WebSocketController<WSSynthesize> {
public:
    void handleNewConnection(const drogon::HttpRequestPtr&,
                             const drogon::WebSocketConnectionPtr&) override {}

    void handleConnectionClosed(const drogon::WebSocketConnectionPtr& conn) override {
        ws_cancel_release(conn.get());
    }

    void handleNewMessage(const drogon::WebSocketConnectionPtr& conn,
                          std::string&& message,
                          const drogon::WebSocketMessageType& type) override {
        if (type != drogon::WebSocketMessageType::Text) return;

        Json::CharReaderBuilder builder;
        Json::Value cfg;
        std::string errs;
        std::istringstream ss(message);
        if (!Json::parseFromStream(builder, ss, &cfg, &errs)) {
                conn->send(std::string(R"({"error":"невірний JSON"})"),
                           drogon::WebSocketMessageType::Text);
                conn->shutdown();
                return;
        }

        // {"cancel":true} під час активного синтезу — зупинка.
        if (cfg.isMember("cancel") && cfg["cancel"].asBool()) {
            auto slot = ws_cancel_slot(conn.get());
            slot->store(true);
            return;
        }

        // Перевірка чи це діалоговий режим
        bool is_dialogue = cfg.isMember("dialogue") && cfg["dialogue"].asBool();
        
        std::string text, voice, err;
        float speed = 1.0f;
        bool raw_stress = false;
        VbMode vb_mode = VbMode::Neural;
        std::vector<Json::Value> segments;
        
        if (is_dialogue) {
            if (!cfg.isMember("segments") || !cfg["segments"].isArray() || cfg["segments"].size() == 0) {
                Json::Value e; e["error"] = "діалог: очікується масив segments";
                conn->send(Json::writeString(Json::StreamWriterBuilder(), e),
                           drogon::WebSocketMessageType::Text);
                return;
            }
            auto& segs = cfg["segments"];
            if (segs.size() > 200) {
                Json::Value e; e["error"] = "забагато сегментів (максимум 200)";
                conn->send(Json::writeString(Json::StreamWriterBuilder(), e),
                           drogon::WebSocketMessageType::Text);
                return;
            }
            for (int i = 0; i < (int)segs.size(); ++i) {
                const auto& s = segs[i];
                const std::string seg_text = s.get("text", "").asString();
                const std::string seg_voice = s.get("voice", "").asString();
                const float seg_speed = (float)s.get("speed", 1.0).asDouble();
                if (trim(seg_text).empty()) {
                    Json::Value e; e["error"] = "segment " + std::to_string(i) + ": text is empty";
                    conn->send(Json::writeString(Json::StreamWriterBuilder(), e),
                               drogon::WebSocketMessageType::Text);
                    return;
                }
                if (!valid_speed(seg_speed)) {
                    Json::Value e; e["error"] = "segment " + std::to_string(i) +
                                                    ": speed мусить бути в діапазоні 0.25-4.0";
                    conn->send(Json::writeString(Json::StreamWriterBuilder(), e),
                               drogon::WebSocketMessageType::Text);
                    return;
                }
                if (g_engine.pick_voice(seg_voice).empty()) {
                    Json::Value e; e["error"] = "segment " + std::to_string(i) +
                                                    ": Невідомий голос: " + seg_voice;
                    conn->send(Json::writeString(Json::StreamWriterBuilder(), e),
                               drogon::WebSocketMessageType::Text);
                    return;
                }
                segments.push_back(s);
            }
        } else {
            err = validate_synth_cfg(cfg, text, voice, speed, raw_stress, vb_mode);
            if (!err.empty()) {
                Json::Value e; e["error"] = err;
                conn->send(Json::writeString(Json::StreamWriterBuilder(), e),
                           drogon::WebSocketMessageType::Text);
                conn->shutdown();
                return;
            }
        }

        auto cancel = ws_cancel_slot(conn.get());
        cancel->store(false);

        // Синтез у detached thread, щоб не блокувати event loop.
        std::thread([conn, cancel, text, voice, speed, raw_stress, vb_mode, is_dialogue, segments]() {
            Timings tm;
            bool cancelled = false;
            auto send_chunk = [&conn](const int16_t* pcm, size_t n) {
                try {
                    std::string bytes(reinterpret_cast<const char*>(pcm), n * 2);
                    conn->send(bytes, drogon::WebSocketMessageType::Binary);
                    return true;
                } catch (...) {
                    return false; // клієнт уже відключився
                }
            };
            auto progress = [&conn](const std::string& phase, int cur, int total) {
                try {
                    Json::Value p;
                    p["phase"] = phase;
                    if (phase == "synth") { p["current"] = cur; p["total"] = total; }
                    Json::Value msg;
                    msg["progress"] = p;
                    conn->send(Json::writeString(Json::StreamWriterBuilder(), msg),
                               drogon::WebSocketMessageType::Text);
                } catch (...) {}
            };
            try {
                double duration = 0.0;
                if (is_dialogue) {
                    const int total_segs = (int)segments.size();
                    for (int si = 0; si < total_segs; ++si) {
                        if (cancel->load(std::memory_order_relaxed)) { cancelled = true; break; }
                        const Json::Value& s = segments[si];
                        const std::string dtext = s.get("text", "").asString();
                        const std::string dvoice = g_engine.pick_voice(s.get("voice", "").asString());
                        const float dspeed = (float)s.get("speed", 1.0).asDouble();
                        const bool draw = s.get("raw_stress", false).asBool();
                        const VbMode dvb = parse_vb_mode(s);
                        if (dvoice.empty())
                            throw std::runtime_error("Невідомий голос у сегменті " + std::to_string(si));

                        Json::Value segp;
                        segp["phase"] = "segment";
                        segp["current"] = si + 1;
                        segp["total"] = total_segs;
                        Json::Value msg;
                        msg["progress"] = segp;
                        conn->send(Json::writeString(Json::StreamWriterBuilder(), msg),
                                   drogon::WebSocketMessageType::Text);

                        duration += synth_stream_send(dtext, dvoice, dspeed, draw, dvb,
                                                      send_chunk, &tm, cancel.get(), progress, &cancelled);
                        if (cancelled) break;
                    }
                } else {
                    duration = synth_stream_send(text, voice, speed, raw_stress, vb_mode,
                                                 send_chunk, &tm, cancel.get(), progress, &cancelled);
                }
                Json::Value fin;
                fin["done"] = true;
                fin["cancelled"] = cancelled;
                fin["dialogue"] = is_dialogue;
                fin["sample_rate"] = holos::kSampleRate;
                fin["duration_sec"] = std::round(duration * 1000.0) / 1000.0;
                if (g_metrics) fin["timings"] = timings_json(tm);
                log_metrics("/api/ws/synthesize", vb_mode, tm, duration);
                conn->send(Json::writeString(Json::StreamWriterBuilder(), fin),
                           drogon::WebSocketMessageType::Text);
            } catch (const std::exception& e) {
                try {
                    Json::Value er; er["error"] = e.what();
                    conn->send(Json::writeString(Json::StreamWriterBuilder(), er),
                               drogon::WebSocketMessageType::Text);
                } catch (...) {
                    // клієнт відключився — не даємо винятку вбити процес
                }
            } catch (...) {
                try {
                    Json::Value er; er["error"] = "internal server error";
                    conn->send(Json::writeString(Json::StreamWriterBuilder(), er),
                               drogon::WebSocketMessageType::Text);
                } catch (...) {}
            }
            try { conn->shutdown(); } catch (...) {}
            // graceful: дописати буфер (фінальний JSON/чанки) перед закриттям
        }).detach();
    }

    WS_PATH_LIST_BEGIN
    WS_PATH_ADD("/api/ws/synthesize", Get);
    WS_PATH_LIST_END
};

// Статичний веб-UI (embedded HTML) — мінімальний аналог Gradio-демо.
static const char kIndexHtml[] = R"HTML(<!DOCTYPE html>
<html lang="uk">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>HolosTTS (native C++)</title>
<style>
:root{color-scheme:dark}
*{box-sizing:border-box}
body{margin:0;font-family:system-ui,-apple-system,sans-serif;background:#0f1115;color:#e6e6e6;min-height:100vh}
.wrap{max-width:860px;margin:0 auto;padding:2rem 1.25rem}
h1{font-size:1.5rem;margin:0 0 .25rem}
.sub{color:#9aa0aa;font-size:.9rem;margin-bottom:1.5rem}
.card{background:#171a21;border:1px solid #2a2f3a;border-radius:12px;padding:1.25rem;margin-bottom:1rem}
label{display:block;font-size:.85rem;color:#9aa0aa;margin-bottom:.4rem}
textarea{width:100%;min-height:110px;background:#0f1115;color:#e6e6e6;border:1px solid #2a2f3a;border-radius:8px;padding:.75rem;font:inherit;resize:vertical}
select,input[type=range],input[type=text]{width:100%}
select,input[type=text]{background:#0f1115;color:#e6e6e6;border:1px solid #2a2f3a;border-radius:8px;padding:.55rem .6rem;font:inherit}
input[type=file]{width:100%;color:#9aa0aa;font-size:.85rem}
button.small{padding:.4rem .75rem;font-size:.85rem}
.row{display:flex;gap:1rem;flex-wrap:wrap}
.row>div{flex:1;min-width:180px}
button{background:#3b82f6;color:#fff;border:0;border-radius:8px;padding:.65rem 1.25rem;font:inherit;font-weight:600;cursor:pointer}
button:disabled{opacity:.5;cursor:not-allowed}
button.secondary{background:#2a2f3a}
button.voice-preview{background:#2a2f3a;border:1px solid #2a2f3a;border-radius:6px;width:2.3rem;height:2.3rem;display:inline-flex;align-items:center;justify-content:center;font-size:1rem;line-height:1;margin-top:.4rem}
button.voice-preview:hover{background:#3a3f4a;border-color:#3b82f6}
button.voice-preview:disabled{opacity:.5;cursor:not-allowed}
audio{width:100%;margin-top:.75rem}
.status{margin-top:.75rem;font-size:.85rem;color:#9aa0aa;min-height:1.2em}
.status.err{color:#f87171}
.status.ok{color:#4ade80}
.prog{display:none;margin-top:.75rem}
.prog.on{display:block}
.prog-track{height:6px;background:#2a2f3a;border-radius:3px;overflow:hidden}
.prog-fill{height:100%;width:0;background:#3b82f6;transition:width .15s ease}
.prog-label{margin-top:.3rem;font-size:.8rem;color:#9aa0aa}
.mode-tabs{display:flex;gap:.5rem;margin-bottom:1rem;flex-wrap:wrap}
.mode-tab{padding:.55rem 1rem;border-radius:8px;background:#171a21;border:1px solid #2a2f3a;color:#9aa0aa;cursor:pointer;font-size:.9rem;user-select:none}
.mode-tab:hover{border-color:#3b82f6;color:#e6e6e6}
.mode-tab.active{background:#3b82f6;border-color:#3b82f6;color:#fff;font-weight:600}
.stress-table{width:100%;border-collapse:collapse;margin-top:.75rem;font-size:.9rem}
.stress-table th,.stress-table td{text-align:left;padding:.45rem .5rem;border-bottom:1px solid #2a2f3a}
.stress-table th{color:#9aa0aa;font-weight:600;font-size:.8rem}
.stress-table td code{font-size:.95rem}
.stress-del{background:transparent;border:1px solid #2a2f3a;border-radius:6px;color:#ff6b6b;
  padding:.15rem .55rem;cursor:pointer;font:inherit;font-size:.85rem}
.stress-del:hover{border-color:#ff6b6b}
.seg-list{display:flex;flex-direction:column;gap:.75rem}
.seg-item{background:#0f1115;border:1px solid #2a2f3a;border-radius:10px;padding:.75rem;display:flex;flex-direction:column;gap:.6rem}
.seg-item.drag-over{border-color:#3b82f6}
.seg-head{display:flex;gap:.5rem;align-items:center;flex-wrap:wrap;cursor:grab}
.seg-idx{color:#9aa0aa;font-size:.8rem;font-weight:600}
.seg-btns{margin-left:auto;display:flex;gap:.3rem}
.seg-btns button{padding:.35rem .6rem;font-size:.8rem;background:#2a2f3a}
.seg-btns button.danger{background:#7f1d1d}
textarea.seg-text{min-height:60px}
.disclaimer{font-size:.78rem;color:#6b7280;line-height:1.5;border-left:3px solid #374151;padding-left:.75rem;margin-top:1.5rem}
</style>
</head>
<body>
<div class="wrap">
<h1>HolosTTS — український TTS (native C++)</h1>
<p class="sub">Drogon HTTP/WS · ONNX Runtime INT8 CPU · API-паритет HolosTTS-hybrid</p>

<div class="mode-tabs" id="modeTabs">
  <div class="mode-tab active" data-mode="single">Одиночний текст</div>
  <div class="mode-tab" data-mode="dialogue">Діалог (мульти-спікер)</div>
  <div class="mode-tab" data-mode="stress">Наголоси</div>
</div>

<div id="modeSingle">
<div class="card">
  <label for="text">Текст</label>
  <textarea id="text" placeholder="Введіть текст для синтезу (мінімум 10 символів)…"></textarea>
  <div style="margin-top:.75rem;display:flex;gap:.75rem;flex-wrap:wrap;align-items:center">
    <button id="vbBtn" class="secondary" type="button">Вербалізувати цифри</button>
    <label style="margin:0;display:flex;align-items:center;gap:.4rem;color:#e6e6e6;font-size:.9rem">
      Автовербалізація:
      <select id="vbMode" style="width:auto;padding:.35rem .5rem">
        <option value="neural" selected>neural (якість)</option>
        <option value="rule">rule (швидкість)</option>
        <option value="off">вимкнено</option>
      </select>
    </label>
  </div>
  <div class="status" id="vbStatus"></div>
</div>

<div class="card">
  <div class="row">
    <div>
      <label for="voice">Голос</label>
      <select id="voice"></select>
      <button id="voicePreviewBtn" class="voice-preview" type="button" title="Послухати цей голос">🔊</button>
      <button id="delBtn" class="secondary small" type="button" style="margin-top:.5rem" disabled>Видалити клонований голос</button>
    </div>
    <div>
      <label for="speed">Швидкість: <span id="speedVal">1.00</span>×</label>
      <input type="range" id="speed" min="0.25" max="4" step="0.05" value="1">
    </div>
  </div>
  <div style="margin-top:1rem;display:flex;gap:.75rem;flex-wrap:wrap">
    <button id="synthBtn">Синтезувати</button>
    <button id="cancelBtn" class="secondary" type="button" disabled>Скасувати</button>
    <button id="dlBtn" class="secondary" disabled>Завантажити WAV</button>
  </div>
  <div class="prog" id="prog">
    <div class="prog-track"><div class="prog-fill" id="progFill"></div></div>
    <div class="prog-label" id="progLabel"></div>
  </div>
  <div class="status" id="status"></div>
  <audio id="player" controls style="display:none"></audio>
</div>
</div>

<!-- Dialogue mode -->
<div id="modeDialogue" style="display:none">
<div class="card">
  <label>Сегменти діалогу</label>
  <div style="margin-bottom:.75rem;display:flex;gap:.5rem;flex-wrap:wrap;align-items:center">
    <button id="addSegBtn" type="button">+ Додати сегмент</button>
    <button id="clearSegBtn" class="secondary" type="button">Очистити</button>
    <span id="segCount" style="margin-left:auto;color:#9aa0aa;font-size:.85rem">Сегментів: 0</span>
  </div>
  <div class="seg-list" id="segList"></div>
  <p style="font-size:.78rem;color:#6b7280;margin:.6rem 0 0">Без «start_offset_ms» репліки йдуть послідовно; із зазначеним оффсетом — накладаються (поліфонія).</p>
  <div style="margin-top:1rem;display:flex;gap:.75rem;flex-wrap:wrap">
    <button id="synthDlgBtn">Синтезувати діалог</button>
    <button id="cancelDlgBtn" class="secondary" type="button" disabled>Скасувати</button>
    <button id="dlDlgBtn" class="secondary" disabled>Завантажити WAV</button>
  </div>
  <div class="prog" id="progDlg">
    <div class="prog-track"><div class="prog-fill" id="progFillDlg"></div></div>
    <div class="prog-label" id="progLabelDlg"></div>
  </div>
  <div class="status" id="statusDlg"></div>
  <audio id="playerDlg" controls style="display:none"></audio>
</div>

<div class="card">
  <label>Імпорт/Експорт діалогу (JSON)</label>
  <div style="display:flex;gap:.5rem;flex-wrap:wrap;margin-top:.5rem">
    <button id="exportDlgBtn" class="secondary" type="button">Експорт JSON</button>
    <input type="file" id="importDlgFile" accept=".json" style="display:none" onchange="importDialogue(this.files[0])">
    <button id="importDlgBtn" class="secondary" type="button" onclick="document.getElementById('importDlgFile').click()">Імпорт JSON</button>
  </div>
  <p style="font-size:.78rem;color:#6b7280;margin:.5rem 0 0">Формат: {segments: [{text, voice, speed?, raw_stress?, auto_verbalize?, start_offset_ms?}, ...]}</p>
</div>
</div>

<!-- Stress dictionary mode -->
<div id="modeStress" style="display:none">
<div class="card">
  <label>Словник власних наголосів</label>
  <p style="font-size:.78rem;color:#6b7280;margin:.3rem 0 .75rem">
    Записи мають перевагу над основним словником. Формат: <code>ка+ва</code> — «+» одразу після голосної,
    на якій наголос. Додані слова застосовуються одразу, без перезапуску.
  </p>
  <div style="display:flex;gap:.5rem;flex-wrap:wrap;align-items:center">
    <input type="text" id="stressWord" placeholder="напр. ка+ва" style="flex:1;min-width:180px"
           onkeydown="if(event.key==='Enter')stressAdd()">
    <button id="stressAddBtn" type="button" onclick="stressAdd()">Додати</button>
    <button id="stressReloadBtn" class="secondary" type="button" onclick="loadStress()">Оновити список</button>
  </div>
  <div class="status" id="stressStatus"></div>
  <table class="stress-table">
    <thead><tr><th>Запис</th><th>Як читається</th><th style="width:1%"></th></tr></thead>
    <tbody id="stressList"></tbody>
  </table>
  <p style="font-size:.78rem;color:#6b7280;margin:.6rem 0 0">
    <span id="stressCount">—</span><br>
    <span id="stressPath" style="word-break:break-all"></span>
  </p>
</div>
</div>

<div class="card">
  <label>Клонування голосу з WAV</label>
  <div class="row">
    <div>
      <input type="file" id="cloneFile" accept=".wav,audio/wav,audio/x-wav">
    </div>
    <div>
      <input type="text" id="cloneName" maxlength="40" placeholder="Назва голосу (1-40 символів)">
    </div>
  </div>
  <div style="margin-top:1rem;display:flex;gap:.75rem;flex-wrap:wrap;align-items:center">
    <button id="cloneBtn" type="button">Зберегти голос</button>
    <span id="cloneHint" class="status" style="margin:0"></span>
  </div>
  <div class="status" id="cloneStatus"></div>
  <p style="font-size:.78rem;color:#6b7280;margin:.5rem 0 0">
    WAV (PCM/float, будь-яка частота, до 60 с / 16 МБ; модель використовує перші ~30 с). Голос зберігається на сервері
    (volume speakers) і доступний в API за назвою. Не клонуйте чужі голоси без згоди.
  </p>
</div>

<div class="disclaimer">
  <strong>Відмова від відповідальності / Disclaimer</strong><br>
  <em>UA:</em> Цей проєкт є незалежною аматорською розробкою (нативною C++ обгорткою) і <strong>не є офіційним продуктом</strong> автора оригінальної нейромережі. Він жодним чином не афілійований, не спонсорується і не підтримується розробником <a href="https://github.com/patriotyk" style="color:#60a5fa">patriotyk</a>. Усі права на оригінальні моделі, ваги та препроцес належать їхньому законному автору. Проєкт надається «як є» (as is).<br>
  <em>EN:</em> This project is an independent, community-driven native C++ wrapper and <strong>is not an official product</strong> of the original neural network creator. It is not affiliated with, endorsed, or sponsored by <a href="https://github.com/patriotyk" style="color:#60a5fa">patriotyk</a>. All rights to the original models and weights belong to their respective owner. The software is provided «as is», without warranty of any kind.
</div>
</div>

<script>
const $=id=>document.getElementById(id);
const status=(m,cls="")=>{const s=$("status");s.textContent=m;s.className="status "+cls};
let lastBlob=null;
let dlgSegs=[],dlgAbort=null,lastDlgBlob=null,allVoices=[],dragIdx=null;

$("speed").oninput=()=>{$("speedVal").textContent=(+$("speed").value).toFixed(2)};

let customSet=new Set(), cloningReady=false;
function updateDel(){$("delBtn").disabled=!customSet.has($("voice").value)}
async function loadVoices(preferred){
  try{
    const r=await fetch("/api/voices");const d=await r.json();
    customSet=new Set(d.custom||[]);cloningReady=!!d.cloning;allVoices=(d.voices||[]).slice();
    const sel=$("voice");const cur=preferred||sel.value;sel.innerHTML="";
    const groups=[["Пресети",(d.voices||[]).filter(v=>!customSet.has(v))],
                  ["Клоновані",(d.voices||[]).filter(v=>customSet.has(v))]];
    groups.forEach(([label,list])=>{
      if(!list.length)return;
      const g=document.createElement("optgroup");g.label=label;
      list.forEach(v=>{const o=document.createElement("option");o.value=v;o.textContent=v;g.appendChild(o)});
      sel.appendChild(g);
    });
    if(cur&&[...sel.options].some(o=>o.value===cur))sel.value=cur;
    $("cloneBtn").disabled=!cloningReady;
    $("cloneHint").textContent=cloningReady?"":"Клонування недоступне: на сервері немає voice_encoder.onnx.";
    updateDel();
    if(dlgSegs.length)renderSegs();
  }catch(e){status("Не вдалося завантажити голоси: "+e,"err")}
}
loadVoices();
$("voice").onchange=updateDel;

// Спільне прев'ю голосу: коротка фраза -> /api/synthesize -> відтворення у playerEl.
async function playPreview(voice, btn, playerEl, statusFn){
  if(!voice){statusFn("Оберіть голос","err");return}
  const orig=btn.textContent;
  btn.disabled=true;btn.textContent="⏳";
  try{
    const r=await fetch("/api/synthesize",{method:"POST",headers:{"Content-Type":"application/json"},
      body:JSON.stringify({text:"Привіт! Це перевірка голосу.",voice,speed:1,format:"wav",auto_verbalize:"off"})});
    const d=await r.json();
    if(!r.ok)throw new Error(d.detail||("HTTP "+r.status));
    const blob=b64ToBlob(d.audio_base64);
    const url=URL.createObjectURL(blob);
    const p=$(playerEl);p.src=url;p.style.display="";p.play().catch(()=>{});
    p.onended=()=>URL.revokeObjectURL(url);
    setTimeout(()=>URL.revokeObjectURL(url),30000);
    statusFn("Прев'ю голосу: "+voice,"ok");
  }catch(e){statusFn("Прев'ю не вдалося: "+e,"err")}
  finally{btn.disabled=false;btn.textContent=orig}
}
$("voicePreviewBtn").onclick=()=>playPreview($("voice").value,$("voicePreviewBtn"),"player",status);

// Mode switching
const modeTabs=$("modeTabs");
const modeSingle=$("modeSingle");
const modeDialogue=$("modeDialogue");
const modeStress=$("modeStress");
modeTabs.querySelectorAll(".mode-tab").forEach(tab=>{
  tab.onclick=()=>{
    modeTabs.querySelectorAll(".mode-tab").forEach(t=>t.classList.remove("active"));
    tab.classList.add("active");
    const m=tab.dataset.mode;
    modeSingle.style.display=m==="single"?"":"none";
    modeDialogue.style.display=m==="dialogue"?"":"none";
    modeStress.style.display=m==="stress"?"":"none";
    if(m==="stress")loadStress();
  };
});

// ---------- Словник наголосів користувача ----------
function stressStatus(m,cls=""){const s=$("stressStatus");s.textContent=m;s.className="status "+cls}
async function loadStress(){
  try{
    const r=await fetch("/api/stress-user");const d=await r.json();
    if(!r.ok)throw new Error(d.detail||("HTTP "+r.status));
    const tb=$("stressList");tb.innerHTML="";
    $("stressCount").textContent="Слів у словнику: "+(d.count||0);
    $("stressPath").textContent=d.path||"";
    (d.words||[]).forEach(w=>{
      const tr=document.createElement("tr");
      const td1=document.createElement("td");td1.textContent=w.raw;
      const td2=document.createElement("td");
      const code=document.createElement("code");code.textContent=w.stressed;td2.appendChild(code);
      const td3=document.createElement("td");
      const btn=document.createElement("button");
      btn.className="stress-del";btn.type="button";btn.textContent="×";
      btn.title="Видалити запис";
      btn.onclick=()=>delStress(w.word,w.raw);
      td3.appendChild(btn);
      tr.append(td1,td2,td3);tb.appendChild(tr);
    });
    if(d.error)stressStatus(d.error,"err");else stressStatus("");
  }catch(e){stressStatus("Не вдалося завантажити словник: "+e,"err")}
}
async function stressAdd(){
  const inp=$("stressWord");const word=inp.value.trim();
  if(!word){stressStatus("Введіть запис, напр. ка+ва","err");return}
  stressStatus("Додаю…");
  try{
    const r=await fetch("/api/stress-user",{method:"POST",headers:{"Content-Type":"application/json"},
      body:JSON.stringify({word})});
    const d=await r.json();
    if(!r.ok)throw new Error(d.detail||("HTTP "+r.status));
    inp.value="";
    await loadStress();
    stressStatus("Додано: "+word,"ok");
  }catch(e){stressStatus(e.message||String(e),"err")}
}
async function delStress(word,raw){
  if(!confirm("Видалити «"+raw+"» зі словника наголосів?"))return;
  try{
    const r=await fetch("/api/stress-user/"+encodeURIComponent(word),{method:"DELETE"});
    const d=await r.json();
    if(!r.ok)throw new Error(d.detail||("HTTP "+r.status));
    await loadStress();
    stressStatus("Видалено: "+raw,"ok");
  }catch(e){stressStatus(e.message||String(e),"err")}
}
loadStress();

// ---------- Діалоговий режим ----------
function dstatus(m,cls=""){const s=$("statusDlg");s.textContent=m;s.className="status "+cls}
function updateSegCount(){$("segCount").textContent="Сегментів: "+dlgSegs.length}
function setDlgProgress(pct,label){
  const box=$("progDlg");box.classList.add("on");
  $("progFillDlg").style.width=Math.max(0,Math.min(100,pct))+"%";
  $("progLabelDlg").textContent=label||"";
}
function hideDlgProgress(){$("progDlg").classList.remove("on");$("progFillDlg").style.width="0%";$("progLabelDlg").textContent=""}

function fillVoiceSelect(sel,cur){
  sel.innerHTML="";
  allVoices.forEach(v=>{const o=document.createElement("option");o.value=v;o.textContent=v;sel.appendChild(o)});
  if(cur&&allVoices.includes(cur))sel.value=cur;
  else if(allVoices.length)sel.value=allVoices[0];
}

function segButton(text,title,onclick,cls){
  const b=document.createElement("button");b.type="button";b.textContent=text;b.title=title;
  if(cls)b.className=cls;b.onclick=onclick;return b;
}

function renderSegs(){
  const list=$("segList");list.innerHTML="";
  if(!dlgSegs.length){
    const p=document.createElement("p");
    p.style.cssText="color:#6b7280;font-size:.85rem;margin:0";
    p.textContent="Сегментів ще немає. Натисніть «+ Додати сегмент».";
    list.appendChild(p);updateSegCount();return;
  }
  dlgSegs.forEach((seg,idx)=>{
    const item=document.createElement("div");item.className="seg-item";
    const head=document.createElement("div");head.className="seg-head";head.draggable=true;
    head.ondragstart=e=>{dragIdx=idx;if(e.dataTransfer)e.dataTransfer.effectAllowed="move"};
    const lab=document.createElement("span");lab.className="seg-idx";lab.textContent="#"+(idx+1);
    const btns=document.createElement("div");btns.className="seg-btns";
    btns.appendChild(segButton("↑","Вище",()=>moveSeg(idx,-1)));
    btns.appendChild(segButton("↓","Нижче",()=>moveSeg(idx,1)));
    btns.appendChild(segButton("⎘","Дублювати",()=>dupSeg(idx)));
    btns.appendChild(segButton("🗑","Видалити",()=>delSeg(idx),"danger"));
    head.append(lab,btns);
    item.appendChild(head);

    const ta=document.createElement("textarea");
    ta.className="seg-text";ta.rows=2;ta.placeholder="Текст репліки…";
    ta.value=seg.text||"";
    ta.oninput=()=>{dlgSegs[idx].text=ta.value};
    item.appendChild(ta);

    const row=document.createElement("div");row.className="row";

    const c1=document.createElement("div");
    const l1=document.createElement("label");l1.textContent="Голос";
    const sv=document.createElement("select");
    fillVoiceSelect(sv,seg.voice);
    sv.onchange=()=>{dlgSegs[idx].voice=sv.value};
    const pv=document.createElement("button");
    pv.type="button";pv.className="voice-preview";pv.textContent="🔊";
    pv.title="Послухати цей голос";
    pv.onclick=()=>playPreview(sv.value,pv,"playerDlg",dstatus);
    c1.append(l1,sv,pv);

    const c2=document.createElement("div");
    const l2=document.createElement("label");l2.textContent="Швидкість";
    const sp=document.createElement("input");sp.type="range";sp.min="0.5";sp.max="2";sp.step="0.05";
    sp.value=String(seg.speed==null?1:seg.speed);
    const spv=document.createElement("span");spv.style.cssText="color:#9aa0aa;font-size:.8rem;min-width:2.4rem";
    spv.textContent=(+sp.value).toFixed(2);
    sp.oninput=()=>{dlgSegs[idx].speed=+sp.value;spv.textContent=(+sp.value).toFixed(2)};
    const spWrap=document.createElement("div");spWrap.style.cssText="display:flex;align-items:center;gap:.5rem";
    spWrap.append(sp,spv);
    c2.append(l2,spWrap);

    const c3=document.createElement("div");
    const l3=document.createElement("label");l3.textContent="Вербалізація";
    const svb=document.createElement("select");
    [["neural","neural (якість)"],["rule","rule (швидкість)"],["off","вимкнено"]].forEach(([v,t])=>{
      const o=document.createElement("option");o.value=v;o.textContent=t;svb.appendChild(o);
    });
    svb.value=seg.auto_verbalize||"neural";
    svb.onchange=()=>{dlgSegs[idx].auto_verbalize=svb.value};
    c3.append(l3,svb);

    const c4=document.createElement("div");
    const l4=document.createElement("label");l4.textContent="Наголос / оффсет";
    const rawWrap=document.createElement("label");
    rawWrap.style.cssText="display:flex;align-items:center;gap:.4rem;color:#e6e6e6;font-size:.85rem;margin:0 0 .4rem";
    const cb=document.createElement("input");cb.type="checkbox";cb.checked=!!seg.raw_stress;
    cb.onchange=()=>{dlgSegs[idx].raw_stress=cb.checked};
    const cbTxt=document.createElement("span");cbTxt.textContent="raw_stress";
    rawWrap.append(cb,cbTxt);
    const off=document.createElement("input");
    off.type="number";off.min="0";off.step="50";off.placeholder="авто (послідовно)";
    off.value=(seg.start_offset_ms==null)?"":String(seg.start_offset_ms);
    off.oninput=()=>{
      const v=off.value.trim();
      dlgSegs[idx].start_offset_ms=v?Math.max(0,parseInt(v,10)||0):null;
    };
    c4.append(l4,rawWrap,off);

    row.append(c1,c2,c3,c4);
    item.appendChild(row);

    item.ondragover=e=>{if(dragIdx!=null){e.preventDefault();item.classList.add("drag-over")}};
    item.ondragleave=()=>item.classList.remove("drag-over");
    item.ondrop=e=>{
      e.preventDefault();item.classList.remove("drag-over");
      if(dragIdx==null||dragIdx===idx)return;
      const moved=dlgSegs.splice(dragIdx,1)[0];
      dlgSegs.splice(idx,0,moved);
      dragIdx=null;renderSegs();
    };
    list.appendChild(item);
  });
  updateSegCount();
}

function addSeg(){
  dlgSegs.push({text:"",voice:allVoices[0]||"",speed:1,raw_stress:false,
                auto_verbalize:$("vbMode").value,start_offset_ms:null});
  renderSegs();
}
function delSeg(i){dlgSegs.splice(i,1);renderSegs()}
function dupSeg(i){dlgSegs.splice(i+1,0,Object.assign({},dlgSegs[i]));renderSegs()}
function moveSeg(i,d){
  const j=i+d;
  if(j<0||j>=dlgSegs.length)return;
  const moved=dlgSegs.splice(i,1)[0];
  dlgSegs.splice(j,0,moved);renderSegs();
}

function dlgPayload(){
  return dlgSegs.map(s=>{
    const o={text:(s.text||"").trim(),voice:s.voice,speed:(s.speed==null?1:s.speed),
             raw_stress:!!s.raw_stress,auto_verbalize:s.auto_verbalize||"neural"};
    if(s.start_offset_ms!=null)o.start_offset_ms=Math.max(0,Math.round(s.start_offset_ms));
    return o;
  });
}

async function synthDialogue(){
  const segs=dlgPayload();
  if(!segs.length){dstatus("Додайте хоча б один сегмент","err");return}
  if(segs.some(x=>!x.text)){dstatus("У кожному сегменті має бути текст","err");return}
  dlgAbort=new AbortController();
  lastDlgBlob=null;
  $("synthDlgBtn").disabled=true;$("cancelDlgBtn").disabled=false;$("dlDlgBtn").disabled=true;
  dstatus("Синтез діалогу…","");
  setDlgProgress(0,"Синтез…");
  const t0=performance.now();
  try{
    const r=await fetch("/api/synthesize_dialogue",{method:"POST",
      headers:{"Content-Type":"application/json"},
      body:JSON.stringify({format:"wav",segments:segs}),
      signal:dlgAbort.signal});
    const d=await r.json();
    if(!r.ok){dstatus(d.detail||("HTTP "+r.status),"err");hideDlgProgress();return}
    lastDlgBlob=b64ToBlob(d.audio_base64);
    const url=URL.createObjectURL(lastDlgBlob);
    const pl=$("playerDlg");pl.src=url;pl.style.display="";pl.play().catch(()=>{});
    $("dlDlgBtn").disabled=false;
    setDlgProgress(100,"Готово");
    const el=((performance.now()-t0)/1000).toFixed(2);
    dstatus("Готово: "+d.duration_sec.toFixed(2)+" с · "+d.segments.length+
            " сегм. · "+(d.sample_rate/1000)+" kHz ("+el+" с)","ok");
  }catch(e){
    if(e.name==="AbortError")dstatus("Скасовано","");
    else dstatus("Помилка: "+e,"err");
    hideDlgProgress();
  }finally{
    dlgAbort=null;
    $("synthDlgBtn").disabled=false;$("cancelDlgBtn").disabled=true;
  }
}

async function importDialogue(file){
  if(!file)return;
  try{
    const d=JSON.parse(await file.text());
    const segs=(Array.isArray(d)?d:d.segments)||[];
    if(!segs.length)throw new Error("файл не містить segments");
    dlgSegs=segs.map(x=>({
      text:String(x.text==null?"":x.text),
      voice:String(x.voice||allVoices[0]||""),
      speed:(x.speed==null?1:+x.speed),
      raw_stress:!!x.raw_stress,
      auto_verbalize:x.auto_verbalize||"neural",
      start_offset_ms:(x.start_offset_ms==null?null:Math.max(0,parseInt(x.start_offset_ms,10)||0))}));
    renderSegs();
    dstatus("Імпортовано сегментів: "+dlgSegs.length,"ok");
  }catch(e){dstatus("Імпорт не вдався: "+(e.message||e),"err")}
  finally{$("importDlgFile").value=""}
}

$("addSegBtn").onclick=addSeg;
$("clearSegBtn").onclick=()=>{
  if(dlgSegs.length&&!confirm("Очистити всі сегменти?"))return;
  dlgSegs=[];renderSegs();dstatus("");
};
$("synthDlgBtn").onclick=synthDialogue;
$("cancelDlgBtn").onclick=()=>{
  if(dlgAbort){dlgAbort.abort();$("cancelDlgBtn").disabled=true;$("progLabelDlg").textContent="Скасування…"}
};
$("dlDlgBtn").onclick=()=>{
  if(!lastDlgBlob)return;
  const a=document.createElement("a");
  a.href=URL.createObjectURL(lastDlgBlob);a.download="dialogue.wav";a.click();
  setTimeout(()=>URL.revokeObjectURL(a.href),1000);
};
$("exportDlgBtn").onclick=()=>{
  const blob=new Blob([JSON.stringify({segments:dlgPayload()},null,2)],{type:"application/json"});
  const a=document.createElement("a");
  a.href=URL.createObjectURL(blob);a.download="dialogue.json";a.click();
  setTimeout(()=>URL.revokeObjectURL(a.href),1000);
};
renderSegs();

$("vbBtn").onclick=async()=>{
  const text=$("text").value;
  if(!text.trim()){$("vbStatus").textContent="Введіть текст";$("vbStatus").className="status err";return}
  $("vbBtn").disabled=true;$("vbStatus").textContent="Вербалізація…";$("vbStatus").className="status";
  try{
    const r=await fetch("/api/verbalize",{method:"POST",headers:{"Content-Type":"application/json"},
      body:JSON.stringify({text,mode:$("vbMode").value})});
    const d=await r.json();
    if(!r.ok){$("vbStatus").textContent=d.detail||("HTTP "+r.status);$("vbStatus").className="status err";return}
    $("text").value=d.text;
    $("vbStatus").textContent=d.changed?"Цифри розгорнуто":"Цифр не знайдено";
    $("vbStatus").className="status ok";
  }catch(e){$("vbStatus").textContent="Помилка: "+e;$("vbStatus").className="status err"}
  finally{$("vbBtn").disabled=false}
};

function b64ToBlob(b64,type="audio/wav"){
  const bin=atob(b64);const arr=new Uint8Array(bin.length);
  for(let i=0;i<bin.length;i++)arr[i]=bin.charCodeAt(i);
  return new Blob([arr],{type});
}

// WAV-заголовок (44 байти, PCM16 mono) + сирі чанки -> Blob.
function pcmChunksToWav(chunks,sampleRate){
  let dataLen=0;for(const c of chunks)dataLen+=c.byteLength;
  const buf=new ArrayBuffer(44+dataLen),v=new DataView(buf);
  const wstr=(o,s)=>{for(let i=0;i<s.length;i++)v.setUint8(o+i,s.charCodeAt(i))};
  wstr(0,"RIFF");v.setUint32(4,36+dataLen,true);wstr(8,"WAVE");
  wstr(12,"fmt ");v.setUint32(16,16,true);v.setUint16(20,1,true);
  v.setUint16(22,1,true);v.setUint32(24,sampleRate,true);
  v.setUint32(28,sampleRate*2,true);v.setUint16(32,2,true);v.setUint16(34,16,true);
  wstr(36,"data");v.setUint32(40,dataLen,true);
  let off=44;for(const c of chunks){new Uint8Array(buf,off,c.byteLength).set(new Uint8Array(c));off+=c.byteLength}
  return new Blob([buf],{type:"audio/wav"});
}

const PHASE_UA={verbalize:"Вербалізація…",phonemize:"Фонемізація…",synth:"Синтез…"};
function setProgress(phase,current,total){
  const box=$("prog");box.classList.add("on");
  if(phase==="synth"&&total>0){
    const pct=Math.round((current/total)*100);
    $("progFill").style.width=pct+"%";
    $("progLabel").textContent=`Синтез: ${current}/${total} частин (${pct}%)`;
  }else{
    $("progFill").style.width="0%";
    $("progLabel").textContent=PHASE_UA[phase]||phase;
  }
}
function hideProgress(){$("prog").classList.remove("on");$("progFill").style.width="0%";$("progLabel").textContent=""}

let wsSynth=null;
function finishSynthUI(){
  $("synthBtn").disabled=false;$("cancelBtn").disabled=true;wsSynth=null;hideProgress();
}
function playPcmChunks(chunks,sampleRate){
  lastBlob=pcmChunksToWav(chunks,sampleRate);
  const url=URL.createObjectURL(lastBlob);
  const p=$("player");p.src=url;p.style.display="";p.play().catch(()=>{});
  $("dlBtn").disabled=false;
}

// Основний шлях: WS /api/ws/synthesize (бінарні PCM-чанки + прогрес + cancel).
function synthViaWS(text){
  const proto=location.protocol==="https:"?"wss:":"ws:";
  const ws=new WebSocket(`${proto}//${location.host}/api/ws/synthesize`);
  ws.binaryType="arraybuffer";
  const chunks=[];let sampleRate=24000,finished=false;
  wsSynth=ws;
  ws.onopen=()=>ws.send(JSON.stringify({
    text,voice:$("voice").value,speed:+$("speed").value,auto_verbalize:$("vbMode").value
  }));
  ws.onmessage=ev=>{
    if(typeof ev.data==="string"){
      let d;try{d=JSON.parse(ev.data)}catch{return}
      if(d.progress){setProgress(d.progress.phase,d.progress.current|0,d.progress.total|0);return}
      if(d.error){status(d.error,"err");finished=true;ws.close();finishSynthUI();return}
      if(d.done){
        finished=true;
        if(d.cancelled){status("Скасовано","");finishSynthUI();return}
        if(!chunks.length){status("Сервер повернув порожній аудіопотік","err");finishSynthUI();return}
        playPcmChunks(chunks,sampleRate);
        status(`Готово: ${d.duration_sec.toFixed(2)}с · ${$("voice").value} · 24 kHz`,"ok");
        finishSynthUI();
      }
      return;
    }
    chunks.push(ev.data);
  };
  ws.onerror=()=>{};
  ws.onclose=()=>{
    if(finished)return;
    status("З'єднання обірвано — пробую без стрімінгу…","");
    synthViaHttp(text);
  };
}

// Fallback: старий /api/synthesize (sync, без прогресу).
async function synthViaHttp(text){
  try{
    const r=await fetch("/api/synthesize",{method:"POST",headers:{"Content-Type":"application/json"},
      body:JSON.stringify({text,voice:$("voice").value,speed:+$("speed").value,format:"wav",auto_verbalize:$("vbMode").value})});
    const d=await r.json();
    if(!r.ok){status(d.detail||("HTTP "+r.status),"err");return}
    lastBlob=b64ToBlob(d.audio_base64);
    const url=URL.createObjectURL(lastBlob);
    const p=$("player");p.src=url;p.style.display="";p.play().catch(()=>{});
    $("dlBtn").disabled=false;
    status(`Готово: ${d.duration_sec.toFixed(2)}с · ${d.voice} · 24 kHz`,"ok");
  }catch(e){status("Помилка: "+e,"err")}
  finally{finishSynthUI()}
}

$("synthBtn").onclick=()=>{
  const text=$("text").value.trim();
  if(text.length<10){status("Текст закороткий (мінімум 10 символів)","err");return}
  $("synthBtn").disabled=true;$("cancelBtn").disabled=false;
  status("Синтез…","");
  synthViaWS(text);
};

$("cancelBtn").onclick=()=>{
  if(wsSynth&&wsSynth.readyState===WebSocket.OPEN){
    wsSynth.send(JSON.stringify({cancel:true}));
    $("cancelBtn").disabled=true;
    $("progLabel").textContent="Скасування…";
  }
};

const cstatus=(m,cls="")=>{const s=$("cloneStatus");s.textContent=m;s.className="status "+cls};

$("cloneBtn").onclick=async()=>{
  const file=$("cloneFile").files[0];
  const name=$("cloneName").value.trim();
  if(!file){cstatus("Оберіть WAV-файл","err");return}
  if(!name){cstatus("Введіть назву голосу","err");return}
  if(file.size>16*1024*1024){cstatus("Файл завеликий (максимум 16 МБ)","err");return}
  $("cloneBtn").disabled=true;cstatus("Клонування… (енкодер CPU, кілька секунд)");
  try{
    const base64=await new Promise((res,rej)=>{
      const fr=new FileReader();
      fr.onload=()=>res(String(fr.result).split(",")[1]||"");
      fr.onerror=()=>rej(fr.error);
      fr.readAsDataURL(file);
    });
    const r=await fetch("/api/voices",{method:"POST",headers:{"Content-Type":"application/json"},
      body:JSON.stringify({name,audio_base64:base64})});
    const d=await r.json();
    if(!r.ok){cstatus(d.detail||("HTTP "+r.status),"err");return}
    cstatus(`Голос ${d.overwritten?"перезаписано":"збережено"}: ${d.voice} (${d.duration_sec.toFixed(2)} с)`,"ok");
    await loadVoices(d.voice);
  }catch(e){cstatus("Помилка: "+e,"err")}
  finally{$("cloneBtn").disabled=!cloningReady}
};

$("delBtn").onclick=async()=>{
  const name=$("voice").value;
  if(!customSet.has(name))return;
  if(!confirm(`Видалити клонований голос «${name}»?`))return;
  try{
    const r=await fetch("/api/voices/"+encodeURIComponent(name),{method:"DELETE"});
    const d=await r.json();
    if(!r.ok){status(d.detail||("HTTP "+r.status),"err");return}
    status(`Голос видалено: ${name}`,"ok");
    await loadVoices();
  }catch(e){status("Помилка: "+e,"err")}
};

$("dlBtn").onclick=()=>{
  if(!lastBlob)return;
  const a=document.createElement("a");a.href=URL.createObjectURL(lastBlob);a.download="holos.wav";a.click();
};
</script>
</body>
</html>)HTML";

int main() {
    int port = env_int("HOLOS_PORT", 8004);
    std::string host = env("HOLOS_HOST", "0.0.0.0");
    std::string models = env("HOLOS_MODELS_DIR", "/app/models");
    std::string trie = env("HOLOS_TRIE", models + "/stress.trie");
    std::string vocab = env("HOLOS_VOCAB", models + "/vocab.json");
    std::string voices_json = env("HOLOS_VOICES", models + "/voices.json");
    const std::string model_format = env("HOLOS_MODEL_FORMAT", "auto");
    const holos::SynthesizerMode synth_mode = parse_model_format(model_format);
    std::string onnx = resolve_onnx(models, model_format);
    std::string accentor = env("HOLOS_ACCENTOR", models + "/accentor-lite.onnx");
    std::string voice_encoder = env("HOLOS_VOICE_ENCODER", models + "/voice_encoder.onnx");
    std::string speakers_dir = env("HOLOS_SPEAKERS_DIR", "/app/speakers");
    int threads = env_int("HOLOS_THREADS", 0);

    LOG_INFO << "loading models: trie=" << trie << " vocab=" << vocab
             << " voices=" << voices_json << " onnx=" << onnx
             << " accentor=" << accentor;
    LOG_INFO << "model format: " << model_format
             << (synth_mode == holos::SynthesizerMode::Auto ? " (auto-detect)" : "");
    g_engine.load(trie, vocab, voices_json, onnx, accentor, voice_encoder, speakers_dir,
                  threads, synth_mode);
    LOG_INFO << "models loaded; voices=" << g_engine.voice_names.size()
             << " vocab=" << g_engine.tokenizer.vocab_size();
    LOG_INFO << "cloning: " << (g_engine.cloning_ready() ? "ready" : "disabled")
             << " (encoder=" << voice_encoder << " speakers=" << speakers_dir << ")";
    if (g_metrics)
        LOG_INFO << "[startup] engine loaded +"
                 << (long long)std::llround(ms_since(g_start)) << "ms";

#ifdef HOLOS_WITH_VERBALIZER
    {
        std::string vb_dir = env("HOLOS_VERBALIZER_DIR", "");
        if (!vb_dir.empty()) {
            g_neural = holos::make_neural_verbalizer(vb_dir);
            if (g_neural)
                LOG_INFO << "neural verbalizer loaded: " << vb_dir;
            else
                LOG_WARN << "neural verbalizer FAILED to load from " << vb_dir
                         << " (rule fallback)";
        } else {
            LOG_INFO << "HOLOS_VERBALIZER_DIR not set; rule-based verbalizer only";
        }
    }
#endif

    if (g_metrics)
        LOG_INFO << "[startup] ready to listen +"
                 << (long long)std::llround(ms_since(g_start)) << "ms";

    app()
        .setLogPath("./")
        .setLogLevel(trantor::Logger::kWarn)
        // Клонування: WAV у base64 у JSON-тілі (типово drogon ріже на 1 МБ/64 КБ).
        // 24 МБ тіла = WAV до 16 МБ після base64-інфляції (~4/3).
        .setClientMaxBodySize(24 * 1024 * 1024)
        .setClientMaxMemoryBodySize(24 * 1024 * 1024)
        .addListener(host, port)
        .registerHandler("/",
            [](const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&& cb) {
                auto resp = HttpResponse::newHttpResponse();
                resp->setContentTypeCode(CT_TEXT_HTML);
                resp->setBody(kIndexHtml);
                cb(resp);
            },
            {Get})
        .registerHandler("/index.html",
            [](const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&& cb) {
                auto resp = HttpResponse::newHttpResponse();
                resp->setContentTypeCode(CT_TEXT_HTML);
                resp->setBody(kIndexHtml);
                cb(resp);
            },
            {Get})
        .registerHandler("/healthz",
            [](const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&& cb) {
                Json::Value j;
                j["status"] = "ok";
                j["engine"] = "holos-tts-core-native";
                j["version"] = HOLOS_VERSION;
                j["provider"] = "CPUExecutionProvider";
                j["threads"] = g_engine.synth->intra_threads();
                // Стратегія автонаголосу: словник (stress.trie) спершу, для
                // OOV/неоднозначних омонімів — нейро accentor; слова без
                // голосних і є/ґ-ініціальні пропускаються (як в апстрімі).
                j["stress"] = "dictionary-accentor";
                j["voices"] = (int)g_engine.voice_names.size();
                j["cloning"] = g_engine.cloning_ready();
                if (g_engine.synth->mode() == holos::SynthesizerMode::Bundle) {
                    j["model_format"] = "bundle";
                    if (auto sr = g_engine.synth->sample_rate()) j["sample_rate"] = (int)*sr;
                    if (auto spf = g_engine.synth->samples_per_frame()) j["samples_per_frame"] = (int)*spf;
                    if (auto mf = g_engine.synth->max_frames()) j["max_frames"] = (int)*mf;
                    j["streaming"] = g_engine.synth->is_streaming_supported();
                } else {
                    j["model_format"] = "legacy";
                }
#ifdef HOLOS_WITH_VERBALIZER
                j["verbalizer"] = g_neural ? "neural+rule" : "rule";
#else
                j["verbalizer"] = "rule";
#endif
                cb(HttpResponse::newHttpJsonResponse(j));
            },
            {Get})
        .registerHandler("/api/voices",
            [](const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&& cb) {
                Json::Value j;
                j["voices"] = Json::arrayValue;
                for (auto& n : g_engine.all_voice_names()) j["voices"].append(n);
                j["custom"] = Json::arrayValue;
                if (g_engine.custom)
                    for (auto& n : g_engine.custom->names()) j["custom"].append(n);
                j["cloning"] = g_engine.cloning_ready();
                cb(HttpResponse::newHttpJsonResponse(j));
            },
            {Get})
        .registerHandler("/api/voices", &handle_voices_save, {Post})
        .registerHandler("/api/voices/{1}",
            [](const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&& cb,
               std::string name) {
                handle_voices_delete(name, std::move(cb));
            },
            {Delete})
        .registerHandler("/api/stress-user",
            [](const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&& cb) {
                std::lock_guard lk(g_user_file_mtx);
                cb(HttpResponse::newHttpJsonResponse(stress_user_json()));
            },
            {Get})
        .registerHandler("/api/stress-user", &handle_stress_user_add, {Post})
        .registerHandler("/api/stress-user/{1}",
            [](const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&& cb,
               std::string word) {
                handle_stress_user_delete(word, std::move(cb));
            },
            {Delete})
        .registerHandler("/v1/audio/speech", &handle_openai_speech, {Post})
        .registerHandler("/api/synthesize", &handle_synthesize, {Post})
        .registerHandler("/api/synthesize_dialogue", &handle_synthesize_dialogue, {Post})
        .registerHandler("/api/verbalize",
            [](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& cb) {
                // Вербалізація цифр/дат/часу: mode=neural|rule (типово neural).
                const auto& body = req->getJsonObject();
                if (!body) { cb(json_error(400, "очікується JSON-тіло")); return; }
                std::string text = body->get("text", "").asString();
                VbMode mode = VbMode::Neural;
                if (body->isMember("mode")) {
                    Json::Value tmp;
                    tmp["auto_verbalize"] = (*body)["mode"];
                    mode = parse_vb_mode(tmp);
                }
                std::string out = apply_verbalize(mode, text);
                Json::Value resp;
                resp["text"] = out;
                resp["changed"] = (out != text);
#ifdef HOLOS_WITH_VERBALIZER
                resp["mode"] = (mode == VbMode::Rule || !g_neural) ? "rule" : "neural";
#else
                resp["mode"] = "rule";
#endif
                cb(HttpResponse::newHttpJsonResponse(resp));
            },
            {Post})
        .run();
}
