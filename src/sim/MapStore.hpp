// Persistent store of world maps (real-terrain elevation files, assets/terrain/*.skt format),
// kept outside the source and build folders so they survive rebuilds, app updates and restarts:
//   macOS  ~/Library/Application Support/SKYFALL/maps
//   Linux  $XDG_DATA_HOME/skyfall/maps  (~/.local/share/skyfall/maps)
//   web    /maps, an IndexedDB-backed folder (persists in the browser)
// Every installed map is listed in maps/manifest.txt with its SHA-256 and size; "verify" re-hashes
// the file against that record, so a damaged map is detected and can be repaired (re-installed
// from the copy bundled with the game, or downloaded again). Maps are installed one by one, so a
// newly added location is fetched without touching the ones already there.
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace sky {

/// SHA-256 of a byte range / a file, lower-case hex ("" if the file cannot be read).
std::string sha256Hex(const std::uint8_t* data, std::size_t n);
std::string sha256File(const std::string& path);

/// SHA-256 of the map files shipped with this version of the game ("" if unknown).
std::string bundledMapChecksum(const std::string& id);

class MapStore {
public:
    enum class State { Missing, Valid, Corrupt };
    struct Entry { std::string sha256; std::uintmax_t bytes = 0; std::string source; };

    explicit MapStore(std::string dir = defaultDir());
    static std::string defaultDir();
    const std::string& dir() const { return dir_; }
    std::string pathFor(const std::string& id) const;

    /// Quick check (file present, size as recorded) or, with `deep`, the SHA-256 as well.
    State check(const std::string& id, bool deep = false) const;
    bool installed(const std::string& id) const { return check(id) == State::Valid; }
    /// Validates `srcFile` as a map (header, size, plausible heights), copies it in atomically
    /// and records its checksum. `source` is noted in the manifest ("bundled", "download").
    /// `verifiedAgainstBundle` (out) tells whether the file matches the checksum shipped with
    /// the game.
    bool install(const std::string& id, const std::string& srcFile, const std::string& source, std::string& error,
                 bool* verifiedAgainstBundle = nullptr);
    void remove(const std::string& id);
    const std::map<std::string, Entry>& entries() const { return entries_; }
    void reload();

private:
    void save() const;
    std::string dir_;
    std::map<std::string, Entry> entries_;
};

/// True if the file is a readable map (header, sample count, heights in a plausible range).
bool validMapFile(const std::string& path, std::string& error);

}  // namespace sky
