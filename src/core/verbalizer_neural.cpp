#include "verbalizer_neural.h"

#include <onnxruntime_cxx_api.h>

#include <sentencepiece_processor.h>

#include <nlohmann/json.hpp>

#include <array>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace holos {
namespace {

constexpr int kDefaultMaxLen = 256;

std::string path_join(const std::string& dir, const std::string& f) {
    if (dir.empty()) return f;
    if (dir.back() == '/') return dir + f;
    return dir + "/" + f;
}

bool file_exists(const std::string& p) {
    std::ifstream f(p);
    return f.good();
}

bool has_digits(const std::string& s) {
    for (unsigned char c : s) {
        if (c >= '0' && c <= '9') return true;
    }
    return false;
}

std::string strip_ws(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\n\r\f\v");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t\n\r\f\v");
    return s.substr(b, e - b + 1);
}

// Розбиття на речення — дзеркало Python re.split(r"(?<=[.!?…])\s+").
std::vector<std::string> split_sentences(const std::string& text) {
    std::vector<std::string> out;
    const size_t n = text.size();
    auto is_ws = [](unsigned char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
    };
    size_t start = 0;
    size_t i = 0;
    while (i < n) {
        size_t punct_end = 0;
        unsigned char c = static_cast<unsigned char>(text[i]);
        if (c == '.' || c == '!' || c == '?') {
            punct_end = i + 1;
        } else if (c == 0xE2 && i + 2 < n &&
                   static_cast<unsigned char>(text[i + 1]) == 0x80 &&
                   static_cast<unsigned char>(text[i + 2]) == 0xA6) {
            punct_end = i + 3;  // …
        }
        if (punct_end > 0 && punct_end < n && is_ws(static_cast<unsigned char>(text[punct_end]))) {
            auto part = strip_ws(text.substr(start, punct_end - start));
            if (!part.empty()) out.push_back(part);
            while (punct_end < n && is_ws(static_cast<unsigned char>(text[punct_end]))) ++punct_end;
            start = punct_end;
            i = punct_end;
        } else {
            ++i;
        }
    }
    if (start < n) {
        auto part = strip_ws(text.substr(start));
        if (!part.empty()) out.push_back(part);
    }
    if (out.empty() && !text.empty()) out.push_back(text);
    return out;
}

int64_t argmax_last(const float* data, int64_t seq, int64_t vocab) {
    const float* row = data + (seq - 1) * vocab;
    int64_t best = 0;
    float best_v = -std::numeric_limits<float>::infinity();
    for (int64_t v = 0; v < vocab; ++v) {
        if (row[v] > best_v) {
            best_v = row[v];
            best = v;
        }
    }
    return best;
}

bool vb_debug() {
    static const bool d = std::getenv("HOLOS_VB_DEBUG") != nullptr;
    return d;
}

void vb_dbg_top(const char* tag, const float* row, int64_t vocab, int k) {
    std::vector<int64_t> idx(vocab);
    for (int64_t i = 0; i < vocab; ++i) idx[i] = i;
    std::partial_sort(idx.begin(), idx.begin() + k, idx.end(),
                      [&](int64_t a, int64_t b) { return row[a] > row[b]; });
    std::fprintf(stderr, "[vbdbg] %s top:", tag);
    for (int i = 0; i < k; ++i)
        std::fprintf(stderr, " %lld=%.3f", (long long)idx[i], row[idx[i]]);
    std::fprintf(stderr, "\n");
}

}  // namespace

struct NeuralVerbalizer::Impl {
    // Імена входів/виходів (класифікуються при init).
    enum class SlotKind { Ids, Mask, EncHidden, Past };

