// M2: Drogon HTTP/WS server (:8004) — API-паритет HolosTTS-hybrid.
#include <drogon/drogon.h>
#include <drogon/WebSocketController.h>

#include "core/audio.h"
#include "core/phonemize.h"
#include "core/synth.h"
#include "core/tokenizer.h"
#include "core/voices.h"

#include <json/json.h>

#include <algorithm>
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

// OpenAI-імена голосів -> мапінг на пресети (як _pick_voice у Python).
const char* kOpenAIVoices[] = {"alloy", "echo", "fable", "onyx", "nova", "shimmer"};

// Глобальний движок (завантажується один раз при старті).
struct Engine {
    std::unique_ptr<holos::Phonemizer> phonemizer;
    holos::Voices voices;
    holos::CharTokenizer tokenizer;
    std::unique_ptr<holos::Synthesizer> synth;
    std::mutex infer_mu; // серіалізуємо фонемізацію+синтез (як _INFER_LOCK)
    std::vector<std::string> voice_names;

    void load(const std::string& trie, const std::string& vocab,
              const std::string& voices_json, const std::string& onnx, int threads) {
        phonemizer = std::make_unique<holos::Phonemizer>(trie);
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
    std::vector<int16_t> synthesize_pcm(const std::string& text, const std::string& voice,
                                        float speed, bool raw_stress) {
        const auto& v = voices.get(voice);
        std::vector<float> audio;
        {
            std::lock_guard<std::mutex> lk(infer_mu);
            for (const auto& p : phonemizer->phonemize(text, raw_stress)) {
                auto chunk = synth->generate(tokenizer(p), v.data(), speed);
                audio.insert(audio.end(), chunk.begin(), chunk.end());
            }
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
                               float& speed, bool& raw_stress) {
    text = j.get("text", j.get("input", "")).asString();
    std::string voice_name = j.get("voice", "").asString();
    speed = (float)j.get("speed", 1.0).asDouble();
    raw_stress = j.get("raw_stress", false).asBool();

    std::string terr;
    if (!validate_text(text, terr)) return terr;
    if (!valid_speed(speed)) return "speed мусить бути в діапазоні 0.25-4.0";
    voice = g_engine.pick_voice(voice_name);
    if (voice.empty()) return "Невідомий голос: " + voice_name;
    return {};
}

// Синтез по частинах + відправлення PCM16-чанків через колбек (спільний для stream/WS).
// Повертає duration_sec. Колбек: (const int16_t* pcm, size_t n) -> bool (false = стоп).
template <typename SendFn>
double synth_stream_send(const std::string& text, const std::string& voice, float speed,
                         bool raw_stress, SendFn&& send_chunk) {
    double duration = 0.0;
    for (const auto& p : g_engine.phonemize(text, raw_stress)) {
        auto pcm = g_engine.synth_part_pcm(p, voice, speed);
        duration += (double)pcm.size() / holos::kSampleRate;
        if (!send_chunk(pcm.data(), pcm.size())) break;
    }
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
        try {
            auto pcm = g_engine.synthesize_pcm(input, voice, speed, raw_stress);
            if (response_format == "wav") {
                auto bytes = holos::wav_bytes(pcm, holos::kSampleRate);
                auto resp = HttpResponse::newHttpResponse();
                resp->addHeader("Content-Type", "audio/wav");
                resp->setBody(std::string(bytes.begin(), bytes.end()));
                callback(resp);
            } else { // pcm
                auto resp = HttpResponse::newHttpResponse();
                resp->setContentTypeCode(CT_APPLICATION_OCTET_STREAM);
                resp->setBody(std::string(reinterpret_cast<const char*>(pcm.data()),
                                          pcm.size() * 2));
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
        [input, voice, speed, raw_stress, is_wav](ResponseStreamPtr stream) {
            std::thread([stream = std::move(stream), input, voice, speed, raw_stress, is_wav]() mutable {
                try {
                    if (is_wav) {
                        auto hdr = holos::wav_stream_header(holos::kSampleRate);
                        if (!stream->send(std::string(hdr.begin(), hdr.end())))
                            { stream->close(); return; }
                    }
                    synth_stream_send(input, voice, speed, raw_stress,
                        [&stream](const int16_t* pcm, size_t n) {
                            std::string bytes(reinterpret_cast<const char*>(pcm), n * 2);
                            return stream->send(bytes);
                        });
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

    if (j.isMember("audio_base64") && !j.get("audio_base64", "").asString().empty()) {
        callback(json_error(404, "клонування голосу недоступне у C++ (лише Python)")); return;
    }
    if (format != "wav") { callback(json_error(400, "format: у C++ підтримується лише wav")); return; }
    if (!valid_speed(speed)) { callback(json_error(400, "speed мусить бути в діапазоні 0.25-4.0")); return; }
    std::string voice = g_engine.pick_voice(voice_name);
    if (voice.empty()) { callback(json_error(400, "Невідомий голос: " + voice_name)); return; }

    try {
        auto pcm = g_engine.synthesize_pcm(text, voice, speed, raw_stress);
        auto bytes = holos::wav_bytes(pcm, holos::kSampleRate);
        std::string b64 = drogon::utils::base64Encode(bytes.data(), bytes.size());
        Json::Value resp;
        resp["format"] = format;
        resp["sample_rate"] = holos::kSampleRate;
        resp["duration_sec"] = (double)pcm.size() / holos::kSampleRate;
        resp["voice"] = voice;
        resp["text"] = text;
        resp["audio_base64"] = b64;
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
        err = validate_synth_cfg(cfg, text, voice, speed, raw_stress);
        if (!err.empty()) {
            Json::Value e; e["error"] = err;
            conn->send(Json::writeString(Json::StreamWriterBuilder(), e),
                       drogon::WebSocketMessageType::Text);
            conn->forceClose();
            return;
        }

        // Синтез у detached thread, щоб не блокувати event loop.
        std::thread([conn, text, voice, speed, raw_stress]() {
            try {
                double duration = synth_stream_send(
                    text, voice, speed, raw_stress,
                    [&conn](const int16_t* pcm, size_t n) {
                        std::string bytes(reinterpret_cast<const char*>(pcm), n * 2);
                        conn->send(bytes, drogon::WebSocketMessageType::Binary);
                        return true;
                    });
                Json::Value fin;
                fin["done"] = true;
                fin["sample_rate"] = holos::kSampleRate;
                fin["duration_sec"] = std::round(duration * 1000.0) / 1000.0;
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

int main() {
    int port = env_int("HOLOS_PORT", 8004);
    std::string models = env("HOLOS_MODELS_DIR", "/app/models");
    std::string trie = env("HOLOS_TRIE", models + "/stress.trie");
    std::string vocab = env("HOLOS_VOCAB", models + "/vocab.json");
    std::string voices_json = env("HOLOS_VOICES", models + "/voices.json");
    std::string onnx = env("HOLOS_ONNX", models + "/holos_cpu_int8.onnx");
    int threads = env_int("HOLOS_THREADS", 0);

    LOG_INFO << "loading models: trie=" << trie << " vocab=" << vocab
             << " voices=" << voices_json << " onnx=" << onnx;
    g_engine.load(trie, vocab, voices_json, onnx, threads);
    LOG_INFO << "models loaded; voices=" << g_engine.voice_names.size()
             << " vocab=" << g_engine.tokenizer.vocab_size();

    app()
        .setLogPath("./")
        .setLogLevel(trantor::Logger::kWarn)
        .addListener("0.0.0.0", port)
        .registerHandler("/healthz",
            [](const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&& cb) {
                Json::Value j;
                j["status"] = "ok";
                j["engine"] = "holos-tts-core-native";
                j["provider"] = "CPUExecutionProvider";
                j["threads"] = g_engine.synth->intra_threads();
                j["stress"] = "dictionary-A";
                j["voices"] = (int)g_engine.voice_names.size();
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
            [](const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&& cb) {
                // Вербалізація (byt5) — Python-only, у C++ немає.
                cb(json_error(404, "вербалізація недоступна у C++ (лише Python)"));
            },
            {Post})
        .run();
}
