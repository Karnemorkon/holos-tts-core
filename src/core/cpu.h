#pragma once

#include <algorithm>
#include <thread>

namespace holos {

inline int default_onnx_threads() {
    const unsigned hw = std::thread::hardware_concurrency();
    if (hw == 0) return 1;
    return static_cast<int>(std::max(1u, (hw * 4u) / 5u));
}

} // namespace holos