    struct Slot {
        SlotKind kind;
        std::string name;
        int past_idx = -1;  // для dec_past: індекс у past_vals
    };

    Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "holos-verbalizer"};
    Ort::SessionOptions sess_opts;
    Ort::AllocatorWithDefaultOptions allocator;

    Ort::Session enc{nullptr};
    Ort::Session dec_full{nullptr};
    Ort::Session dec_past{nullptr};

    sentencepiece::SentencePieceProcessor sp;

    // HF M2M100 id-мапінг: SP pieces -> model ids (vocab.json), НЕ SP-native ids.
    std::unordered_map<std::string, int> piece_to_id;
    std::vector<std::string> id_to_piece;  // indexed by model id

    int lang_id = 128094;
    int eos_id = 2;
    int decoder_start = 2;
    int max_len = kDefaultMaxLen;
    bool ok = false;
    bool has_cache = false;
    std::string last_error;

    // encoder
    std::vector<Slot> enc_slots;  // Ids, Mask
    std::string enc_out_hidden;

    // decoder_model_quantized (step 0 / fallback)
    std::vector<Slot> full_slots;  // Ids, EncHidden, Mask
    std::string full_out_logits;
    std::vector<std::string> full_out_names;   // [logits, present...]
    std::vector<int> full_present_idx;         // per past input: idx у full_out_names

    // decoder_with_past_model_quantized (steps 1..N)
    std::vector<Slot> past_slots;   // у порядку входів моделі
    std::string past_out_logits;
    std::vector<std::string> past_out_names;   // [logits, present.decoder...]
    std::vector<std::string> past_in_names;    // лише past-входи (в порядку слотів)
    std::vector<int> past_out_idx;             // per past input: idx у past_out_names або -1
    std::vector<int> past_slot_pos;            // per past input: позиція слота в past_slots
    std::vector<int> past_feed_pos;            // per past input: позиція у feed-векторі

    static std::string present_name_for(const std::string& past_name) {
        // "past_key_values.N.X" -> "present.N.X"
        const std::string prefix = "past_key_values.";
        if (past_name.rfind(prefix, 0) == 0)
            return "present." + past_name.substr(prefix.size());
        return past_name;
    }

    bool resolve_names(Ort::Session& s, bool is_past_decoder,
                       std::vector<Slot>& slots, std::string& logits_name) {
        std::vector<std::string> out_names;
        auto mi = s.GetInputCount();
        for (size_t i = 0; i < mi; ++i) {
            auto n = s.GetInputNameAllocated(i, allocator);
            std::string name(n.get());
            Slot sl{SlotKind::Ids, name, -1};
            if (name.find("input_ids") != std::string::npos) {
                sl.kind = SlotKind::Ids;
            } else if (name.find("mask") != std::string::npos) {
                sl.kind = SlotKind::Mask;
            } else if (name.find("encoder_hidden") != std::string::npos) {
                sl.kind = SlotKind::EncHidden;
            } else if (is_past_decoder && name.rfind("past_key_values.", 0) == 0) {
                sl.kind = SlotKind::Past;
                sl.past_idx = static_cast<int>(past_in_names.size());
                past_in_names.push_back(name);
            } else {
                last_error = "unknown decoder input: " + name;
                return false;
            }
            slots.push_back(sl);
        }
        auto mo = s.GetOutputCount();
        for (size_t i = 0; i < mo; ++i) {
            auto n = s.GetOutputNameAllocated(i, allocator);
            out_names.push_back(n.get());
        }
        if (out_names.empty() || out_names[0] != "logits") {
            logits_name = "logits";
        } else {
            logits_name = out_names[0];
        }
        return true;
    }

    bool init(const std::string& dir) {
        try {
            std::string cfg_path = path_join(dir, "verbalizer_config.json");
            if (file_exists(cfg_path)) {
                std::ifstream f(cfg_path);
                nlohmann::json j;
                f >> j;
                lang_id = j.value("lang_token_id", lang_id);
                eos_id = j.value("eos_token_id", eos_id);
                decoder_start = j.value("decoder_start_token_id", decoder_start);
                max_len = j.value("max_length", max_len);
            }

            std::string sp_path = path_join(dir, "sentencepiece.bpe.model");
            if (!file_exists(sp_path)) {
                last_error = "missing sentencepiece.bpe.model in " + dir;
                return false;
            }
            auto st = sp.Load(sp_path);
            if (!st.ok()) {
                last_error = "sentencepiece load failed: " + st.ToString();
                return false;
            }

            // vocab.json: HF piece->id (M2M100 renumbers vs SP-native ids!)
            std::string vocab_path = path_join(dir, "vocab.json");
            if (!file_exists(vocab_path)) {
                last_error = "missing vocab.json in " + dir;
                return false;
            }
            {
                std::ifstream vf(vocab_path);
                nlohmann::json vj;
                vf >> vj;
                int max_id = 0;
                for (auto& [piece, id] : vj.items()) {
                    int iid = id.get<int>();
                    piece_to_id[piece] = iid;
                    if (iid > max_id) max_id = iid;
                }
                id_to_piece.assign(static_cast<size_t>(max_id) + 1, "");
                for (auto& [piece, id] : vj.items()) {
                    id_to_piece[static_cast<size_t>(id.get<int>())] = piece;
                }
            }

            const char* enc_cands[] = {"encoder_model_quantized.onnx",
                                       "encoder_model_optimized.onnx",
                                       "encoder_model.onnx", nullptr};
            const char* full_cands[] = {"decoder_model_quantized.onnx",
                                        "decoder_model_optimized.onnx",
                                        "decoder_model.onnx", nullptr};
            const char* past_cands[] = {"decoder_with_past_model_quantized.onnx",
                                        "decoder_model_with_past_quantized.onnx",
                                        "decoder_with_past_model.onnx", nullptr};
            std::string enc_path, full_path, past_path;
            for (int i = 0; enc_cands[i]; ++i) {
                auto p = path_join(dir, enc_cands[i]);
                if (file_exists(p)) { enc_path = p; break; }
            }
            for (int i = 0; full_cands[i]; ++i) {
                auto p = path_join(dir, full_cands[i]);
                if (file_exists(p)) { full_path = p; break; }
            }
            for (int i = 0; past_cands[i]; ++i) {
                auto p = path_join(dir, past_cands[i]);
                if (file_exists(p)) { past_path = p; break; }
            }
            if (enc_path.empty() || full_path.empty()) {
                last_error = "missing encoder/decoder onnx in " + dir;
                return false;
            }

            sess_opts.SetIntraOpNumThreads(4);
            sess_opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

            enc = Ort::Session(env, enc_path.c_str(), sess_opts);
            dec_full = Ort::Session(env, full_path.c_str(), sess_opts);

            // encoder names
            {
                std::vector<Slot> tmp;
                // resolve вручну (encoder має 2 входи і 1 вихід)
                auto mi = enc.GetInputCount();
                for (size_t i = 0; i < mi; ++i) {
                    auto n = enc.GetInputNameAllocated(i, allocator);
                    std::string name(n.get());
                    Slot sl{SlotKind::Ids, name, -1};
                    if (name.find("mask") != std::string::npos) sl.kind = SlotKind::Mask;
                    enc_slots.push_back(sl);
                }
                auto mo = enc.GetOutputCount();
                if (mo < 1) {
                    last_error = "encoder has no outputs";
                    return false;
                }
                auto n = enc.GetOutputNameAllocated(0, allocator);
                enc_out_hidden = n.get();
            }

            // decoder_model (step0)
            if (!resolve_names(dec_full, false, full_slots, full_out_logits)) return false;
            {
                auto mo = dec_full.GetOutputCount();
                for (size_t i = 0; i < mo; ++i) {
                    auto n = dec_full.GetOutputNameAllocated(i, allocator);
                    full_out_names.push_back(n.get());
                }
            }

            // decoder_with_past (optional)
            if (file_exists(past_path)) {
                past_slots.clear();
                past_in_names.clear();
                dec_past = Ort::Session(env, past_path.c_str(), sess_opts);
                if (!resolve_names(dec_past, true, past_slots, past_out_logits)) return false;
                auto mo = dec_past.GetOutputCount();
                for (size_t i = 0; i < mo; ++i) {
                    auto n = dec_past.GetOutputNameAllocated(i, allocator);
                    past_out_names.push_back(n.get());
                }
                // map past input -> output index
                past_out_idx.resize(past_in_names.size(), -1);
                past_slot_pos.resize(past_in_names.size(), -1);
                past_feed_pos.resize(past_in_names.size(), -1);
                for (size_t i = 0; i < past_slots.size(); ++i) {
                    if (past_slots[i].past_idx >= 0) {
                        int pi = past_slots[i].past_idx;
                        past_slot_pos[pi] = static_cast<int>(i);
                        past_feed_pos[pi] = static_cast<int>(i);
                        auto pn = present_name_for(past_in_names[pi]);
                        for (size_t j = 0; j < past_out_names.size(); ++j) {
                            if (past_out_names[j] == pn) {
                                past_out_idx[pi] = static_cast<int>(j);
                                break;
                            }
                        }
                    }
                }
                // map past input -> step0 (full decoder) output index
                full_present_idx.resize(past_in_names.size(), -1);
                for (size_t pi = 0; pi < past_in_names.size(); ++pi) {
                    auto pn = present_name_for(past_in_names[pi]);
                    for (size_t j = 0; j < full_out_names.size(); ++j) {
                        if (full_out_names[j] == pn) {
                            full_present_idx[pi] = static_cast<int>(j);
                            break;
                        }
                    }
                }
                has_cache = true;
                for (int idx : full_present_idx) {
                    if (idx < 0) { has_cache = false; break; }
                }
            }

            if (vb_debug()) {
                std::fprintf(stderr, "[vbdbg] full_slots:");
                for (auto& s : full_slots)
                    std::fprintf(stderr, " %s(k=%d)", s.name.c_str(), (int)s.kind);
                std::fprintf(stderr, " | past_slots(%zu):", past_slots.size());
                for (auto& s : past_slots)
                    std::fprintf(stderr, " %s(k=%d,p=%d)", s.name.c_str(), (int)s.kind, s.past_idx);
                std::fprintf(stderr, " has_cache=%d lang=%d eos=%d dstart=%d\n",
                             (int)has_cache, lang_id, eos_id, decoder_start);
            }

            ok = true;
            return true;
        } catch (const std::exception& e) {
            last_error = e.what();
            ok = false;
            return false;
        }
    }

    // Encode: lang prefix + vocab.json-mapped SP pieces + eos
    // (дзеркало M2M100Tokenizer.encode: HF ids, не SP-native).
    std::vector<int64_t> encode_sentence(const std::string& s) {
        std::vector<std::string> pieces;
        sp.Encode(s, &pieces).IgnoreError();
        std::vector<int64_t> out;
        out.reserve(pieces.size() + 2);
        out.push_back(lang_id);
        for (auto& p : pieces) {
            auto it = piece_to_id.find(p);
            out.push_back(it != piece_to_id.end() ? static_cast<int64_t>(it->second) : 3 /*unk*/);
        }
        out.push_back(eos_id);
        return out;
    }

    std::string decode_ids(const std::vector<int64_t>& ids) {
        std::vector<std::string> pieces;
        pieces.reserve(ids.size());
        for (auto id : ids) {
            if (id == eos_id || id == lang_id || id == 1 /*pad*/) continue;
            if (id >= 0 && static_cast<size_t>(id) < id_to_piece.size() &&
                !id_to_piece[static_cast<size_t>(id)].empty()) {
                pieces.push_back(id_to_piece[static_cast<size_t>(id)]);
            }
        }
        std::string out;
        sp.Decode(pieces, &out).IgnoreError();
        return out;
    }

    std::string run_sentence(const std::string& sentence) {
        auto src = encode_sentence(sentence);
        const int64_t src_len = static_cast<int64_t>(src.size());
        std::vector<int64_t> mask(src_len, 1);
        std::array<int64_t, 2> shape{1, src_len};

        if (vb_debug()) {
            std::fprintf(stderr, "[vbdbg] in: %s\n", sentence.c_str());
            std::fprintf(stderr, "[vbdbg] src ids:");
            for (auto id : src) std::fprintf(stderr, " %lld", (long long)id);
            std::fprintf(stderr, "\n");
        }

        Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

        // --- encoder ---
        std::vector<Ort::Value> enc_in;
        for (auto& sl : enc_slots) {
            if (sl.kind == SlotKind::Ids)
                enc_in.push_back(Ort::Value::CreateTensor<int64_t>(
                    mem, src.data(), src.size(), shape.data(), 2));
            else
                enc_in.push_back(Ort::Value::CreateTensor<int64_t>(
                    mem, mask.data(), mask.size(), shape.data(), 2));
        }
        std::vector<const char*> enc_in_names;
        for (auto& sl : enc_slots) enc_in_names.push_back(sl.name.c_str());
        const char* enc_out_names[] = {enc_out_hidden.c_str()};
        auto enc_out = enc.Run(Ort::RunOptions{nullptr},
                               enc_in_names.data(), enc_in.data(), enc_in.size(),
                               enc_out_names, 1);

        // Копіюємо hidden у власний буфер (тensors посилаються на зовнішні дані;
        // потрібно пережити кілька Run-викликів).
        const float* hdata = enc_out[0].GetTensorData<float>();
        auto hshape = enc_out[0].GetTensorTypeAndShapeInfo().GetShape();
        std::vector<float> hidden_buf(hdata, hdata + (hshape[1] * hshape[2]));
        std::array<int64_t, 3> hidden_shape{hshape[0], hshape[1], hshape[2]};
        if (vb_debug()) {
            double sum = 0;
            float amax = 0;
            for (float x : hidden_buf) {
                sum += x;
                float a = std::fabs(x);
                if (a > amax) amax = a;
            }
            std::fprintf(stderr, "[vbdbg] hidden n=%zu dims=[%lld,%lld,%lld] mean=%.6f absmax=%.3f\n",
                         hidden_buf.size(), (long long)hshape[0], (long long)hshape[1],
                         (long long)hshape[2], sum / hidden_buf.size(), amax);
        }

        // --- step 0: full decoder [decoder_start] -> lang token ---
        std::vector<int64_t> step0_ids{decoder_start};
        std::array<int64_t, 2> step0_shape{1, 1};

        std::vector<Ort::Value> feed0;
        std::vector<const char*> feed0_names;
        for (auto& sl : full_slots) {
            feed0_names.push_back(sl.name.c_str());
            switch (sl.kind) {
                case SlotKind::Ids:
                    feed0.push_back(Ort::Value::CreateTensor<int64_t>(
                        mem, step0_ids.data(), step0_ids.size(), step0_shape.data(), 2));
                    break;
                case SlotKind::Mask:
                    feed0.push_back(Ort::Value::CreateTensor<int64_t>(
                        mem, mask.data(), mask.size(), shape.data(), 2));
                    break;
                case SlotKind::EncHidden:
                    feed0.push_back(Ort::Value::CreateTensor<float>(
                        mem, hidden_buf.data(), hidden_buf.size(),
                        hidden_shape.data(), hidden_shape.size()));
                    break;
            }
        }
        std::vector<const char*> feed0_out_names;
        feed0_out_names.reserve(full_out_names.size());
        for (auto& n : full_out_names) feed0_out_names.push_back(n.c_str());
        auto res0 = dec_full.Run(Ort::RunOptions{nullptr},
                                 feed0_names.data(), feed0.data(), feed0.size(),
                                 feed0_out_names.data(), feed0_out_names.size());

        // logits: [1, 1, vocab]
        auto li = res0[0].GetTensorTypeAndShapeInfo().GetShape();
        int64_t prev = argmax_last(res0[0].GetTensorData<float>(), li[1], li[2]);
        if (vb_debug()) {
            const float* row0 = res0[0].GetTensorData<float>() + (li[1] - 1) * li[2];
            vb_dbg_top("step0", row0, li[2], 5);
            std::fprintf(stderr, "[vbdbg] step0 argmax=%lld vocab=%lld seq=%lld\n",
                         (long long)prev, (long long)li[2], (long long)li[1]);
        }
        if (prev == eos_id) return "";

        // harvest present KV -> past (відповідно до порядку past_in_names)
        std::vector<Ort::Value> past;
        if (has_cache) {
            past.reserve(past_in_names.size());
            for (size_t pi = 0; pi < past_in_names.size(); ++pi) {
                past.push_back(std::move(res0[full_present_idx[pi]]));
            }
        }

        std::vector<int64_t> out_ids;

        if (has_cache) {
            // --- steps 1..N: decoder_with_past (KV cache) ---
            for (int step = 0; step < max_len; ++step) {
                std::array<int64_t, 2> cur_shape{1, 1};
                std::vector<int64_t> cur{prev};

                std::vector<Ort::Value> feed(past_slots.size());
                std::vector<const char*> feed_names;
                feed_names.reserve(past_slots.size());
                for (size_t i = 0; i < past_slots.size(); ++i) {
                    auto& sl = past_slots[i];
                    feed_names.push_back(sl.name.c_str());
                    if (sl.kind == SlotKind::Ids) {
                        feed[i] = Ort::Value::CreateTensor<int64_t>(
                            mem, cur.data(), cur.size(), cur_shape.data(), 2);
                    } else if (sl.kind == SlotKind::Mask) {
                        feed[i] = Ort::Value::CreateTensor<int64_t>(
                            mem, mask.data(), mask.size(), shape.data(), 2);
                    } else if (sl.kind == SlotKind::Past) {
                        feed[i] = std::move(past[sl.past_idx]);
                    } else {
                        throw std::runtime_error("unexpected slot kind in dec_past");
                    }
                }
                std::vector<const char*> out_names;
                out_names.reserve(past_out_names.size());
                for (auto& n : past_out_names) out_names.push_back(n.c_str());
                auto res = dec_past.Run(Ort::RunOptions{nullptr},
                                        feed_names.data(), feed.data(), feed.size(),
                                        out_names.data(), out_names.size());

                auto ls = res[0].GetTensorTypeAndShapeInfo().GetShape();
                int64_t nxt = argmax_last(res[0].GetTensorData<float>(), ls[1], ls[2]);
                if (vb_debug() && step < 5) {
                    std::fprintf(stderr, "[vbdbg] step %d argmax=%lld seq=%lld\n",
                                 step, (long long)nxt, (long long)ls[1]);
                }

                // refill past: де present є у виходах — беремо новий, інакше (encoder KV)
                // повертаємо попереднє значення з feed.
                std::vector<Ort::Value> new_past(past_in_names.size());
                for (size_t pi = 0; pi < past_in_names.size(); ++pi) {
                    int oi = past_out_idx[pi];
                    if (oi >= 0) {
                        new_past[pi] = std::move(res[oi]);
                    } else {
                        new_past[pi] = std::move(feed[past_feed_pos[pi]]);
                    }
                }
                past = std::move(new_past);

                if (nxt == eos_id) break;
                out_ids.push_back(nxt);
                prev = nxt;
            }
        } else {
            // --- fallback: повний перерахунок кожен крок (decoder_model) ---
            std::vector<int64_t> cur{decoder_start, prev};
            for (int step = 0; step < max_len; ++step) {
                std::array<int64_t, 2> cur_shape{1, static_cast<int64_t>(cur.size())};
                std::vector<Ort::Value> feed;
                std::vector<const char*> feed_names;
                for (auto& sl : full_slots) {
                    feed_names.push_back(sl.name.c_str());
                    switch (sl.kind) {
                        case SlotKind::Ids:
                            feed.push_back(Ort::Value::CreateTensor<int64_t>(
                                mem, cur.data(), cur.size(), cur_shape.data(), 2));
                            break;
                        case SlotKind::Mask:
                            feed.push_back(Ort::Value::CreateTensor<int64_t>(
                                mem, mask.data(), mask.size(), shape.data(), 2));
                            break;
                        case SlotKind::EncHidden:
                            feed.push_back(Ort::Value::CreateTensor<float>(
                                mem, hidden_buf.data(), hidden_buf.size(),
                                hidden_shape.data(), hidden_shape.size()));
                            break;
                    }
                }
                std::vector<const char*> out_names{full_out_logits.c_str()};
                auto res = dec_full.Run(Ort::RunOptions{nullptr},
                                        feed_names.data(), feed.data(), feed.size(),
                                        out_names.data(), out_names.size());
                auto ls = res[0].GetTensorTypeAndShapeInfo().GetShape();
                int64_t nxt = argmax_last(res[0].GetTensorData<float>(), ls[1], ls[2]);
                if (nxt == eos_id) break;
                out_ids.push_back(nxt);
                cur.push_back(nxt);
            }
        }

        return decode_ids(out_ids);
    }
};

