#pragma once

#include <filesystem>
#include <string>

namespace holos {

#ifdef _WIN32
inline std::wstring ort_path(const std::string& utf8) {
    const auto u8 = std::u8string(reinterpret_cast<const char8_t*>(utf8.data()),
                                  utf8.size());
    return std::filesystem::path(u8).wstring();
}
#else
inline const std::string& ort_path(const std::string& utf8) { return utf8; }
#endif

}  // namespace holos
