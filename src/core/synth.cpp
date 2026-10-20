#include "synth.h"
#include "cpu.h"
#include "ort_path.h"

#include <onnxruntime_cxx_api.h>

#include <algorithm>
#include <stdexcept>
#include <thread>

namespace holos {

namespace {

// Ім'я входу ONNX; елементний тип -> рядок для логів/парсингу.
struct InputSpec {
    std::string name;
    ONNXTensorElementDataType elem;
    std::vector<int64_t> shape;
    size_t rank() const { return shape.size(); }
    int64_t last_dim() const { return shape.empty() ? -1 : shape.back(); }
};

// Python _bind_inputs: детерміноване зв'язування за сигнатурою кожної ноди.
struct Binding {
    std::string phonemes; // tensor(string), якщо граф приймає фонемний рядок
    std::string tokens;   // int64 ранг 2
    std::string lengths;  // int64 ранг <=1
    std::string voice;    // float32 [*,256] або ім'я містить voice/style
    std::vector<std::string> controls; // float32 ранг <=1 (speed тощо)
};

constexpr int64_t kVoiceDim = 256;

Binding bind_inputs(const std::vector<InputSpec>& specs) {
    Binding b;
    for (const auto& s : specs) {
        std::string low = s.name;
        for (char& c : low) c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
        if (s.elem == ONNX_TENSOR_ELEMENT_DATA_TYPE_STRING) {
            b.phonemes = s.name;
        } else if ((s.elem == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) &&
                   (s.last_dim() == kVoiceDim || low.find("voice") != std::string::npos ||
                    low.find("style") != std::string::npos)) {
            b.voice = s.name;
        } else if (s.elem == ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64 && s.rank() <= 1) {
            b.lengths = s.name;
        } else if (s.elem == ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64) {
            b.tokens = s.name;
        } else if (s.elem == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT && s.rank() <= 1) {
            b.controls.push_back(s.name);
        } else {
            throw std::runtime_error("unknown ONNX input node: " + s.name);
        }
    }
    if (b.phonemes.empty() && (b.tokens.empty() || b.voice.empty()))
        throw std::runtime_error("failed to bind ONNX inputs");
    return b;
}

} // namespace

struct Synthesizer::Impl {
    Ort::Env env;
    Ort::SessionOptions opts;
    std::unique_ptr<Ort::Session> session;
    Binding binding;
    int intra = 0;

    Impl(const std::string& path, int threads)
        : env(ORT_LOGGING_LEVEL_WARNING, "holos-tts-core") {
        intra = threads > 0 ? threads : default_onnx_threads();
        opts.SetIntraOpNumThreads(intra);
        opts.SetInterOpNumThreads(1);
        opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        session = std::make_unique<Ort::Session>(env, holos::ort_path(path).c_str(), opts);

        Ort::AllocatorWithDefaultOptions alloc;
        std::vector<InputSpec> specs;
        size_t n = session->GetInputCount();
        for (size_t i = 0; i < n; ++i) {
            auto nm = session->GetInputNameAllocated(i, alloc);
            // НЕ ланцюжком: TypeInfo мусить жити, доки читаємо elem/shape
            // (GetTensorTypeAndShapeInfo тримає вказівник у TypeInfo).
            auto typeinfo = session->GetInputTypeInfo(i);
            auto tensorinfo = typeinfo.GetTensorTypeAndShapeInfo();
            auto elem = tensorinfo.GetElementType();
            auto shape = tensorinfo.GetShape();
            specs.push_back(InputSpec{std::string(nm.get()), elem, std::move(shape)});
        }
        binding = bind_inputs(specs);
    }
};

Synthesizer::Synthesizer(const std::string& onnx_path, int intra_threads)
    : impl_(new Impl(onnx_path, intra_threads)) {}

Synthesizer::~Synthesizer() { delete impl_; }

int Synthesizer::intra_threads() const { return impl_->intra; }

std::vector<float> Synthesizer::generate(const std::vector<int64_t>& phoneme_ids,
                                         const float* voice256, float speed) const {
    auto& s = *impl_->session;
    const Binding& b = impl_->binding;
    Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    Ort::AllocatorWithDefaultOptions alloc;

    // Токени: [1, N] з BOS/EOS=0 (як HolosTTS.generate); якщо граф приймає
    // фонемний рядок — токени не потрібні.
    std::vector<int64_t> tokens;
    if (b.phonemes.empty()) {
        tokens.reserve(phoneme_ids.size() + 2);
        tokens.push_back(0); // BOS
        tokens.insert(tokens.end(), phoneme_ids.begin(), phoneme_ids.end());
        tokens.push_back(0); // EOS
    }

    // Стабільні буфери для скалярних входів (CreateTensor НЕ копіює дані).
    std::vector<int64_t> len_buf;
    std::vector<float> ctl_buf;
    ctl_buf.reserve(b.controls.size());

    std::vector<Ort::Value> inputs;
    std::vector<const char*> in_names;
    auto add_named = [&](const std::string& name) { in_names.push_back(name.c_str()); };

    if (!b.tokens.empty()) {
        std::vector<int64_t> shape = {1, static_cast<int64_t>(tokens.size())};
        inputs.push_back(Ort::Value::CreateTensor<int64_t>(mem, tokens.data(), tokens.size(),
                                                           shape.data(), 2));
        add_named(b.tokens);
    }
    if (!b.lengths.empty()) {
        len_buf.push_back(static_cast<int64_t>(tokens.size()));
        std::vector<int64_t> shape = {1};
        inputs.push_back(Ort::Value::CreateTensor<int64_t>(mem, len_buf.data(), 1, shape.data(), 1));
        add_named(b.lengths);
    }
    if (!b.voice.empty()) {
        std::vector<int64_t> shape = {1, kVoiceDim};
        inputs.push_back(Ort::Value::CreateTensor<float>(mem, const_cast<float*>(voice256),
                                                         kVoiceDim, shape.data(), 2));
        add_named(b.voice);
    }
    for (const auto& cname : b.controls) {
        // керуючі (speed) — за замовч. 1.0; для speed-входу використовуємо `speed`.
        std::string low = cname;
        for (char& c : low) c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
        ctl_buf.push_back((low.find("speed") != std::string::npos) ? speed : 1.0f);
        std::vector<int64_t> shape = {1};
        inputs.push_back(Ort::Value::CreateTensor<float>(mem, &ctl_buf.back(), 1, shape.data(), 1));
        add_named(cname);
    }

    const char* out_name = "audio";
    auto outputs = s.Run(Ort::RunOptions{nullptr}, in_names.data(), inputs.data(),
                         inputs.size(), &out_name, 1);
    auto& audio = outputs[0];
    size_t count = audio.GetTensorTypeAndShapeInfo().GetElementCount();
    const float* data = audio.GetTensorMutableData<float>();
    return std::vector<float>(data, data + count);
}

} // namespace holos
