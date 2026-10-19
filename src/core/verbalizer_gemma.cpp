#include "verbalizer_gemma.h"

#include "pcre2_util.h"

#include <onnxruntime_cxx_api.h>

#include <sentencepiece_processor.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace holos {
namespace {

constexpr int kMaxNewTokens = 192;

std::string path_join(const std::string& dir, const std::string& f) {
    if (dir.empty()) return f;
    if (dir.back() == '/') return dir + f;
    return dir + "/" + f;
}

bool file_exists(const std::string& p) {
    std::ifstream f(p);
    return f.good();
}

bool has_ascii_digits(const std::string& s) {
    for (unsigned char c : s) {
        if (c >= '0' && c <= '9') return true;
    }
    return false;
}

bool is_ascii_ws(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

// trim ASCII whitespace + NBSP (як Python str.strip / re \s).
size_t trim_left(const std::string& s) {
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (is_ascii_ws(c)) { ++i; continue; }
        if (c == 0xC2 && i + 1 < s.size() &&
            static_cast<unsigned char>(s[i + 1]) == 0xA0) {
            i += 2;
            continue;
        }
        break;
    }
    return i;
}

size_t trim_right(const std::string& s, size_t begin) {
    size_t end = s.size();
    while (end > begin) {
        unsigned char c = static_cast<unsigned char>(s[end - 1]);
        if (is_ascii_ws(c)) { --end; continue; }
        if (end >= begin + 2 && c == 0xA0 &&
            static_cast<unsigned char>(s[end - 2]) == 0xC2) {
            end -= 2;
            continue;
        }
        break;
    }
    return end;
}

std::string strip_ws(const std::string& s) {
    size_t b = trim_left(s);
    size_t e = trim_right(s, b);
    return s.substr(b, e - b);
}

std::string group_digits(const std::string& digits) {
    std::vector<std::string> parts;
    for (size_t end = digits.size(); end > 0;) {
        size_t begin = end >= 3 ? end - 3 : 0;
        parts.push_back(digits.substr(begin, end - begin));
        end = begin;
    }
    std::string out;
    for (size_t i = parts.size(); i-- > 0;) {
        if (!out.empty()) out += ' ';
        out += parts[i];
    }
    return out;
}

std::string lower_utf8(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) {
            out += static_cast<char>(std::tolower(c));
            ++i;
            continue;
        }
        if ((c == 0xD0 || c == 0xD1) && i + 1 < s.size()) {
            uint32_t cp = ((c & 0x1F) << 6) |
                          (static_cast<unsigned char>(s[i + 1]) & 0x3F);
            if (cp >= 0x0410 && cp <= 0x042F) cp += 0x20;
            else if (cp >= 0x0400 && cp <= 0x040F) cp += 0x50;
            else if (cp == 0x0490) cp = 0x0491;
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
            i += 2;
            continue;
        }
        out += static_cast<char>(c);
        ++i;
    }
    return out;
}

size_t cp_count(const std::string& s) {
    size_t n = 0;
    for (unsigned char c : s)
        if ((c & 0xC0) != 0x80) ++n;
    return n;
}

bool is_abbrev_before(const std::string& s, size_t dot) {
    size_t j = dot;
    while (j > 0) {
        unsigned char c = static_cast<unsigned char>(s[j - 1]);
        if (std::isalpha(c) || c >= 0x80) --j;
        else break;
    }
    if (j == dot) return false;
    std::string w = lower_utf8(s.substr(j, dot - j));
    if (cp_count(w) == 1) return true;
    static const char* const kAbbrev[] = {
        "рт", "ст", "ім", "вул", "просп", "буд", "кв", "обл", "тис",
        "млн", "млрд", "грн", "коп", "см", "мм", "кг", "км", "стор",
        "рис", "табл", "гл", "англ", "укр", "доц", "проф", "акад",
        "див", "ін", "др", "канд", "ім", "тоб", "напр", "прибл"};
    for (const char* a : kAbbrev)
        if (w == a) return true;
    return false;
}

