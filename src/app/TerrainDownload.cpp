#include "TerrainDownload.hpp"

#include "../sim/Locations.hpp"
#include "raylib.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <map>
#include <utility>

#if defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace sky {

namespace fs = std::filesystem;

bool downloadFile(const std::string& url, const std::string& path) {
#if (defined(__unix__) || defined(__APPLE__)) && !defined(__EMSCRIPTEN__)
    std::string tmp = path + ".part";
    std::vector<std::string> args = {"curl", "-sfL", "--retry", "3", "--connect-timeout", "20", "-m", "120", "-A", "SKYFALL-terrain/1.0", "-o", tmp, url};
    std::vector<char*> argv;
    for (auto& a : args) argv.push_back(a.data());
    argv.push_back(nullptr);
    pid_t pid = -1;
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 1, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
    const char* exe = access("/usr/bin/curl", X_OK) == 0 ? "/usr/bin/curl" : "/bin/curl";
    int rc = posix_spawn(&pid, exe, &fa, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&fa);
    if (rc != 0) return false;
    int st = 0;
    if (waitpid(pid, &st, 0) != pid || !WIFEXITED(st) || WEXITSTATUS(st) != 0) { std::error_code ec; fs::remove(tmp, ec); return false; }
    std::error_code ec;
    fs::rename(tmp, path, ec);
    return !ec;
#else
    (void)url; (void)path;
    return false;
#endif
}

TerrainDownloader::~TerrainDownloader() {
    cancel_ = true;
    if (worker_.joinable()) worker_.join();
}

bool TerrainDownloader::start(const std::vector<std::string>& ids, const std::string& outDir, const std::string& cacheDir) {
    if (busy_ || ids.empty()) return false;
    if (worker_.joinable()) worker_.join();
    cancel_ = false;
    busy_ = true;
    locDone_ = 0;
    locTotal_ = static_cast<int>(ids.size());
    tilesDone_ = 0;
    tilesTotal_ = 1;
    worker_ = std::thread(&TerrainDownloader::run, this, ids, outDir, cacheDir);
    return true;
}

float TerrainDownloader::progress() const {
    float per = static_cast<float>(tilesDone_) / std::max(1, tilesTotal_.load());
    return std::min(1.0f, (locDone_ + per) / std::max(1, locTotal_.load()));
}

std::string TerrainDownloader::status() const { std::lock_guard<std::mutex> l(mu_); return status_; }
std::string TerrainDownloader::current() const { std::lock_guard<std::mutex> l(mu_); return current_; }

std::vector<std::string> TerrainDownloader::takeFinished() {
    std::lock_guard<std::mutex> l(mu_);
    std::vector<std::string> f;
    f.swap(finished_);
    return f;
}

void TerrainDownloader::setStatus(const std::string& s) { std::lock_guard<std::mutex> l(mu_); status_ = s; }

void TerrainDownloader::run(std::vector<std::string> ids, std::string outDir, std::string cacheDir) {
    std::error_code ec;
    fs::create_directories(outDir, ec);
    fs::create_directories(cacheDir, ec);
    int ok = 0, failed = 0;
    std::string lastErr;
    for (const std::string& id : ids) {
        if (cancel_) break;
        const LocationDef* loc = findLocation(id);
        if (!loc || id == "desert") { ++locDone_; continue; }
        { std::lock_guard<std::mutex> l(mu_); current_ = loc->name; }
        setStatus("Downloading " + loc->name + "...");
        std::string err;
        if (fetchLocation(id, outDir, cacheDir, err)) {
            ++ok;
            std::lock_guard<std::mutex> l(mu_);
            finished_.push_back(id);
        } else {
            ++failed;
            lastErr = loc->name + ": " + err;
        }
        ++locDone_;
        tilesDone_ = 0;
    }
    { std::lock_guard<std::mutex> l(mu_); current_.clear(); }
    if (cancel_) setStatus("Download cancelled.");
    else if (failed) setStatus("Downloaded " + std::to_string(ok) + ", failed " + std::to_string(failed) + " (" + lastErr + "). Check the internet connection and try again.");
    else setStatus("Downloaded " + std::to_string(ok) + (ok == 1 ? " map." : " maps."));
    busy_ = false;
}

namespace {
constexpr double kEarthR = 6378137.0;
void mercatorPx(double lat, double lon, int zoom, double& x, double& y) {
    double n = std::pow(2.0, zoom) * 256.0;
    x = (lon + 180.0) / 360.0 * n;
    double lr = lat * M_PI / 180.0;
    y = (1.0 - std::asinh(std::tan(lr)) / M_PI) / 2.0 * n;
}
}  // namespace

