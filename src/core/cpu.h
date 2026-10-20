#pragma once

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>

#ifdef __linux__
#include <sched.h>
#endif

namespace holos {
namespace detail {

#ifdef __linux__
inline unsigned affinity_cpus() {
    cpu_set_t set;
    if (sched_getaffinity(0, sizeof(set), &set) == 0) {
        const int n = CPU_COUNT(&set);
        if (n > 0) return static_cast<unsigned>(n);
    }
    return 0;
}

inline unsigned read_cpu_max(const std::string& dir) {
    std::ifstream f(dir + "/cpu.max");
    std::string quota;
    long long period = 0;
    if (!(f >> quota >> period) || quota == "max" || period <= 0) return 0;
    char* end = nullptr;
    const long long q = std::strtoll(quota.c_str(), &end, 10);
    if (end == quota.c_str() || q <= 0) return 0;
    return static_cast<unsigned>(std::max(1LL, q / period));
}

inline unsigned cgroup_cpu_quota() {
    const std::string root = "/sys/fs/cgroup";
    std::string rel;
    {
        std::ifstream f("/proc/self/cgroup");
        std::string line;
        while (std::getline(f, line)) {
            if (line.rfind("0::", 0) == 0) {
                rel = line.substr(3);
                break;
            }
        }
    }

    if (!rel.empty() && rel.front() == '/') {
        std::string path = rel == "/" ? root : root + rel;
        unsigned best = 0;
        while (true) {
            const unsigned q = read_cpu_max(path);
            if (q > 0 && (best == 0 || q < best)) best = q;
            if (path == root) break;
            const auto slash = path.find_last_of('/');
            if (slash == std::string::npos || slash < root.size()) break;
            path.resize(slash);
        }
        if (best > 0) return best;
    }

    if (const unsigned q = read_cpu_max(root); q > 0) return q;

    const auto read_ll = [](const char* path) -> long long {
        std::ifstream f(path);
        long long v = 0;
        return (f >> v) ? v : 0;
    };
    const long long q = read_ll("/sys/fs/cgroup/cpu/cpu.cfs_quota_us");
    const long long p = read_ll("/sys/fs/cgroup/cpu/cpu.cfs_period_us");
    if (q > 0 && p > 0) return static_cast<unsigned>(std::max(1LL, q / p));
    return 0;
}
#endif

} // namespace detail

inline int default_onnx_threads() {
    unsigned cpus = 0;
#ifdef __linux__
    cpus = detail::affinity_cpus();
#endif
    if (cpus == 0) cpus = std::thread::hardware_concurrency();
    if (cpus == 0) return 1;

    unsigned threads = std::max(1u, (cpus * 4u) / 5u);
#ifdef __linux__
    const unsigned quota = detail::cgroup_cpu_quota();
    if (quota > 0) threads = std::min(threads, quota);
#endif
    return static_cast<int>(std::max(1u, threads));
}

} // namespace holos
