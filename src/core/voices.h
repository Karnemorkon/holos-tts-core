#pragma once
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace holos {

// Пресети голосів: {name -> float32[256]} (voices.json, експорт із voices.pt).
class Voices {
public:
    Voices() = default;
    // Завантаження з voices.json (об'єкт: name -> масив 256 float).
    static Voices load(const std::string& json_path);
    // Побудова з готової мапи (наприклад, voices.npz бандла holos-onnx).
    static Voices from_map(std::map<std::string, std::vector<float>> map);

    bool has(const std::string& name) const { return map_.count(name) != 0; }
    size_t size() const { return map_.size(); }
    // Відсортовані імена голосів (для /api/voices).
    std::vector<std::string> names() const;
    // 256-float ембеддінг голосу; кидає std::out_of_range для невідомого.
    const std::vector<float>& get(const std::string& name) const;

private:
    std::unordered_map<std::string, std::vector<float>> map_;
};

} // namespace holos