bool TerrainDownloader::fetchLocation(const std::string& id, const std::string& outDir, const std::string& cacheDir, std::string& err) {
    const LocationDef* loc = findLocation(id);
    const int zoom = 12, N = 1025;
    const double extent = 32000.0, half = extent / 2.0;
    const double lat0 = loc->lat, lon0 = loc->lon;
    auto latlon = [&](double x, double z, double& lat, double& lon) {
        lat = lat0 - (z / kEarthR) * 180.0 / M_PI;
        lon = lon0 + (x / (kEarthR * std::cos(lat0 * M_PI / 180.0))) * 180.0 / M_PI;
    };
    double px0 = 1e30, px1 = -1e30, py0 = 1e30, py1 = -1e30;
    for (double x : {-half, half})
        for (double z : {-half, half}) {
            double la, lo, px, py;
            latlon(x, z, la, lo);
            mercatorPx(la, lo, zoom, px, py);
            px0 = std::min(px0, px - 2); px1 = std::max(px1, px + 2);
            py0 = std::min(py0, py - 2); py1 = std::max(py1, py + 2);
        }
    int tx0 = static_cast<int>(std::floor(px0 / 256)), tx1 = static_cast<int>(std::floor(px1 / 256));
    int ty0 = static_cast<int>(std::floor(py0 / 256)), ty1 = static_cast<int>(std::floor(py1 / 256));
    tilesTotal_ = (tx1 - tx0 + 1) * (ty1 - ty0 + 1) + 2;   // + resample and save
    tilesDone_ = 0;
    std::map<std::pair<int, int>, std::vector<float>> tiles;
    for (int ty = ty0; ty <= ty1; ++ty)
        for (int tx = tx0; tx <= tx1; ++tx) {
            if (cancel_) { err = "cancelled"; return false; }
            char name[96];
            std::snprintf(name, sizeof name, "%d_%d_%d.png", zoom, tx, ty);
            std::string file = cacheDir + "/" + name;
            if (!FileExists(file.c_str())) {
                std::string url = tileUrl;
                auto rep = [&](const std::string& k, int v) {
                    size_t p = url.find(k);
                    if (p != std::string::npos) url.replace(p, k.size(), std::to_string(v));
                };
                rep("{z}", zoom); rep("{x}", tx); rep("{y}", ty);
                if (!downloadFile(url, file)) { err = "could not download elevation tiles"; return false; }
            }
            Image img = LoadImage(file.c_str());
            if (!img.data || img.width != 256 || img.height != 256) {
                if (img.data) UnloadImage(img);
                std::error_code ec;
                fs::remove(file, ec);
                err = "bad tile data";
                return false;
            }
            ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
            const unsigned char* p = static_cast<const unsigned char*>(img.data);
            std::vector<float> e(256 * 256);
            for (int k = 0; k < 256 * 256; ++k)   // terrarium: R*256 + G + B/256 - 32768
                e[k] = static_cast<float>(p[k * 4] * 256.0 + p[k * 4 + 1] + p[k * 4 + 2] / 256.0 - 32768.0);
            UnloadImage(img);
            tiles[{tx, ty}] = std::move(e);
            ++tilesDone_;
        }
    auto elevPx = [&](int px, int py) -> float {
        int tx = static_cast<int>(std::floor(px / 256.0)), ty = static_cast<int>(std::floor(py / 256.0));
        auto it = tiles.find({tx, ty});
        if (it == tiles.end()) return 0.0f;
        int lx = std::clamp(px - tx * 256, 0, 255), ly = std::clamp(py - ty * 256, 0, 255);
        return it->second[ly * 256 + lx];
    };
    Heightmap hm;
    hm.n = N;
    hm.extent = extent;
    hm.h.resize(static_cast<size_t>(N) * N);
    double step = extent / (N - 1);
    for (int j = 0; j < N; ++j) {
        if (cancel_) { err = "cancelled"; return false; }
        double z = -half + j * step;
        for (int i = 0; i < N; ++i) {
            double x = -half + i * step, la, lo, px, py;
            latlon(x, z, la, lo);
            mercatorPx(la, lo, zoom, px, py);
            int x0 = static_cast<int>(std::floor(px - 0.5)), y0 = static_cast<int>(std::floor(py - 0.5));
            double fx = px - 0.5 - x0, fy = py - 0.5 - y0;
            double a = elevPx(x0, y0), b = elevPx(x0 + 1, y0), c = elevPx(x0, y0 + 1), d = elevPx(x0 + 1, y0 + 1);
            hm.h[static_cast<size_t>(j) * N + i] = static_cast<float>((a + (b - a) * fx) * (1 - fy) + (c + (d - c) * fx) * fy);
        }
    }
    ++tilesDone_;
    std::string out = outDir + "/" + id + ".skt";
    if (!saveHeightmap(out + ".part", hm, lat0, lon0)) { err = "cannot write " + out; return false; }
    std::error_code ec;
    fs::rename(out + ".part", out, ec);
    if (ec) { err = "cannot write " + out; return false; }
    ++tilesDone_;
    return true;
}

}  // namespace sky
