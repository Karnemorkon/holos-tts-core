#include "voices.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <stdexcept>

namespace holos {

Voices Voices::load(const std::string& json_path) {
    std::ifstream f(json_path);
    if (!f) throw std::runtime_error("cannot open voices.json: " + json_path);
    nlohmann::json j;
    f >> j;
    if (!j.is_object()) throw std::runtime_error("voices.json: expected a JSON object");
    Voices v;
    for (auto& [name, arr] : j.items()) {
        if (!arr.is_array() || arr.size() != 256)
            throw std::runtime_error("voices.json: voice '" + name + "' must be 256 floats");
        v.map_[name] = arr.get<std::vector<float>>();
    }
    return v;
}

const std::vector<float>& Voices::get(const std::string& name) const {
    auto it = map_.find(name);
    if (it == map_.end()) throw std::out_of_range("unknown voice: " + name);
    return it->second;
}

} // namespace holos