// Розбиття на речення — дзеркало Python re.split(r"(?<=[.!?…])\s+")
// з винятком для поширених абревіатур (рт. ст., ім., вул. тощо).
std::vector<std::string> split_sentences(const std::string& text) {
    std::vector<std::string> out;
    const size_t n = text.size();
    size_t start = 0;
    size_t i = 0;
    while (i < n) {
        size_t punct_end = 0;
        unsigned char c = static_cast<unsigned char>(text[i]);
        if (c == '.') {
            if (is_abbrev_before(text, i)) {
                ++i;
                continue;
            }
            punct_end = i + 1;
        } else if (c == '!' || c == '?') {
            punct_end = i + 1;
        } else if (c == 0xE2 && i + 2 < n &&
                   static_cast<unsigned char>(text[i + 1]) == 0x80 &&
                   static_cast<unsigned char>(text[i + 2]) == 0xA6) {
            punct_end = i + 3;  // …
        }
        if (punct_end > 0 && punct_end < n &&
            is_ascii_ws(static_cast<unsigned char>(text[punct_end]))) {
            auto part = strip_ws(text.substr(start, punct_end - start));
            if (!part.empty()) out.push_back(part);
            while (punct_end < n &&
                   is_ascii_ws(static_cast<unsigned char>(text[punct_end])))
                ++punct_end;
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

std::string group_str(const std::string& subject, const PCRE2_SIZE* ov, int g) {
    if (ov[2 * g] == PCRE2_UNSET) return "";
    return subject.substr(ov[2 * g], ov[2 * g + 1] - ov[2 * g]);
}

template <typename F>
std::string re_sub_fn(const std::string& pattern, const std::string& subject, F fn) {
    auto re = util::compile(pattern);
    int ngroups = 0;
    pcre2_pattern_info(re.get(), PCRE2_INFO_CAPTURECOUNT, &ngroups);
    pcre2_match_data* md = pcre2_match_data_create_from_pattern(re.get(), nullptr);
    std::string out;
    out.reserve(subject.size());
    PCRE2_SIZE off = 0, len = subject.size();
    while (off <= len) {
        int rc = pcre2_match(re.get(), reinterpret_cast<PCRE2_SPTR>(subject.data()),
                             len, off, 0, md, nullptr);
        if (rc <= 0) break;
        PCRE2_SIZE* ov = pcre2_get_ovector_pointer(md);
        out.append(subject, off, ov[0] - off);
        out += fn(subject, ov, ngroups);
        PCRE2_SIZE me = ov[1];
        if (me == ov[0]) {
            if (me >= len) { off = len + 1; break; }
            unsigned char c = static_cast<unsigned char>(subject[me]);
            size_t step = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
            out.append(subject, me, step);
            off = me + step;
        } else {
            off = me;
        }
    }
    pcre2_match_data_free(md);
    out.append(subject, off, len - off);
    return out;
}

bool re_search(const std::string& pattern, const std::string& subject) {
    auto re = util::compile(pattern);
    pcre2_match_data* md = pcre2_match_data_create_from_pattern(re.get(), nullptr);
    int rc = pcre2_match(re.get(), reinterpret_cast<PCRE2_SPTR>(subject.data()),
                         subject.size(), 0, 0, md, nullptr);
    pcre2_match_data_free(md);
    return rc > 0;
}

}  // namespace

namespace gemma_prep {

bool match_bare_digits(const std::string& text, std::string* digits) {
    size_t b = trim_left(text);
    size_t e = trim_right(text, b);
    if (b >= e) return false;
    std::string d;
    d.reserve(e - b);
    for (size_t i = b; i < e; ++i) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        if (c < '0' || c > '9') return false;
        d.push_back(static_cast<char>(c));
    }
    if (d.size() >= 7 || (d.size() >= 2 && d[0] == '0')) {
        if (digits) *digits = d;
        return true;
    }
    return false;
}

std::string spell_digits(const std::string& digits) {
    static const char* kNames[] = {"нуль", "один", "два",    "три",   "чотири",
                                   "п'ять", "шість", "сім", "вісім", "дев'ять"};
    std::string out;
    for (char c : digits) {
        if (!out.empty()) out += ' ';
        out += kNames[c - '0'];
    }
    return out;
}

std::string add_spacing(const std::string& text) {
    if (text.empty()) return text;
    static const char* kOther =
        "(?i)картк|карта|iban|айбан|телефон|подзвон|дзвон|код\\b|на рахунок|"
        "cvv|імей|imei|паспорт|ідентифікатор";
    if (re_search(kOther, text)) return text;

    static const char* kRun =
        R"((?<![\w:/.\-№])(\d{10,13})(?![\w:/.\-]))";
    std::string s = re_sub_fn(kRun, text,
                              [](const std::string& subject, const PCRE2_SIZE* ov,
                                 int) -> std::string {
                                  std::string d = group_str(subject, ov, 1);
                                  if (d.empty()) return d;
                                  if (d[0] == '0' || d.rfind("380", 0) == 0)
                                      return d;
                                  return group_digits(d);
                              });

    static const char* kGrouped =
        R"((?<![\w:/.\-№])(\d{1,4}(?:[ \x{a0}]\d{1,4})+)(?![\w:/.\-]))";
    return re_sub_fn(kGrouped, s,
                     [](const std::string& subject, const PCRE2_SIZE* ov,
                        int) -> std::string {
                         std::string raw = group_str(subject, ov, 1);
                         if (raw.empty()) return raw;
                         std::vector<std::string> parts;
                         size_t i = 0, start = 0;
                         while (i < raw.size()) {
                             if (raw[i] == ' ') {
                                 parts.push_back(raw.substr(start, i - start));
                                 start = ++i;
                             } else if (static_cast<unsigned char>(raw[i]) == 0xC2 &&
                                        i + 1 < raw.size() &&
                                        static_cast<unsigned char>(raw[i + 1]) == 0xA0) {
                                 parts.push_back(raw.substr(start, i - start));
                                 i += 2;
                                 start = i;
                             } else {
                                 ++i;
                             }
                         }
                         parts.push_back(raw.substr(start));
                         std::string digits;
                         for (auto& p : parts) digits += p;
                         if (digits.empty()) return raw;
                         bool all4 = parts.size() >= 3;
                         for (auto& p : parts)
                             if (p.size() != 4) all4 = false;
                         if (all4) return raw;
                         if (digits[0] == '0' || digits.rfind("380", 0) == 0)
                             return raw;
                         bool grouped_ok = parts[0].size() <= 3;
                         for (size_t k = 1; grouped_ok && k < parts.size(); ++k)
                             if (parts[k].size() != 3) grouped_ok = false;
                         if (grouped_ok) return raw;
                         if (digits.size() < 5 || digits.size() > 15) return raw;
                         return group_digits(digits);
                     });
}

}  // namespace gemma_prep

struct GemmaVerbalizer::Impl {
    struct PastSlot {
        std::string name;
        size_t feed_pos = 0;              // позиція у входах сесії
        std::vector<int64_t> empty_shape; // [1, kv_heads, 0, head_dim]
        size_t out_idx = 0;               // індекс present.* у виходах сесії
    };

    Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "holos-verbalizer-gemma"};
    Ort::SessionOptions sess_opts;
    Ort::AllocatorWithDefaultOptions allocator;
    Ort::MemoryInfo mem_info =
        Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

    Ort::Session sess{nullptr};
    sentencepiece::SentencePieceProcessor sp;

    std::string model_path;
    std::string last_error;
    bool ok = false;

    std::vector<std::string> in_names;
    std::vector<std::string> out_names;
    size_t ids_pos = 0, mask_pos = 0, logits_idx = 0;
    std::vector<PastSlot> past_slots;

    int64_t vocab = 0;
    int bos_id = -1, start_turn_id = -1, end_turn_id = -1, eos_id = -1;
    std::vector<int> eos_ids;
    std::vector<char> banned;
    float dummy_ = 0.0f;  // валідний пойнтер для порожнього past

    static std::string present_for(const std::string& past_name) {
        const std::string prefix = "past_key_values.";
        if (past_name.rfind(prefix, 0) == 0)
            return "present." + past_name.substr(prefix.size());
        return {};
    }

    bool init(const std::string& dir) {
        try {
            model_path = path_join(dir, "model.onnx");
            std::string sp_path = path_join(dir, "tokenizer.model");
            if (!file_exists(model_path)) {
                last_error = "missing model.onnx in " + dir;
                return false;
            }
            if (!file_exists(sp_path)) {
                last_error = "missing tokenizer.model in " + dir;
                return false;
            }
            auto st = sp.Load(sp_path);
            if (!st.ok()) {
                last_error = "sentencepiece load failed: " + st.ToString();
                return false;
            }
            bos_id = sp.PieceToId("<bos>");
            start_turn_id = sp.PieceToId("<start_of_turn>");
            end_turn_id = sp.PieceToId("<end_of_turn>");
            eos_id = sp.PieceToId("<eos>");
            if (bos_id < 0 || start_turn_id < 0 || end_turn_id < 0 || eos_id < 0) {
                last_error = "tokenizer.model lacks Gemma special tokens";
                return false;
            }
            eos_ids = {eos_id, end_turn_id};

            sess_opts.SetIntraOpNumThreads(4);
            sess_opts.SetGraphOptimizationLevel(
                GraphOptimizationLevel::ORT_ENABLE_ALL);
            sess = Ort::Session(env, model_path.c_str(), sess_opts);

            bool has_ids = false, has_mask = false;
            size_t ni = sess.GetInputCount();
            in_names.reserve(ni);
            for (size_t i = 0; i < ni; ++i) {
                auto n = sess.GetInputNameAllocated(i, allocator);
                std::string name(n.get());
                in_names.push_back(name);
                if (name == "input_ids") {
                    ids_pos = i;
                    has_ids = true;
                } else if (name == "attention_mask") {
                    mask_pos = i;
                    has_mask = true;
                } else if (name.rfind("past_key_values.", 0) == 0) {
                    PastSlot slot;
                    slot.name = name;
                    slot.feed_pos = i;
                    auto shape = sess.GetInputTypeInfo(i)
                                     .GetTensorTypeAndShapeInfo()
                                     .GetShape();
                    if (shape.size() != 4 || shape[1] <= 0 || shape[3] <= 0) {
                        last_error = "unsupported past shape for " + name;
                        return false;
                    }
                    slot.empty_shape = {1, shape[1], 0, shape[3]};
                    past_slots.push_back(std::move(slot));
                } else {
                    last_error = "unsupported model input: " + name;
                    return false;
                }
            }
            if (!has_ids || !has_mask || past_slots.empty()) {
                last_error = "model misses input_ids/attention_mask/past";
                return false;
            }

            size_t no = sess.GetOutputCount();
            bool has_logits = false;
            out_names.reserve(no);
            for (size_t i = 0; i < no; ++i) {
                auto n = sess.GetOutputNameAllocated(i, allocator);
                std::string name(n.get());
                out_names.push_back(name);
                if (name == "logits") {
                    logits_idx = i;
                    has_logits = true;
                }
            }
            if (!has_logits) {
                last_error = "model has no logits output";
                return false;
            }
            for (auto& slot : past_slots) {
                auto pn = present_for(slot.name);
                slot.out_idx = out_names.size();
                for (size_t j = 0; j < out_names.size(); ++j) {
                    if (out_names[j] == pn) {
                        slot.out_idx = j;
                        break;
                    }
                }
                if (slot.out_idx >= out_names.size()) {
                    last_error = "model misses output " + pn;
                    return false;
                }
            }

            auto lshape = sess.GetOutputTypeInfo(logits_idx)
                              .GetTensorTypeAndShapeInfo()
                              .GetShape();
            if (lshape.size() != 3 || lshape[2] <= 0) {
                last_error = "unexpected logits shape";
                return false;
            }
            vocab = lshape[2];
            banned.assign(static_cast<size_t>(vocab), 0);
            for (int id = 0; id < sp.GetPieceSize(); ++id) {
                if (id >= vocab) break;
                if (has_ascii_digits(sp.IdToPiece(id)))
                    banned[static_cast<size_t>(id)] = 1;
            }

            ok = true;
            return true;
        } catch (const std::exception& e) {
            last_error = e.what();
            return false;
        }
    }

    std::vector<int64_t> prompt_ids(const std::string& text) {
        std::vector<int64_t> ids;
        std::vector<int> tmp;
        ids.push_back(bos_id);
        ids.push_back(start_turn_id);
        sp.Encode("user\n", &tmp).IgnoreError();
        ids.insert(ids.end(), tmp.begin(), tmp.end());
        tmp.clear();
        sp.Encode(strip_ws(text), &tmp).IgnoreError();
        ids.insert(ids.end(), tmp.begin(), tmp.end());
        ids.push_back(end_turn_id);
        tmp.clear();
        sp.Encode("\n", &tmp).IgnoreError();
        ids.insert(ids.end(), tmp.begin(), tmp.end());
        ids.push_back(start_turn_id);
        tmp.clear();
        sp.Encode("model\n", &tmp).IgnoreError();
        ids.insert(ids.end(), tmp.begin(), tmp.end());
        return ids;
    }

    std::vector<Ort::Value> run(const std::vector<int64_t>& cur_ids,
                                const std::vector<int64_t>& mask,
                                const std::vector<std::vector<float>>& past_bufs) {
        std::vector<Ort::Value> feed;
        feed.reserve(in_names.size());
        std::vector<int64_t> id_shape{1, static_cast<int64_t>(cur_ids.size())};
        std::vector<int64_t> mask_shape{1, static_cast<int64_t>(mask.size())};
        for (size_t pos = 0; pos < in_names.size(); ++pos) {
            if (pos == ids_pos) {
                feed.push_back(Ort::Value::CreateTensor<int64_t>(
                    mem_info, const_cast<int64_t*>(cur_ids.data()), cur_ids.size(),
                    id_shape.data(), id_shape.size()));
            } else if (pos == mask_pos) {
                feed.push_back(Ort::Value::CreateTensor<int64_t>(
                    mem_info, const_cast<int64_t*>(mask.data()), mask.size(),
                    mask_shape.data(), mask_shape.size()));
            } else {
                size_t pi = 0;
                while (pi < past_slots.size() && past_slots[pi].feed_pos != pos) ++pi;
                if (pi >= past_slots.size())
                    throw std::runtime_error("unmapped model input slot");
                const auto& buf = past_bufs[pi];
                const auto& dims = past_slots[pi].empty_shape;
                std::vector<int64_t> shape = dims;
                shape[2] = static_cast<int64_t>(buf.size()) /
                           (dims[0] * dims[1] * dims[3]);
                float* ptr = buf.empty() ? &dummy_ : const_cast<float*>(buf.data());
                feed.push_back(Ort::Value::CreateTensor<float>(
                    mem_info, ptr, buf.size(), shape.data(), shape.size()));
            }
        }
        std::vector<const char*> names;
        names.reserve(in_names.size());
        for (auto& n : in_names) names.push_back(n.c_str());
        std::vector<const char*> onames;
        onames.reserve(out_names.size());
        for (auto& n : out_names) onames.push_back(n.c_str());
        return sess.Run(Ort::RunOptions{nullptr}, names.data(), feed.data(),
                        feed.size(), onames.data(), onames.size());
    }

    void copy_presents(std::vector<Ort::Value>& res,
                       std::vector<std::vector<float>>& past_bufs) {
        for (size_t i = 0; i < past_slots.size(); ++i) {
            auto info = res[past_slots[i].out_idx].GetTensorTypeAndShapeInfo();
            size_t count = info.GetElementCount();
            const float* data = res[past_slots[i].out_idx].GetTensorData<float>();
            past_bufs[i].assign(data, data + count);
        }
    }

    int argmax(const float* row) const {
        int best = 0;
        float best_v = -std::numeric_limits<float>::infinity();
        for (int64_t v = 0; v < vocab; ++v) {
            if (banned[static_cast<size_t>(v)]) continue;
            if (row[v] > best_v) {
                best_v = row[v];
                best = static_cast<int>(v);
            }
        }
        return best;
    }

    std::string run_sentence(const std::string& text) {
        std::vector<int64_t> ids = prompt_ids(text);
        std::vector<int64_t> mask(ids.size(), 1);
        std::vector<std::vector<float>> past_bufs(past_slots.size());

        auto res = run(ids, mask, past_bufs);
        std::vector<int> gen;
        while (static_cast<int>(gen.size()) < kMaxNewTokens) {
            auto lshape = res[logits_idx].GetTensorTypeAndShapeInfo().GetShape();
            const float* data = res[logits_idx].GetTensorData<float>();
            const float* row = data + (lshape[1] - 1) * vocab;
            int nxt = argmax(row);
            bool is_eos = false;
            for (int e : eos_ids)
                if (nxt == e) is_eos = true;
            if (is_eos) break;
            gen.push_back(nxt);
            if (static_cast<int>(gen.size()) >= kMaxNewTokens) break;

            copy_presents(res, past_bufs);
            ids.assign(1, static_cast<int64_t>(nxt));
            mask.push_back(1);
            res = run(ids, mask, past_bufs);
        }

        std::string out;
        if (!gen.empty()) out = sp.DecodeIds(gen);
        return strip_ws(out);
    }
};