NeuralVerbalizer::NeuralVerbalizer(const std::string& model_dir)
    : impl_(new Impl()) {
    impl_->init(model_dir);
}

NeuralVerbalizer::~NeuralVerbalizer() = default;

bool NeuralVerbalizer::ready() const {
    return impl_ && impl_->ok;
}

std::string NeuralVerbalizer::verbalize(const std::string& text) {
    if (!ready()) return text;
    if (!has_digits(text)) return text;

    auto sentences = split_sentences(text);
    std::vector<std::string> results;
    results.reserve(sentences.size());
    for (auto& s : sentences) {
        try {
            results.push_back(impl_->run_sentence(s));
        } catch (const std::exception& e) {
            std::fprintf(stderr, "[verbalizer-neural] sentence failed: %s -> keeping original\n",
                         e.what());
            results.push_back(s);
        }
    }

    std::string out;
    for (size_t i = 0; i < results.size(); ++i) {
        if (i) out += " ";
        out += results[i];
    }
    return out;
}

std::unique_ptr<NeuralVerbalizer> make_neural_verbalizer(const std::string& model_dir) {
    if (model_dir.empty()) return nullptr;
    auto v = std::make_unique<NeuralVerbalizer>(model_dir);
    if (!v->ready()) return nullptr;
    return v;
}

}  // namespace holos
