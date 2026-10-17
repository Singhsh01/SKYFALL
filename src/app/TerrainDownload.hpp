// In-game download of real-world elevation data (Settings -> World maps, or WORLD LOCATIONS).
// Same source and conversion as tools/fetch_real_terrain.py: AWS Terrain Tiles (terrarium PNG,
// zoom 12), a 32 x 32 km square around the location, resampled to 1025 x 1025 metres and saved
// as assets/terrain/<id>.skt. Tiles are fetched with the system's curl in a background thread;
// the game keeps running and shows progress.
#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace sky {

class TerrainDownloader {
public:
    ~TerrainDownloader();
    /// Queue locations (ids) for download into outDir. Ignored while a batch is running.
    bool start(const std::vector<std::string>& ids, const std::string& outDir, const std::string& cacheDir);
    bool busy() const { return busy_; }
    /// 0..1 over the whole batch.
    float progress() const;
    std::string status() const;
    std::string current() const;
    /// Ids finished since the last call (successful downloads).
    std::vector<std::string> takeFinished();
    void cancel() { cancel_ = true; }
    /// Tile URL template ({z}/{x}/{y}); file:// works (tests).
    std::string tileUrl = "https://s3.amazonaws.com/elevation-tiles-prod/terrarium/{z}/{x}/{y}.png";

private:
    void run(std::vector<std::string> ids, std::string outDir, std::string cacheDir);
    bool fetchLocation(const std::string& id, const std::string& outDir, const std::string& cacheDir, std::string& err);
    void setStatus(const std::string& s);
    std::thread worker_;
    std::atomic<bool> busy_{false}, cancel_{false};
    std::atomic<int> tilesDone_{0}, tilesTotal_{1}, locDone_{0}, locTotal_{1};
    mutable std::mutex mu_;
    std::string status_, current_;
    std::vector<std::string> finished_;
};

/// Downloads one URL to a file with the system's curl. Returns true on success.
bool downloadFile(const std::string& url, const std::string& path);

}  // namespace sky
