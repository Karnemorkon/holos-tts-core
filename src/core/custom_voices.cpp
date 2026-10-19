#include "custom_voices.h"

#include "pcre2_util.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace holos {

namespace {

constexpr size_t kVoiceDim = 256;

std::string read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("не вдалося відкрити " + path);
    std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (!f.good() && !f.eof()) throw std::runtime_error("помилка читання " + path);
    return s;
}

uint32_t le_u32(const char* p) {
    return uint32_t(uint8_t(p[0])) | (uint32_t(uint8_t(p[1])) << 8) |
           (uint32_t(uint8_t(p[2])) << 16) | (uint32_t(uint8_t(p[3])) << 24);
}
uint16_t le_u16(const char* p) {
    return uint16_t(uint16_t(uint8_t(p[0])) | (uint16_t(uint8_t(p[1])) << 8));
}

// Витягує перше ціле з shape у numpy-заголовку: (256,) або (256) тощо.
size_t parse_shape_n(const std::string& header) {
    const size_t spos = header.find("'shape'");
    if (spos == std::string::npos) throw std::runtime_error("npy: немає 'shape'");
    const size_t open = header.find('(', spos);
    if (open == std::string::npos) throw std::runtime_error("npy: некоректний 'shape'");
    size_t i = open + 1;
    while (i < header.size() && (header[i] == ' ' || header[i] == '(')) ++i;
    size_t n = 0;
    bool any = false;
    while (i < header.size() && header[i] >= '0' && header[i] <= '9') {
        n = n * 10 + size_t(header[i] - '0');
        any = true;
        ++i;
    }
    if (!any) throw std::runtime_error("npy: некоректний 'shape'");
    // 2-D та вище не підтримуємо (голос — вектор 256).
    while (i < header.size() && header[i] == ' ') ++i;
    if (i < header.size() && header[i] == ',') {
        ++i;
        while (i < header.size() && header[i] == ' ') ++i;
    }
    if (i >= header.size() || header[i] != ')')
        throw std::runtime_error("npy: очікується 1-D масив");
    return n;
}

} // namespace

std::vector<float> load_npy_f32(const std::string& path) {
    const std::string buf = read_file(path);
    if (buf.size() < 10 || std::memcmp(buf.data(), "\x93NUMPY", 6) != 0)
        throw std::runtime_error("npy: невірний magic");
    const uint8_t major = uint8_t(buf[6]);
    size_t off = 8;
    uint32_t header_len = 0;
    if (major == 1) {
        if (buf.size() < 10) throw std::runtime_error("npy: обрізаний заголовок");
        header_len = le_u16(buf.data() + 8);
        off = 10;
    } else if (major == 2 || major == 3) {
        if (buf.size() < 12) throw std::runtime_error("npy: обрізаний заголовок");
        header_len = le_u32(buf.data() + 8);
        off = 12;
    } else {
        throw std::runtime_error("npy: непідтримувана версія");
    }
    if (buf.size() < off + header_len)
        throw std::runtime_error("npy: обрізаний заголовок");
    const std::string header = buf.substr(off, header_len);
    if (header.find("<f4") == std::string::npos && header.find("=f4") == std::string::npos)
        throw std::runtime_error("npy: підтримується лише float32 (little-endian)");
    if (header.find("'fortran_order': True") != std::string::npos)
        throw std::runtime_error("npy: fortran_order не підтримується");

    const size_t n = parse_shape_n(header);
    const size_t data_off = off + header_len;
    if (buf.size() < data_off + n * 4)
        throw std::runtime_error("npy: обрізані дані");

    std::vector<float> out(n);
    for (size_t i = 0; i < n; ++i) {
        uint32_t bits = le_u32(buf.data() + data_off + i * 4);
        std::memcpy(&out[i], &bits, 4);
    }
    return out;
}

void save_npy_f32(const std::string& path, const std::vector<float>& values) {
    std::string header = "{'descr': '<f4', 'fortran_order': False, 'shape': (" +
                         std::to_string(values.size()) + ",), }";
    const size_t prefix = 6 /*magic*/ + 2 /*version*/ + 2 /*header len*/;
    size_t total = prefix + header.size() + 1 /*newline*/;
    while (total % 64 != 0) {
        header += ' ';
        ++total;
    }
    header += '\n';

    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) throw std::runtime_error("не вдалося записати " + path);
    f.write("\x93NUMPY", 6);
    const char ver[2] = {1, 0};
    f.write(ver, 2);
    const uint16_t hlen = uint16_t(header.size());
    char hb[2] = {char(hlen & 0xFF), char((hlen >> 8) & 0xFF)};
    f.write(hb, 2);
    f.write(header.data(), std::streamsize(header.size()));
    for (float v : values) {
        uint32_t bits;
        std::memcpy(&bits, &v, 4);
        char b[4] = {char(bits & 0xFF), char((bits >> 8) & 0xFF),
                     char((bits >> 16) & 0xFF), char((bits >> 24) & 0xFF)};
        f.write(b, 4);
    }
    f.flush();
    if (!f.good()) throw std::runtime_error("помилка запису " + path);
}

