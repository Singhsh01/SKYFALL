#include "MapStore.hpp"

#include "Locations.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace sky {

namespace fs = std::filesystem;

// ------------------------------------------------------------------------------------------
// SHA-256 (FIPS 180-4), compact and dependency-free
// ------------------------------------------------------------------------------------------
namespace {
struct Sha256 {
    std::uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::uint8_t buf[64];
    std::size_t used = 0;
    std::uint64_t bits = 0;
    static std::uint32_t rotr(std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
    void block(const std::uint8_t* p) {
        static const std::uint32_t k[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be,
            0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa,
            0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85,
            0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
            0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f,
            0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
        std::uint32_t w[64];
        for (int i = 0; i < 16; ++i) w[i] = (std::uint32_t(p[4 * i]) << 24) | (std::uint32_t(p[4 * i + 1]) << 16) | (std::uint32_t(p[4 * i + 2]) << 8) | p[4 * i + 3];
        for (int i = 16; i < 64; ++i) {
            std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            std::uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            std::uint32_t ch = (e & f) ^ (~e & g);
            std::uint32_t t1 = hh + S1 + ch + k[i] + w[i];
            std::uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            std::uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
            std::uint32_t t2 = S0 + mj;
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    void update(const std::uint8_t* p, std::size_t n) {
        bits += static_cast<std::uint64_t>(n) * 8;
        while (n > 0) {
            std::size_t take = std::min(n, sizeof(buf) - used);
            std::memcpy(buf + used, p, take);
            used += take; p += take; n -= take;
            if (used == 64) { block(buf); used = 0; }
        }
    }
    std::string hex() {
        std::uint64_t total = bits;
        std::uint8_t pad = 0x80;
        update(&pad, 1);
        std::uint8_t zero = 0;
        while (used != 56) update(&zero, 1);
        std::uint8_t len[8];
        for (int i = 0; i < 8; ++i) len[i] = static_cast<std::uint8_t>(total >> (56 - 8 * i));
        update(len, 8);
        static const char* d = "0123456789abcdef";
        std::string s;
        for (std::uint32_t v : h)
            for (int i = 28; i >= 0; i -= 4) s += d[(v >> i) & 15];
        return s;
    }
};
}  // namespace

std::string sha256Hex(const std::uint8_t* data, std::size_t n) {
    Sha256 s;
    s.update(data, n);
    return s.hex();
}

std::string sha256File(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return "";
    Sha256 s;
    std::vector<char> b(1 << 16);
    while (f) {
        f.read(b.data(), static_cast<std::streamsize>(b.size()));
        std::streamsize got = f.gcount();
        if (got > 0) s.update(reinterpret_cast<const std::uint8_t*>(b.data()), static_cast<std::size_t>(got));
    }
    return s.hex();
}

std::string bundledMapChecksum(const std::string& id) {
    // assets/terrain/*.skt as shipped (tools/fetch_real_terrain.py output); see SHA256SUMS
    static const std::map<std::string, std::string> k = {
        {"aconcagua", "ed8e42adf1e1c16a3ce715fbba8e337c3c131ca0bd4fac241db60890107be0d8"},
        {"erg_chebbi", "734cfa4b82e43f5ce75a9a2b9d8fe37372f7c16e4720090da6f81058cf9c2438"},
        {"everest", "099dde01ae082b97dd57c3fdb472a5918d45d86936413a038995d41f2452a0d5"},
        {"fuji", "5440d831cf245315877a63ead083667d2cc7d57323ee289307f5f84fde796c20"},
        {"geiranger", "8e5d0c18f28bed4c7350db81715721c6fd188a917d28f100de52839056ab1dee"},
        {"glencoe", "318dfd168071b3c7eea291f53a0bd5f4b224b04b16b3d865b6fd86ae4f5f2eca"},
        {"grand_canyon", "f8a4e26d0914c4a2c7d248b82f7c69c7fbdb6bf39cabed4d51227a7e0272a3f8"},
        {"guilin", "d2f879ebaf4911af14d10050349067f35f7ef737998ca01d07e6350b148865ab"},
        {"matterhorn", "7068a8dbe64097e4a31f9b42bb75aa4c93d1bcc83e21b1eedcd3d324d8ac7c8e"},
        {"musandam", "84a198bee7169d45a774f259d68b669e0aa225f8dff3102602ad35f6f6e5861a"},
        {"rio", "3e95b9de1ec7b01b5a1909933af56e84b2f6effd41f43d175a7fe097a811e717"},
        {"torres_paine", "0f90e1ba580e08e5ed16e788fd492bf54ed8fff498425947e7b93cbfdc90eed5"},
        {"wadi_rum", "ba15529a116a3e0ab1643b69f0843347ad121e0e29c1db428b553f6802932c0f"},
    };
    auto it = k.find(id);
    return it == k.end() ? "" : it->second;
}

bool validMapFile(const std::string& path, std::string& error) {
    Heightmap hm;
    if (!loadHeightmap(path, hm, error)) return false;
    std::error_code ec;
    auto bytes = fs::file_size(path, ec);
    if (ec || bytes != 32 + static_cast<std::uintmax_t>(hm.n) * hm.n * 2) { error = "unexpected file size"; return false; }
    float mn = 1e9f, mx = -1e9f;
    for (float v : hm.h) { mn = std::min(mn, v); mx = std::max(mx, v); }
    if (mn < -12000.0f || mx > 9000.0f || mx - mn < 1.0f) { error = "implausible heights"; return false; }
    return true;
}

// ------------------------------------------------------------------------------------------
// The store
// ------------------------------------------------------------------------------------------
std::string MapStore::defaultDir() {
#if defined(__EMSCRIPTEN__)
    return "/maps";
#else
    if (const char* o = std::getenv("SKYFALL_MAPS_DIR")) return o;
    fs::path d;
#if defined(_WIN32)
    if (const char* a = std::getenv("LOCALAPPDATA")) d = fs::path(a) / "SKYFALL";
#elif defined(__APPLE__)
    if (const char* h = std::getenv("HOME")) d = fs::path(h) / "Library" / "Application Support" / "SKYFALL";
#else
    if (const char* x = std::getenv("XDG_DATA_HOME")) d = fs::path(x) / "skyfall";
    else if (const char* h = std::getenv("HOME")) d = fs::path(h) / ".local" / "share" / "skyfall";
#endif
    if (d.empty()) d = ".skyfall";
    return (d / "maps").string();
#endif
}

MapStore::MapStore(std::string dir) : dir_(std::move(dir)) { reload(); }

std::string MapStore::pathFor(const std::string& id) const { return (fs::path(dir_) / (id + ".skt")).string(); }

void MapStore::reload() {
    entries_.clear();
    std::ifstream f((fs::path(dir_) / "manifest.txt").string());
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        std::string id;
        Entry e;
        if (ss >> id >> e.sha256 >> e.bytes) {
            ss >> e.source;
            if (e.sha256.size() == 64) entries_[id] = e;
        }
    }
}

void MapStore::save() const {
    std::error_code ec;
    fs::create_directories(dir_, ec);
    std::string tmp = (fs::path(dir_) / "manifest.txt.tmp").string();
    {
        std::ofstream f(tmp, std::ios::trunc);
        f << "# SKYFALL world maps: id sha256 bytes source\n";
        for (const auto& [id, e] : entries_) f << id << ' ' << e.sha256 << ' ' << e.bytes << ' ' << (e.source.empty() ? "-" : e.source) << '\n';
    }
    fs::rename(tmp, fs::path(dir_) / "manifest.txt", ec);
}

MapStore::State MapStore::check(const std::string& id, bool deep) const {
    auto it = entries_.find(id);
    std::string p = pathFor(id);
    std::error_code ec;
    if (it == entries_.end() || !fs::exists(p, ec)) return State::Missing;
    if (fs::file_size(p, ec) != it->second.bytes || ec) return State::Corrupt;
    if (deep && sha256File(p) != it->second.sha256) return State::Corrupt;
    return State::Valid;
}

bool MapStore::install(const std::string& id, const std::string& srcFile, const std::string& source, std::string& error, bool* verified) {
    if (verified) *verified = false;
    if (!validMapFile(srcFile, error)) return false;
    std::string sum = sha256File(srcFile);
    if (sum.empty()) { error = "cannot read the file"; return false; }
    std::string known = bundledMapChecksum(id);
    if (verified) *verified = !known.empty() && known == sum;
    std::error_code ec;
    fs::create_directories(dir_, ec);
    std::string dst = pathFor(id), tmp = dst + ".part";
    if (fs::absolute(srcFile, ec) != fs::absolute(dst, ec)) {
        fs::copy_file(srcFile, tmp, fs::copy_options::overwrite_existing, ec);
        if (ec) { error = "cannot write " + tmp; return false; }
        // the copy must be identical (disk full, interrupted write)
        if (sha256File(tmp) != sum) { fs::remove(tmp, ec); error = "copy failed verification"; return false; }
        fs::rename(tmp, dst, ec);
        if (ec) { fs::remove(tmp, ec); error = "cannot replace " + dst; return false; }
    }
    Entry e;
    e.sha256 = sum;
    e.bytes = fs::file_size(dst, ec);
    e.source = source;
    entries_[id] = e;
    save();
    return true;
}

void MapStore::remove(const std::string& id) {
    std::error_code ec;
    fs::remove(pathFor(id), ec);
    entries_.erase(id);
    save();
}

}  // namespace sky
