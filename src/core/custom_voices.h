#pragma once
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace holos {

// Numpy .npy (1-D float32, little-endian) — формат як np.save/np.load.
// Кидає std::runtime_error для невідповідного формату.
std::vector<float> load_npy_f32(const std::string& path);
void save_npy_f32(const std::string& path, const std::vector<float>& values);

// Кастомні (клоновані) голоси: <dir>/<name>.npy (float32[256]).
// Сумісно з HolosTTS-hybrid (np.load/np.save). Каталог сканується при
// кожному зверненні: .npy, покладені у volume, зʼявляються без рестарту.
class CustomVoices {
public:
    explicit CustomVoices(std::string dir) : dir_(std::move(dir)) {}
    bool enabled() const { return !dir_.empty(); }
    const std::string& dir() const { return dir_; }

    // Як hybrid: ^[\w \-]{1,40}$ (Unicode); пробіли/дефіс/підкреслення дозволені.
    static bool valid_name(const std::string& name);

    std::vector<std::string> names() const; // відсортовані (після rescan)
    bool has(const std::string& name) const;
    std::vector<float> get(const std::string& name) const; // 256 float; throws
    void save(const std::string& name, const std::vector<float>& emb);
    bool remove(const std::string& name); // false, якщо голосу не було

private:
    void rescan_locked() const;
    std::string path_for(const std::string& name) const;

    std::string dir_;
    mutable std::mutex mu_;
    mutable std::map<std::string, std::vector<float>> map_;
};

} // namespace holos
