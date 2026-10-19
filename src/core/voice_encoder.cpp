#include "voice_encoder.h"

#include <onnxruntime_cxx_api.h>

#include <algorithm>
#include <cstdio>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace holos {

namespace {

bool file_readable(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fclose(f);
    return true;
}

} // namespace

struct VoiceEncoder::Impl {
    std::string path;
    int intra = 0;
    mutable std::once_flag once;
    mutable std::unique_ptr<Ort::Env> env;
    mutable std::unique_ptr<Ort::Session> session;
    mutable std::string in_name;
    mutable std::string out_name;

    Impl(std::string p, int threads) : path(std::move(p)), intra(threads) {}

    // Ліниве створення сесії: енкодер потрібен лише при енролменті, тож
    // не платимо ~60 МБ RSS і час старту, поки клонування не використали.
    void load() const {
        env = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "holos-voice-encoder");
        Ort::SessionOptions opts;
        const int threads =
            intra > 0 ? intra
                      : static_cast<int>(std::min<unsigned>(
                            std::thread::hardware_concurrency(), 9u));
        opts.SetIntraOpNumThreads(threads);
        opts.SetInterOpNumThreads(1);
        opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        // Пам'ять понад усе: виклики рідкісні, тож звільняємо тимчасові
        // буфери після кожного Run замість арени (мінус ~0.9 ГБ після піку).
        opts.DisableMemPattern();
        opts.DisableCpuMemArena();
        session = std::make_unique<Ort::Session>(*env, path.c_str(), opts);
        Ort::AllocatorWithDefaultOptions alloc;
        in_name = std::string(session->GetInputNameAllocated(0, alloc).get());
        out_name = std::string(session->GetOutputNameAllocated(0, alloc).get());
    }
};

VoiceEncoder::VoiceEncoder(const std::string& onnx_path, int intra_threads) {
    if (onnx_path.empty() || !file_readable(onnx_path)) return; // вимкнено
    impl_ = new Impl(onnx_path, intra_threads);
}

VoiceEncoder::~VoiceEncoder() { delete impl_; }

std::vector<float> VoiceEncoder::encode(const std::vector<float>& wave24k) const {
    if (!impl_) throw std::runtime_error("voice encoder недоступний");
    if (wave24k.empty()) throw std::runtime_error("порожнє аудіо для енкодера");
    std::call_once(impl_->once, [this] { impl_->load(); });

    Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    std::vector<int64_t> shape = {1, static_cast<int64_t>(wave24k.size())};
    auto input = Ort::Value::CreateTensor<float>(
        mem, const_cast<float*>(wave24k.data()), wave24k.size(), shape.data(),
        shape.size());

    const char* in_names[1] = {impl_->in_name.c_str()};
    const char* out_names[1] = {impl_->out_name.c_str()};
    auto outputs = impl_->session->Run(Ort::RunOptions{nullptr}, in_names, &input, 1,
                                       out_names, 1);
    const size_t count = outputs[0].GetTensorTypeAndShapeInfo().GetElementCount();
    if (count != 256)
        throw std::runtime_error("voice encoder: очікувався 256-вимірний вихід, отримано " +
                                 std::to_string(count));
    const float* data = outputs[0].GetTensorMutableData<float>();
    return std::vector<float>(data, data + count);
}

} // namespace holos
