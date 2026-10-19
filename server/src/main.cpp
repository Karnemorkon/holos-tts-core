// M2: Drogon HTTP/WS server (:8004) — API-паритет HolosTTS-hybrid.
#include <drogon/drogon.h>
#include <drogon/WebSocketController.h>

#include "core/audio.h"
#include "core/phonemize.h"
#include "core/synth.h"
#include "core/tokenizer.h"
#include "core/verbalizer.h"
#ifdef HOLOS_WITH_VERBALIZER
#include "core/verbalizer_neural.h"
#endif
#include "core/voices.h"

#include <json/json.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

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

// Глобальний движок (завантажується один раз при старті).
struct Engine {
    std::unique_ptr<holos::Phonemizer> phonemizer;
    holos::Voices voices;
    holos::CharTokenizer tokenizer;
    std::unique_ptr<holos::Synthesizer> synth;
    std::mutex infer_mu; // серіалізуємо фонемізацію+синтез (як _INFER_LOCK)
    std::vector<std::string> voice_names;

    void load(const std::string& trie, const std::string& vocab,
              const std::string& voices_json, const std::string& onnx,
              const std::string& accentor, int threads) {
        phonemizer = std::make_unique<holos::Phonemizer>(trie, accentor);
        voices = holos::Voices::load(voices_json);
        tokenizer = holos::CharTokenizer::from_vocab_json(vocab);
        synth = std::make_unique<holos::Synthesizer>(onnx, threads);
        voice_names = voices.names();
    }

    // Мапінг voice-імені (пресет або OpenAI-імʼя) -> пресет; порожній = невідомий.
    std::string pick_voice(const std::string& name) const {
        if (name.empty()) return voice_names.empty() ? "" : voice_names.front();
        if (voices.has(name)) return name;
        for (int i = 0; i < 6; ++i) {
            if (name == kOpenAIVoices[i])
                return voice_names[size_t(i) % voice_names.size()];
        }
        return ""; // невідомий
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
        const auto& v = voices.get(voice);
        std::vector<float> audio;
        {
            std::lock_guard<std::mutex> lk(infer_mu);
            auto t_phon = Clock::now();
            auto parts = phonemizer->phonemize(t, raw_stress);
            if (tm) { tm->phon_ms = ms_since(t_phon); tm->parts = (int)parts.size(); }
            double ort = 0.0;
            for (const auto& p : parts) {
                auto t_ort = Clock::now();
                auto chunk = synth->generate(tokenizer(p), v.data(), speed);
                ort += ms_since(t_ort);
                audio.insert(audio.end(), chunk.begin(), chunk.end());
            }
            if (tm) tm->ort_ms = ort;
        }
        return holos::float_to_pcm16(audio);
    }

    // Одна фонемна частина -> float аudio (для стрімінгу; per-chunk нормалізація).
    std::vector<int16_t> synth_part_pcm(const std::string& phonemes, const std::string& voice,
                                        float speed) {
        const auto& v = voices.get(voice);
        std::lock_guard<std::mutex> lk(infer_mu);
        auto audio = synth->generate(tokenizer(phonemes), v.data(), speed);
        return holos::float_to_pcm16(audio);
    }
};

Engine g_engine;

bool valid_speed(float s) { return s >= 0.25f && s <= 4.0f; }

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
template <typename SendFn>
double synth_stream_send(const std::string& text, const std::string& voice, float speed,
                         bool raw_stress, VbMode vb_mode, SendFn&& send_chunk,
                         Timings* tm = nullptr) {
    auto t0 = Clock::now();
    std::string t = apply_verbalize(vb_mode, text);
    if (tm) tm->vb_ms = ms_since(t0);
    double duration = 0.0;
    auto t_phon = Clock::now();
    auto parts = g_engine.phonemize(t, raw_stress);
    if (tm) { tm->phon_ms = ms_since(t_phon); tm->parts = (int)parts.size(); }
    double ort = 0.0;
    bool first = true;
    for (const auto& p : parts) {
        auto t_ort = Clock::now();
        auto pcm = g_engine.synth_part_pcm(p, voice, speed);
        ort += ms_since(t_ort);
        duration += (double)pcm.size() / holos::kSampleRate;
        if (first) {
            if (tm) tm->ttfb_ms = ms_since(t0);
            first = false;
        }
        if (!send_chunk(pcm.data(), pcm.size())) break;
    }
    if (tm) { tm->ort_ms = ort; tm->total_ms = ms_since(t0); }
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
        callback(json_error(404, "клонування голосу недоступне у C++ (лише Python)")); return;
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

} // namespace