GemmaVerbalizer::GemmaVerbalizer(const std::string& model_dir)
    : impl_(new Impl()) {
    if (!impl_->init(model_dir)) {
        std::fprintf(stderr, "[verbalizer-gemma] init failed: %s\n",
                     impl_->last_error.c_str());
    }
}

GemmaVerbalizer::~GemmaVerbalizer() = default;

bool GemmaVerbalizer::ready() const {
    return impl_ && impl_->ok;
}

std::string GemmaVerbalizer::verbalize(const std::string& text) {
    if (!ready()) return text;
    std::string t = strip_ws(text);
    if (t.empty()) return "";
    if (!has_ascii_digits(t)) return text;

    std::string bare;
    if (gemma_prep::match_bare_digits(t, &bare))
        return gemma_prep::spell_digits(bare);

    auto sentences = split_sentences(t);
    std::vector<std::string> results;
    results.reserve(sentences.size());
    for (auto& s : sentences) {
        if (!has_ascii_digits(s)) {
            results.push_back(s);
            continue;
        }
        try {
            std::string pre = gemma_prep::add_spacing(s);
            std::string r = impl_->run_sentence(pre);
            results.push_back(r.empty() ? s : r);
        } catch (const std::exception& e) {
            std::fprintf(stderr,
                         "[verbalizer-gemma] sentence failed: %s -> keeping original\n",
                         e.what());
            results.push_back(s);
        }
    }

    std::string out;
    for (size_t i = 0; i < results.size(); ++i) {
        if (i) out += ' ';
        out += results[i];
    }
    return out;
}

std::unique_ptr<GemmaVerbalizer> make_gemma_verbalizer(const std::string& model_dir) {
    if (model_dir.empty()) return nullptr;
    auto v = std::make_unique<GemmaVerbalizer>(model_dir);
    if (!v->ready()) return nullptr;
    return v;
}

}  // namespace holos