bool CustomVoices::valid_name(const std::string& name) {
    static const auto re = [] {
        return util::compile("^[\\w \\-]{1,40}$");
    }();
    pcre2_match_data* md = pcre2_match_data_create_from_pattern(re.get(), nullptr);
    const int rc = pcre2_match(re.get(),
                               reinterpret_cast<PCRE2_SPTR>(name.data()), name.size(),
                               0, 0, md, nullptr);
    PCRE2_SIZE* ov = pcre2_get_ovector_pointer(md);
    const bool ok = rc > 0 && ov[0] == 0 && ov[1] == name.size();
    pcre2_match_data_free(md);
    return ok;
}

std::string CustomVoices::path_for(const std::string& name) const {
    return (fs::path(dir_) / (name + ".npy")).string();
}

void CustomVoices::rescan_locked() const {
    if (dir_.empty()) return;
    std::error_code ec;
    if (!fs::is_directory(dir_, ec)) return;
    std::map<std::string, std::vector<float>> found;
    for (const auto& entry : fs::directory_iterator(dir_, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        const std::string fn = entry.path().filename().string();
        if (fn.size() < 5 || fn.compare(fn.size() - 4, 4, ".npy") != 0) continue;
        const std::string name = fn.substr(0, fn.size() - 4);
        auto it = map_.find(name);
        if (it != map_.end()) {
            found[name] = it->second;
            continue;
        }
        try {
            auto v = load_npy_f32(entry.path().string());
            if (v.size() == kVoiceDim) found[name] = std::move(v);
        } catch (...) {
            // пошкоджений файл — пропускаємо (як hybrid)
        }
    }
    map_.swap(found);
}

std::vector<std::string> CustomVoices::names() const {
    std::lock_guard<std::mutex> lk(mu_);
    rescan_locked();
    std::vector<std::string> out;
    out.reserve(map_.size());
    for (const auto& [name, _] : map_) out.push_back(name);
    return out; // std::map -> вже відсортовано
}

bool CustomVoices::has(const std::string& name) const {
    std::lock_guard<std::mutex> lk(mu_);
    rescan_locked();
    return map_.count(name) != 0;
}

std::vector<float> CustomVoices::get(const std::string& name) const {
    std::lock_guard<std::mutex> lk(mu_);
    rescan_locked();
    auto it = map_.find(name);
    if (it == map_.end()) throw std::out_of_range("невідомий кастомний голос: " + name);
    return it->second;
}

void CustomVoices::save(const std::string& name, const std::vector<float>& emb) {
    if (dir_.empty()) throw std::runtime_error("збереження голосів вимкнено");
    if (!valid_name(name))
        throw std::invalid_argument(
            "Некоректна назва голосу: 1-40 символів (літери/цифри/пробіл/або _-)");
    if (emb.size() != kVoiceDim)
        throw std::invalid_argument("Ембеддінг має бути розміром 256");
    std::error_code ec;
    fs::create_directories(dir_, ec);
    const std::string path = path_for(name);
    const std::string tmp = path + ".tmp";
    save_npy_f32(tmp, emb);
    fs::rename(tmp, path, ec);
    if (ec) {
        std::error_code rm;
        fs::remove(tmp, rm);
        throw std::runtime_error("не вдалося зберегти голос: " + ec.message());
    }
    std::lock_guard<std::mutex> lk(mu_);
    map_[name] = emb;
}

bool CustomVoices::remove(const std::string& name) {
    if (dir_.empty()) return false;
    std::lock_guard<std::mutex> lk(mu_);
    rescan_locked();
    auto it = map_.find(name);
    if (it == map_.end()) return false;
    std::error_code ec;
    fs::remove(path_for(name), ec);
    if (ec) throw std::runtime_error("не вдалося видалити голос: " + ec.message());
    map_.erase(it);
    return true;
}

} // namespace holos