// WS /api/ws/synthesize — потоковий синтез: JSON-конфіг -> бінарні PCM16 -> фінальний JSON.
class WSSynthesize : public drogon::WebSocketController<WSSynthesize> {
public:
    void handleNewConnection(const drogon::HttpRequestPtr&,
                             const drogon::WebSocketConnectionPtr&) override {}

    void handleConnectionClosed(const drogon::WebSocketConnectionPtr&) override {}

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
            conn->forceClose();
            return;
        }

        std::string text, voice, err;
        float speed = 1.0f;
        bool raw_stress = false;
        VbMode vb_mode = VbMode::Neural;
        err = validate_synth_cfg(cfg, text, voice, speed, raw_stress, vb_mode);
        if (!err.empty()) {
            Json::Value e; e["error"] = err;
            conn->send(Json::writeString(Json::StreamWriterBuilder(), e),
                       drogon::WebSocketMessageType::Text);
            conn->forceClose();
            return;
        }

        // Синтез у detached thread, щоб не блокувати event loop.
        std::thread([conn, text, voice, speed, raw_stress, vb_mode]() {
            Timings tm;
            try {
                double duration = synth_stream_send(
                    text, voice, speed, raw_stress, vb_mode,
                    [&conn](const int16_t* pcm, size_t n) {
                        std::string bytes(reinterpret_cast<const char*>(pcm), n * 2);
                        conn->send(bytes, drogon::WebSocketMessageType::Binary);
                        return true;
                    },
                    &tm);
                Json::Value fin;
                fin["done"] = true;
                fin["sample_rate"] = holos::kSampleRate;
                fin["duration_sec"] = std::round(duration * 1000.0) / 1000.0;
                if (g_metrics) fin["timings"] = timings_json(tm);
                log_metrics("/api/ws/synthesize", vb_mode, tm, duration);
                conn->send(Json::writeString(Json::StreamWriterBuilder(), fin),
                           drogon::WebSocketMessageType::Text);
            } catch (const std::exception& e) {
                Json::Value er; er["error"] = e.what();
                conn->send(Json::writeString(Json::StreamWriterBuilder(), er),
                           drogon::WebSocketMessageType::Text);
            }
            conn->forceClose();
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
select,input[type=range]{width:100%}
select{background:#0f1115;color:#e6e6e6;border:1px solid #2a2f3a;border-radius:8px;padding:.55rem .6rem;font:inherit}
.row{display:flex;gap:1rem;flex-wrap:wrap}
.row>div{flex:1;min-width:180px}
button{background:#3b82f6;color:#fff;border:0;border-radius:8px;padding:.65rem 1.25rem;font:inherit;font-weight:600;cursor:pointer}
button:disabled{opacity:.5;cursor:not-allowed}
button.secondary{background:#2a2f3a}
audio{width:100%;margin-top:.75rem}
.status{margin-top:.75rem;font-size:.85rem;color:#9aa0aa;min-height:1.2em}
.status.err{color:#f87171}
.status.ok{color:#4ade80}
.disclaimer{font-size:.78rem;color:#6b7280;line-height:1.5;border-left:3px solid #374151;padding-left:.75rem;margin-top:1.5rem}
.streambar{height:4px;background:#2a2f3a;border-radius:2px;margin-top:.75rem;overflow:hidden}
.streambar>div{height:100%;width:0;background:#3b82f6;transition:width .15s}
</style>
</head>
<body>
<div class="wrap">
<h1>HolosTTS — український TTS (native C++)</h1>
<p class="sub">Drogon HTTP/WS · ONNX Runtime INT8 CPU · API-паритет HolosTTS-hybrid</p>

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
    </div>
    <div>
      <label for="speed">Швидкість: <span id="speedVal">1.00</span>×</label>
      <input type="range" id="speed" min="0.25" max="4" step="0.05" value="1">
    </div>
  </div>
  <div style="margin-top:1rem;display:flex;gap:.75rem;flex-wrap:wrap">
    <button id="synthBtn">Синтезувати</button>
    <button id="streamBtn" class="secondary">Стрімінг (WS)</button>
    <button id="dlBtn" class="secondary" disabled>Завантажити WAV</button>
  </div>
  <div class="streambar"><div id="sbar"></div></div>
  <div class="status" id="status"></div>
  <audio id="player" controls style="display:none"></audio>
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

$("speed").oninput=()=>{$("speedVal").textContent=(+$("speed").value).toFixed(2)};

async function loadVoices(){
  try{
    const r=await fetch("/api/voices");const d=await r.json();
    const sel=$("voice");sel.innerHTML="";
    (d.voices||[]).forEach(v=>{const o=document.createElement("option");o.value=v;o.textContent=v;sel.appendChild(o)});
  }catch(e){status("Не вдалося завантажити голоси: "+e,"err")}
}
loadVoices();

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

$("synthBtn").onclick=async()=>{
  const text=$("text").value.trim();
  if(text.length<10){status("Текст закороткий (мінімум 10 символів)","err");return}
  $("synthBtn").disabled=true;status("Синтез…");
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
  finally{$("synthBtn").disabled=false}
};

$("dlBtn").onclick=()=>{
  if(!lastBlob)return;
  const a=document.createElement("a");a.href=URL.createObjectURL(lastBlob);a.download="holos.wav";a.click();
};

function pcm16ToWav(pcmChunks,sampleRate=24000){
  let total=0;pcmChunks.forEach(c=>total+=c.length);
  const buf=new ArrayBuffer(44+total*2);const v=new DataView(buf);
  const w=(o,s)=>{for(let i=0;i<s.length;i++)v.setUint8(o+i,s.charCodeAt(i))};
  w(0,"RIFF");v.setUint32(4,36+total*2,true);w(8,"WAVE");w(12,"fmt ");
  v.setUint32(16,16,true);v.setUint16(20,1,true);v.setUint16(22,1,true);
  v.setUint32(24,sampleRate,true);v.setUint32(28,sampleRate*2,true);
  v.setUint16(32,2,true);v.setUint16(34,16,true);w(36,"data");v.setUint32(40,total*2,true);
  let off=44;
  for(const c of pcmChunks){new Uint8Array(buf,off,c.length).set(new Uint8Array(c));off+=c.length}
  return new Blob([buf],{type:"audio/wav"});
}

$("streamBtn").onclick=()=>{
  const text=$("text").value.trim();
  if(text.length<10){status("Текст закороткий (мінімум 10 символів)","err");return}
  $("streamBtn").disabled=true;status("Стрімінг через WebSocket…");
  const ws=new WebSocket((location.protocol==="https:"?"wss://":"ws://")+location.host+"/api/ws/synthesize");
  const parts=[];let gotFinal=false;
  ws.onopen=()=>ws.send(JSON.stringify({text,voice:$("voice").value,speed:+$("speed").value,auto_verbalize:$("vbMode").value}));
  ws.onmessage=e=>{
    if(typeof e.data==="string"){
      const d=JSON.parse(e.data);
      if(d.error){status("Помилка: "+d.error,"err");ws.close();return}
      if(d.done){
        gotFinal=true;
        const blob=pcm16ToWav(parts,24000);
        lastBlob=blob;
        const url=URL.createObjectURL(blob);
        const p=$("player");p.src=url;p.style.display="";p.play().catch(()=>{});
        $("dlBtn").disabled=false;
        status(`Стрімінг готово: ${d.duration_sec.toFixed(2)}с · ${parts.length} чанків · 24 kHz`,"ok");
        ws.close();
      }
    }else{
      parts.push(e.data);
      $("sbar").style.width=Math.min(100,parts.length*12)+"%";
    }
  };
  ws.onerror=()=>{status("WebSocket помилка","err");$("streamBtn").disabled=false};
  ws.onclose=()=>{$("streamBtn").disabled=false;$("sbar").style.width="0"};
};
</script>
</body>
</html>)HTML";

int main() {
    int port = env_int("HOLOS_PORT", 8004);
    std::string models = env("HOLOS_MODELS_DIR", "/app/models");
    std::string trie = env("HOLOS_TRIE", models + "/stress.trie");
    std::string vocab = env("HOLOS_VOCAB", models + "/vocab.json");
    std::string voices_json = env("HOLOS_VOICES", models + "/voices.json");
    std::string onnx = env("HOLOS_ONNX", models + "/holos_cpu_int8.onnx");
    std::string accentor = env("HOLOS_ACCENTOR", models + "/accentor-lite.onnx");
    int threads = env_int("HOLOS_THREADS", 0);

    LOG_INFO << "loading models: trie=" << trie << " vocab=" << vocab
             << " voices=" << voices_json << " onnx=" << onnx
             << " accentor=" << accentor;
    g_engine.load(trie, vocab, voices_json, onnx, accentor, threads);
    LOG_INFO << "models loaded; voices=" << g_engine.voice_names.size()
             << " vocab=" << g_engine.tokenizer.vocab_size();
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
        .addListener("0.0.0.0", port)
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
                j["provider"] = "CPUExecutionProvider";
                j["threads"] = g_engine.synth->intra_threads();
                j["stress"] = "dictionary-A";
                j["voices"] = (int)g_engine.voice_names.size();
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
                for (auto& n : g_engine.voice_names) j["voices"].append(n);
                j["custom"] = Json::arrayValue; // клонування — Python
                cb(HttpResponse::newHttpJsonResponse(j));
            },
            {Get})
        .registerHandler("/v1/audio/speech", &handle_openai_speech, {Post})
        .registerHandler("/api/synthesize", &handle_synthesize, {Post})
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
