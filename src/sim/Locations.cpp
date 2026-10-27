#include "Locations.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>

namespace sky {

const std::vector<LocationDef>& locations() {
    static const std::vector<LocationDef> k = {
        {"desert", "Skyfall Desert", "Procedural", 0, 0, Biome::Desert, 1650, 520, 1050,
         "The original eroded desert range with a coast, airbase and town. Home of the campaign."},
        {"everest", "Everest & Khumbu", "Nepal / Himalaya", 27.95, 86.87, Biome::Alpine, 5300, 4000, 4600,
         "The roof of the world: Everest, Lhotse and the Khumbu glacier. Thin air - mind your thrust."},
        {"matterhorn", "Matterhorn & Zermatt", "Switzerland / Alps", 45.99, 7.72, Biome::Alpine, 3000, 2100, 2600,
         "Knife-edge ridges, glaciers and the Zermatt valley."},
        {"geiranger", "Geirangerfjord", "Norway / Europe", 62.10, 7.15, Biome::Temperate, 1650, 950, 1250,
         "Deep sea fjord walls - perfect for low-level flying."},
        {"glencoe", "Glen Coe", "Scotland / Europe", 56.66, -5.05, Biome::Temperate, 1150, 550, 900,
         "Green glens, lochs and the Three Sisters."},
        {"erg_chebbi", "Erg Chebbi", "Morocco / Sahara", 31.13, -4.00, Biome::Desert, 99999, 300, 99999,
         "Sahara sand sea: towering dunes on the edge of the desert plain."},
        {"wadi_rum", "Wadi Rum", "Jordan / Middle East", 29.57, 35.42, Biome::Desert, 99999, 400, 99999,
         "Sandstone mountains rising from red desert valleys."},
        {"musandam", "Musandam Fjords", "Oman / Strait of Hormuz", 26.20, 56.25, Biome::Desert, 99999, 200, 99999,
         "Bare limestone mountains plunging into the sea."},
        {"grand_canyon", "Grand Canyon", "Arizona / USA", 36.10, -112.11, Biome::Desert, 99999, 2400, 99999,
         "Two kilometres deep: fly the canyon below the rim."},
        {"aconcagua", "Aconcagua", "Argentina / Andes", -32.65, -70.01, Biome::Desert, 4800, 2600, 3600,
         "The highest peak outside Asia above the dry Andean valleys."},
        {"torres_paine", "Torres del Paine", "Chile / Patagonia", -50.97, -73.00, Biome::Alpine, 1500, 900, 1300,
         "Granite towers, glaciers and turquoise lakes."},
        {"rio", "Rio de Janeiro", "Brazil / South America", -22.95, -43.21, Biome::Tropical, 99999, 900, 99999,
         "Sugarloaf, Corcovado and the Tijuca forest over Guanabara Bay."},
        {"guilin", "Guilin Karst", "China / Asia", 24.78, 110.45, Biome::Tropical, 99999, 1200, 99999,
         "Hundreds of limestone towers along the Li River."},
        {"fuji", "Mount Fuji", "Japan / Asia", 35.36, 138.73, Biome::Temperate, 3200, 2500, 2700,
         "The perfect volcanic cone above forests and lakes."},
        // ---- India ----
        {"ladakh", "Leh & the Indus Valley", "India / Ladakh", 34.13, 77.58, Biome::HighDesert, 5600, 3650, 4900,
         "High-altitude desert: bare scree mountains, the Indus and green oases at 3,500 m. Thin air.", 3.0, -1e9, 1, false},
        {"nubra", "Nubra Valley", "India / Ladakh", 34.58, 77.56, Biome::HighDesert, 5700, 3300, 4900,
         "Sand dunes and a braided river between 6,000 m ridges, Diskit and Hunder on the valley floor.", 3.0, -1e9, 0, false},
        {"kashmir", "Kashmir Valley", "India / Jammu & Kashmir", 34.08, 74.85, Biome::Temperate, 3900, 3300, 3600,
         "Srinagar, the Jhelum and the Dal lake shore in a wide valley ringed by the Pir Panjal and Zabarwan.", 2.0, -1e9, 2},
        {"gangotri", "Gangotri & Garhwal Himalaya", "India / Uttarakhand", 30.90, 79.05, Biome::Alpine, 4600, 3800, 4300,
         "Glaciers, the Bhagirathi peaks and deep gorges: high Himalayan flying.", 1.5, -1e9, 0, false},
        {"himachal", "Kullu & Manali", "India / Himachal Pradesh", 32.20, 77.17, Biome::Alpine, 4300, 3500, 3900,
         "The Beas valley: deodar forests, orchards, hillside towns and snow peaks above.", 2.0, -1e9, 1},
        {"uttarakhand", "Tehri & the Bhagirathi", "India / Uttarakhand", 30.38, 78.45, Biome::Temperate, 99999, 2600, 2900,
         "The Bhagirathi valley around Tehri: river gorges, terraced slopes and hillside towns in the Garhwal hills.", 2.0, -1e9, 1},
        {"tawang", "Tawang", "India / Arunachal Pradesh", 27.59, 91.87, Biome::Temperate, 4600, 4000, 4300,
         "Forested ridges, deep valleys and a monastery town under snow peaks.", 2.0, -1e9, 1},
        {"sikkim", "Gangtok & East Sikkim", "India / Sikkim", 27.33, 88.62, Biome::Temperate, 4800, 3900, 4300,
         "Gangtok on its ridge, the Teesta gorge and dense subtropical forest.", 2.0, -1e9, 2},
        {"assam", "Brahmaputra & Guwahati", "India / Assam", 26.17, 91.72, Biome::Tropical, 99999, 1500, 99999,
         "The mighty Brahmaputra and its plains around Guwahati, green hills to the south.", 3.0, -1e9, 2},
        {"kochi", "Kochi & the Backwaters", "India / Kerala", 9.95, 76.29, Biome::Tropical, 99999, 2000, 99999,
         "The port city between the Arabian Sea and the Kerala backwaters, palms everywhere.", 0.0, 0.8, 2},
        {"chennai", "Chennai Coast", "India / Tamil Nadu", 13.05, 80.25, Biome::Tropical, 99999, 2000, 99999,
         "A large coastal city on the Bay of Bengal: beaches, port and dense urban grid.", 0.0, -1e9, 3},
        {"bengaluru", "Bengaluru", "India / Karnataka", 12.97, 77.59, Biome::Tropical, 99999, 2000, 99999,
         "A high-rise technology city on the Deccan plateau at 900 m, lakes and parks.", 0.0, -1e9, 3},
        {"vizag", "Visakhapatnam", "India / Andhra Pradesh", 17.72, 83.28, Biome::Tropical, 99999, 2000, 99999,
         "A coastal port city between the Eastern Ghats hills and the Bay of Bengal.", 0.0, -1e9, 2},
        {"western_ghats", "Munnar & the Western Ghats", "India / Kerala", 10.09, 77.06, Biome::Tropical, 99999, 2300, 2450,
         "Tea estates, shola forest and steep green ridges up to 2,600 m.", 1.5, -1e9, 1},
        {"mumbai", "Mumbai", "India / Maharashtra", 19.02, 72.86, Biome::Tropical, 99999, 2000, 99999,
         "A dense skyline on the Arabian Sea: Marine Drive, the harbour and the sea links.", 0.0, -1e9, 3},
    };
    return k;
}

const LocationDef* findLocation(const std::string& id) {
    for (const auto& l : locations()) if (l.id == id) return &l;
    return nullptr;
}

TerrainInfo terrainInfoFor(const LocationDef& loc) {
    TerrainInfo t;
    t.id = loc.id;
    t.name = loc.name;
    t.region = loc.region;
    t.biome = loc.biome;
    t.snowLine = loc.snowLine;
    t.treeLine = loc.treeLine;
    t.highRock = loc.highRock;
    t.real = loc.id != "desert";
    t.riverDepth = loc.riverDepth;
    t.lakeLevel = loc.lakeLevel;
    t.urban = loc.urban;
    if (t.real) t.attribution = "Elevation: AWS Terrain Tiles (Mapzen terrarium; SRTM, GMTED2010, ETOPO1, NED and others)";
    return t;
}

namespace {
template <class T> void put(std::ofstream& f, T v) { f.write(reinterpret_cast<const char*>(&v), sizeof v); }
template <class T> bool get(std::ifstream& f, T& v) { return static_cast<bool>(f.read(reinterpret_cast<char*>(&v), sizeof v)); }
}  // namespace

bool loadHeightmap(const std::string& path, Heightmap& out, std::string& error) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { error = "not downloaded"; return false; }
    char magic[4];
    int32_t n = 0, reserved = 0;
    float extent = 0, mn = 0, mx = 0, lat = 0, lon = 0;
    if (!f.read(magic, 4) || std::memcmp(magic, "SKT1", 4) != 0) { error = "not a SKYFALL terrain file"; return false; }
    if (!get(f, n) || !get(f, extent) || !get(f, mn) || !get(f, mx) || !get(f, lat) || !get(f, lon) || !get(f, reserved)) {
        error = "truncated header";
        return false;
    }
    if (n < 64 || n > 8193 || extent < 1000 || extent > 200000) { error = "bad dimensions"; return false; }
    std::vector<int16_t> raw(static_cast<size_t>(n) * n);
    if (!f.read(reinterpret_cast<char*>(raw.data()), static_cast<std::streamsize>(raw.size() * sizeof(int16_t)))) {
        error = "truncated data";
        return false;
    }
    out.n = n;
    out.extent = extent;
    out.h.resize(raw.size());
    for (size_t k = 0; k < raw.size(); ++k) out.h[k] = static_cast<float>(raw[k]);
    return true;
}

bool saveHeightmap(const std::string& path, const Heightmap& hm, double lat, double lon) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write("SKT1", 4);
    float mn = 1e9f, mx = -1e9f;
    for (float v : hm.h) { mn = std::min(mn, v); mx = std::max(mx, v); }
    put<int32_t>(f, hm.n);
    put<float>(f, static_cast<float>(hm.extent));
    put<float>(f, mn);
    put<float>(f, mx);
    put<float>(f, static_cast<float>(lat));
    put<float>(f, static_cast<float>(lon));
    put<int32_t>(f, 0);
    for (float v : hm.h) put<int16_t>(f, static_cast<int16_t>(std::lround(std::clamp(v, -32000.0f, 32000.0f))));
    return static_cast<bool>(f);
}

}  // namespace sky
