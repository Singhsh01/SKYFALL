#include "WorldMeshes.hpp"

#include "MeshBuilder.hpp"

#include <algorithm>
#include <cmath>
#include "sim/Parallel.hpp"

namespace sky {

namespace {

inline unsigned char u8(double v) { return static_cast<unsigned char>(std::clamp(v, 0.0, 1.0) * 255.0 + 0.5); }

// Periodic value noise for tiling textures.
double periodicNoise(double x, double y, int period, unsigned seed) {
    auto h = [&](int ix, int iy) {
        ix = ((ix % period) + period) % period;
        iy = ((iy % period) + period) % period;
        unsigned v = static_cast<unsigned>(ix) * 374761393u + static_cast<unsigned>(iy) * 668265263u + seed * 2246822519u;
        v = (v ^ (v >> 13)) * 1274126177u;
        return ((v ^ (v >> 16)) & 0xFFFF) / 65535.0;
    };
    int ix = static_cast<int>(std::floor(x)), iy = static_cast<int>(std::floor(y));
    double fx = x - ix, fy = y - iy;
    fx = fx * fx * (3 - 2 * fx);
    fy = fy * fy * (3 - 2 * fy);
    double a = h(ix, iy), b = h(ix + 1, iy), c = h(ix, iy + 1), d = h(ix + 1, iy + 1);
    return (a + (b - a) * fx) * (1 - fy) + (c + (d - c) * fx) * fy;
}
double periodicFbm(double u, double v, int basePeriod, int octaves, unsigned seed) {
    double sum = 0, amp = 0.5, norm = 0;
    int period = basePeriod;
    for (int o = 0; o < octaves; ++o) {
        sum += amp * periodicNoise(u * period, v * period, period, seed + o * 7u);
        norm += amp;
        amp *= 0.5;
        period *= 2;
    }
    return sum / norm;
}

Texture2D makeNoiseTexture() {
    const int N = 256;
    Image img = GenImageColor(N, N, BLACK);
    Color* px = static_cast<Color*>(img.data);
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x) {
            double u = double(x) / N, v = double(y) / N;
            double r = periodicFbm(u, v, 4, 6, 1u);
            double g = periodicFbm(u, v, 8, 5, 2u);
            double b = periodicFbm(u, v, 16, 4, 3u);
            double a = periodicFbm(u, v, 32, 3, 4u);
            // stretch contrast so thresholds in shaders behave
            auto s = [](double t) { return std::clamp((t - 0.5) * 1.7 + 0.5, 0.0, 1.0); };
            px[y * N + x] = {u8(s(r)), u8(s(g)), u8(s(b)), u8(s(a))};
        }
    Texture2D t = LoadTextureFromImage(img);
    UnloadImage(img);
    GenTextureMipmaps(&t);
    SetTextureFilter(t, TEXTURE_FILTER_TRILINEAR);
    SetTextureWrap(t, TEXTURE_WRAP_REPEAT);
    return t;
}

// Pre-differentiated green channel of the noise texture: R/G = d/du, d/dv (scaled by 6 around
// 0.5). One fetch replaces the four the terrain shader needed per detail octave.
Texture2D makeDetailGradTexture() {
    const int N = 256;
    std::vector<double> g(N * N);
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x) {
            double u = double(x) / N, v = double(y) / N;
            double t = periodicFbm(u, v, 8, 5, 2u);
            g[y * N + x] = std::clamp((t - 0.5) * 1.7 + 0.5, 0.0, 1.0);
        }
    Image img = GenImageColor(N, N, BLACK);
    Color* px = static_cast<Color*>(img.data);
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x) {
            double dx = g[y * N + (x + 1) % N] - g[y * N + (x + N - 1) % N];
            double dy = g[((y + 1) % N) * N + x] - g[((y + N - 1) % N) * N + x];
            px[y * N + x] = {u8(std::clamp(0.5 + dx * 6.0, 0.0, 1.0)), u8(std::clamp(0.5 + dy * 6.0, 0.0, 1.0)), 128, 255};
        }
    Texture2D t = LoadTextureFromImage(img);
    UnloadImage(img);
    GenTextureMipmaps(&t);
    SetTextureFilter(t, TEXTURE_FILTER_TRILINEAR);
    SetTextureWrap(t, TEXTURE_WRAP_REPEAT);
    return t;
}

Texture2D makeSprite(int kind) {
    const int N = 128;
    Image img = GenImageColor(N, N, BLANK);
    Color* px = static_cast<Color*>(img.data);
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x) {
            double dx = (x + 0.5) / N * 2 - 1, dy = (y + 0.5) / N * 2 - 1;
            double r = std::sqrt(dx * dx + dy * dy);
            double a = 0;
            if (kind == 0) {  // puff: soft, lumpy
                double n = periodicFbm(double(x) / N, double(y) / N, 4, 4, 9u);
                double edge = 1.0 - smoothstep(0.35 + 0.35 * n, 1.0, r);
                a = std::clamp(edge * (0.55 + 0.6 * n), 0.0, 1.0);
            } else if (kind == 1) {  // glow
                a = std::exp(-r * r * 5.0) + 0.5 * std::exp(-r * r * 40.0);
                a = std::clamp(a, 0.0, 1.0) * (r < 1.0);
            } else {  // spark / hard dot
                a = std::clamp(1.0 - smoothstep(0.15, 0.6, r), 0.0, 1.0);
            }
            px[y * N + x] = {255, 255, 255, u8(a)};
        }
    Texture2D t = LoadTextureFromImage(img);
    UnloadImage(img);
    GenTextureMipmaps(&t);
    SetTextureFilter(t, TEXTURE_FILTER_TRILINEAR);
    return t;
}

/// Cumulus sprite: several overlapping lobes with eroded edges, a flat base and baked
/// top-lit shading in RGB (drawn upright, never rotated).
Image makeCloudSprite(unsigned seed) {
    const int N = 256;
    Image img = GenImageColor(N, N, BLANK);
    Color* px = static_cast<Color*>(img.data);
    Rng rng(seed);
    struct Lobe { double x, y, r; };
    std::vector<Lobe> lobes;
    for (int i = 0; i < 16; ++i) {
        double x = rng.range(0.22, 0.78);
        double r = rng.range(0.10, 0.22) * (1.15 - std::fabs(x - 0.5));
        double y = 0.70 - r * rng.range(0.55, 1.15) - (0.5 - std::fabs(x - 0.5)) * rng.range(0.1, 0.35);
        lobes.push_back({x, y, r});
    }
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x) {
            double u = (x + 0.5) / N, v = (y + 0.5) / N;
            double d = 0;
            double topNormal = 0;
            for (const Lobe& l : lobes) {
                double dx = u - l.x, dy = v - l.y;
                double q = 1.0 - std::sqrt(dx * dx + dy * dy) / l.r;
                if (q > d) { d = q; topNormal = -dy / l.r; }
            }
            double n = periodicFbm(u, v, 8, 5, seed + 3);
            double dens = d + (n - 0.5) * 0.35;
            double a = smoothstep(0.02, 0.22, dens);
            a *= 1.0 - smoothstep(0.66, 0.74, v + (n - 0.5) * 0.03);   // flat cloud base
            double lit = 0.58 + 0.30 * std::clamp(topNormal, -0.2, 1.0) + 0.22 * (1.0 - v) + (n - 0.5) * 0.18;
            lit = std::clamp(lit, 0.35, 1.0);
            unsigned char c = u8(lit);
            px[y * N + x] = {c, c, u8(std::min(1.0, lit * 1.03)), u8(a)};
        }
    return img;
}

Color surfaceTint(const Terrain& t, double x, double z, double h, double forest, double town) {
    // vertex mask colour for the terrain shader (r scrub, g town, b dry river bed)
    return {u8(forest), u8(town), u8(0.0), 255};
}

const TerrainInfo* g_info = nullptr;   // terrain being built (biome, tree line)
const std::vector<unsigned char>* g_cover = nullptr;   // land-cover map being built (RGBA, kCoverN^2)
constexpr int kCoverN = 1024;

/// Land cover at a world position from the map (bilinear): r forest, g grass/meadow, b farmland, a rock.
Vector4 coverAt(double x, double z) {
    if (!g_cover) return {0, 0, 0, 0};
    double gx = (x / (2.0 * Terrain::kHalfSize) + 0.5) * kCoverN - 0.5, gz = (z / (2.0 * Terrain::kHalfSize) + 0.5) * kCoverN - 0.5;
    if (gx < 0 || gz < 0 || gx >= kCoverN - 1 || gz >= kCoverN - 1) return {0, 0, 0, 0};
    int i = static_cast<int>(gx), j = static_cast<int>(gz);
    double u = gx - i, v = gz - j;
    float out[4];
    for (int c = 0; c < 4; ++c) {
        auto P = [&](int a, int b) { return (*g_cover)[(static_cast<size_t>(b) * kCoverN + a) * 4 + c] / 255.0; };
        out[c] = static_cast<float>((P(i, j) * (1 - u) + P(i + 1, j) * u) * (1 - v) + (P(i, j + 1) * (1 - u) + P(i + 1, j + 1) * u) * v);
    }
    return {out[0], out[1], out[2], out[3]};
}

/// Where forests, meadows, farmland and bare rock are, from height, slope, hollows and climate.
/// Green climates: forest on the valley sides and lower slopes below the tree line (with clearings),
/// meadows and patchwork farmland on flat low valley floors, alpine grass above the trees, bare rock
/// on cliffs. Desert: golden grassland on the flats, scattered junipers on the foothills, red rock
/// on every steep face.
void computeLandCover(const Terrain& t, std::vector<unsigned char>& out) {
    const int N = kCoverN;
    const double cell = 2.0 * Terrain::kHalfSize / N;
    const TerrainInfo& info = t.info();
    const bool desert = info.biome == Biome::Desert;
    std::vector<float> H(static_cast<size_t>(N) * N);
    parallelRows(N, [&](int j) {
        for (int i = 0; i < N; ++i)
            H[static_cast<size_t>(j) * N + i] = static_cast<float>(t.height(-Terrain::kHalfSize + (i + 0.5) * cell, -Terrain::kHalfSize + (j + 0.5) * cell));
    });
    auto Hc = [&](int i, int j) { i = std::clamp(i, 0, N - 1); j = std::clamp(j, 0, N - 1); return double(H[static_cast<size_t>(j) * N + i]); };
    out.assign(static_cast<size_t>(N) * N * 4, 0);
    parallelRows(N, [&](int j) {
        for (int i = 0; i < N; ++i) {
            double x = -Terrain::kHalfSize + (i + 0.5) * cell, z = -Terrain::kHalfSize + (j + 0.5) * cell;
            double h = Hc(i, j);
            Vec3 n = normalize(Vec3{Hc(i - 1, j) - Hc(i + 1, j), 2 * cell, Hc(i, j - 1) - Hc(i, j + 1)});
            double slope = 1.0 - n.y;                                      // 0 flat .. ~0.5 cliff
            double around = (Hc(i - 4, j) + Hc(i + 4, j) + Hc(i, j - 4) + Hc(i, j + 4)) * 0.25;
            double hollow = std::clamp((around - h) / 25.0, -1.0, 1.0);   // + in valleys, - on ridges
            double big = fbm2(x / 2600.0, z / 2600.0, 4, 401u), mid = fbm2(x / 650.0, z / 650.0, 3, 402u);
            double fr = 0, gr = 0, fa = 0, rk = 0;
            if (h < 0.5) { gr = 1.0; /* sea floor or shore: grass where the water line leaves it dry */ }
            else if (desert) {
                rk = smoothstep(0.16, 0.30, slope + (mid - 0.5) * 0.08);
                double flat = 1.0 - smoothstep(0.04, 0.12, slope);
                gr = flat * smoothstep(0.30, 0.55, big * 0.7 + mid * 0.3) * (1.0 - rk) * (1.0 - smoothstep(info.highRock, info.highRock + 400, h));
                // junipers / pinyon on the foothills and the talus below cliffs
                fr = smoothstep(0.04, 0.12, slope) * (1.0 - rk) * smoothstep(0.45, 0.62, mid) * 0.55 * (1.0 - smoothstep(info.treeLine + 600, info.treeLine + 1200, h));
            } else {
                double snowy = smoothstep(info.snowLine - 100, info.snowLine + 200, h);
                rk = std::clamp(smoothstep(0.25, 0.44, slope + (mid - 0.5) * 0.12) + smoothstep(info.highRock, info.highRock + 300, h) * (0.4 + slope), 0.0, 1.0);
                double belowTrees = 1.0 - smoothstep(info.treeLine - 120, info.treeLine + 80, h + (mid - 0.5) * 140);
                // forests: dense on slopes and valley sides, broken by clearings
                double clearing = smoothstep(0.30, 0.42, big * 0.65 + mid * 0.35);
                fr = belowTrees * clearing * (1.0 - rk) * (0.55 + 0.45 * smoothstep(0.02, 0.12, slope));
                // patchwork farmland on flat, low valley floors (and meadows around it)
                double lowFlat = (1.0 - smoothstep(0.015, 0.06, slope)) * (1.0 - smoothstep(info.treeLine * 0.55, info.treeLine * 0.85, h)) *
                                 smoothstep(-0.2, 0.3, hollow + 0.2);
                fa = lowFlat * smoothstep(0.35, 0.5, big) * (info.biome == Biome::Alpine ? 0.6 : 1.0) * (info.treeLine < 800 ? 0.4 : 1.0);   // highlands: few fields
                fr *= 1.0 - fa;
                gr = (1.0 - rk) * (1.0 - snowy) * (1.0 - fr) * (1.0 - fa) * (h < info.treeLine + 500 ? 1.0 : 0.6);
                if (h < 3.0) { fr *= 0.3; fa = 0; }   // beaches and shore
            }
            size_t o = (static_cast<size_t>(j) * N + i) * 4;
            out[o + 0] = u8(fr);
            out[o + 1] = u8(gr);
            out[o + 2] = u8(fa);
            out[o + 3] = u8(rk);
        }
    });
}

// Desert: sparse scrub in low gentle ground; green climates: forests below the tree line.
// Inside the map the land-cover map decides (so 3D trees stand exactly where the shader paints forest).
double forestMask(double x, double z, double h) {
    if (g_cover && std::fabs(x) < Terrain::kHalfSize - 40 && std::fabs(z) < Terrain::kHalfSize - 40) {
        double f = coverAt(x, z).x;
        return g_info && g_info->biome == Biome::Desert ? f * 0.6 : f;
    }
    if (g_info && g_info->biome != Biome::Desert) {
        double f = smoothstep(0.40, 0.58, fbm2(x / 900.0, z / 900.0, 3, 99u)) * 0.95;
        f *= 1.0 - smoothstep(g_info->treeLine - 180.0, g_info->treeLine + 60.0, h);
        f *= smoothstep(2.0, 10.0, h);
        return f;
    }
    double tl = g_info ? g_info->treeLine : 520.0;
    double f = smoothstep(0.50, 0.66, fbm2(x / 700.0, z / 700.0, 3, 99u)) * 0.55;
    f *= 1.0 - smoothstep(tl - 270.0, tl, h);
    f *= smoothstep(6.0, 18.0, h);
    return f;
}
double wadiMask(double x, double z) {
    if (g_info && g_info->real) return 0.0;   // the dry river bed belongs to the desert map
    double wadiZ = 3700.0 + (fbm2(x / 3000.0, 4.2, 3, 71u) - 0.5) * 2200.0;
    return (1.0 - smoothstep(30.0, 200.0, std::fabs(z - wadiZ))) * smoothstep(-6000.0, -2000.0, x) * (1.0 - smoothstep(7500.0, 8500.0, x));
}
double townMask(const Terrain& t, double x, double z) {
    if (t.info().real) return 0.0;   // real maps get their towns from the scenery generator
    Vec3 c = t.townCenter();
    double d = std::sqrt((x - c.x) * (x - c.x) + (z - c.z) * (z - c.z));
    return 1.0 - smoothstep(850.0, 1250.0, d);
}

Mesh buildGridMesh(const Terrain& t, double x0, double z0, double size, int n, double skirt, double lower,
                   bool skipInner) {
    MeshBuilder mb;
    double s = size / (n - 1);
    std::vector<double> H((n + 2) * (n + 2));
    auto at = [&](int i, int j) -> double& { return H[(j + 1) * (n + 2) + (i + 1)]; };
    for (int j = -1; j <= n; ++j)
        for (int i = -1; i <= n; ++i) at(i, j) = t.height(x0 + i * s, z0 + j * s);
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
            double x = x0 + i * s, z = z0 + j * s, h = at(i, j);
            Vector3 nn = Vector3Normalize(V3(at(i - 1, j) - at(i + 1, j), 2 * s, at(i, j - 1) - at(i, j + 1)));
            double tm = townMask(t, x, z);
            double fm = forestMask(x, z, h) * (1.0 - tm);
            if (t.onRunway(x, z, 400.0)) fm = 0;
            Color tint = surfaceTint(t, x, z, h, fm, tm);
            tint.b = u8(wadiMask(x, z) * (1.0 - tm));
            mb.vertex(V3(x, h - lower, z), nn, tint);
        }
    auto id = [&](int i, int j) { return static_cast<unsigned short>(j * n + i); };
    for (int j = 0; j < n - 1; ++j)
        for (int i = 0; i < n - 1; ++i) {
            if (skipInner) {
                double cx0 = x0 + i * s, cz0 = z0 + j * s;
                double lim = Terrain::kHalfSize;
                if (cx0 >= -lim && cx0 + s <= lim && cz0 >= -lim && cz0 + s <= lim) continue;
            }
            unsigned short a = id(i, j), b = id(i + 1, j), c = id(i, j + 1), d = id(i + 1, j + 1);
            mb.idx.insert(mb.idx.end(), {a, c, b, b, c, d});
        }
    if (skirt > 0) {
        auto addSkirt = [&](int i0, int j0, int di, int dj) {
            unsigned short base = static_cast<unsigned short>(mb.vertexCount());
            for (int k = 0; k < n; ++k) {
                int i = i0 + di * k, j = j0 + dj * k;
                size_t vi = id(i, j);
                Vector3 p = {mb.pos[vi * 3], mb.pos[vi * 3 + 1] - static_cast<float>(skirt), mb.pos[vi * 3 + 2]};
                Vector3 nn = {mb.nrm[vi * 3], mb.nrm[vi * 3 + 1], mb.nrm[vi * 3 + 2]};
                Color c = {mb.col[vi * 4], mb.col[vi * 4 + 1], mb.col[vi * 4 + 2], 255};
                mb.vertex(p, nn, c);
            }
            for (int k = 0; k < n - 1; ++k) {
                unsigned short a = id(i0 + di * k, j0 + dj * k), b = id(i0 + di * (k + 1), j0 + dj * (k + 1));
                unsigned short c = base + k, d = base + k + 1;
                mb.idx.insert(mb.idx.end(), {a, b, c, b, d, c});
            }
        };
        addSkirt(0, 0, 1, 0);
        addSkirt(0, n - 1, 1, 0);
        addSkirt(0, 0, 0, 1);
        addSkirt(n - 1, 0, 0, 1);
    }
    return mb.build(true);
}

void addTree(MeshBuilder& mb, Vector3 p, float height, float radius, Color c, int kind) {
    const int sides = kind == 0 ? 5 : 6;
    if (kind == 2) {  // palm: slim trunk + drooping fronds
        Color trunk = {92, 76, 58, 255};
        float tr = height * 0.035f;
        for (int i = 0; i < 5; ++i) {
            float a0 = 2 * PI * i / 5, a1 = 2 * PI * (i + 1) / 5;
            Vector3 b0 = {p.x + cosf(a0) * tr, p.y, p.z + sinf(a0) * tr}, b1 = {p.x + cosf(a1) * tr, p.y, p.z + sinf(a1) * tr};
            Vector3 t0 = {p.x + cosf(a0) * tr * 0.7f + height * 0.04f, p.y + height, p.z + sinf(a0) * tr * 0.7f};
            Vector3 t1 = {p.x + cosf(a1) * tr * 0.7f + height * 0.04f, p.y + height, p.z + sinf(a1) * tr * 0.7f};
            mb.quad(b0, t0, t1, b1, trunk);
        }
        Vector3 top = {p.x + height * 0.04f, p.y + height, p.z};
        for (int f = 0; f < 7; ++f) {
            float a = 2 * PI * f / 7 + 0.3f;
            Vector3 dir = {cosf(a), 0, sinf(a)};
            Vector3 side = {-dir.z * radius * 0.22f, 0, dir.x * radius * 0.22f};
            Vector3 mid = {top.x + dir.x * radius * 0.55f, top.y + radius * 0.12f, top.z + dir.z * radius * 0.55f};
            Vector3 tip = {top.x + dir.x * radius, top.y - radius * 0.45f, top.z + dir.z * radius};
            Vector3 m0 = Vector3Add(mid, side), m1 = Vector3Subtract(mid, side);
            mb.tri2(top, m0, tip, c);
            mb.tri2(top, tip, m1, c);
        }
        return;
    }
    if (kind == 0) {  // conifer
        Vector3 top = {p.x, p.y + height, p.z};
        for (int i = 0; i < sides; ++i) {
            float a0 = 2 * PI * i / sides, a1 = 2 * PI * (i + 1) / sides;
            Vector3 b0 = {p.x + cosf(a0) * radius, p.y + height * 0.12f, p.z + sinf(a0) * radius};
            Vector3 b1 = {p.x + cosf(a1) * radius, p.y + height * 0.12f, p.z + sinf(a1) * radius};
            Color cc = c;
            float shade = 0.8f + 0.2f * cosf(a0 * 1.3f);
            cc.r = (unsigned char)(cc.r * shade); cc.g = (unsigned char)(cc.g * shade); cc.b = (unsigned char)(cc.b * shade);
            Vector3 n0 = Vector3Normalize({cosf(a0), 0.6f, sinf(a0)});
            Vector3 n1 = Vector3Normalize({cosf(a1), 0.6f, sinf(a1)});
            mb.triN(top, b1, b0, {0, 1, 0}, n1, n0, cc);
        }
    } else {  // broadleaf blob (double cone)
        Vector3 top = {p.x, p.y + height, p.z};
        Vector3 bot = {p.x, p.y + height * 0.15f, p.z};
        float mid = height * 0.55f;
        for (int i = 0; i < sides; ++i) {
            float a0 = 2 * PI * i / sides, a1 = 2 * PI * (i + 1) / sides;
            float r0 = radius * (0.85f + 0.3f * fabsf(sinf(a0 * 2.7f + p.x))), r1 = radius * (0.85f + 0.3f * fabsf(sinf(a1 * 2.7f + p.x)));
            Vector3 m0 = {p.x + cosf(a0) * r0, p.y + mid, p.z + sinf(a0) * r0};
            Vector3 m1 = {p.x + cosf(a1) * r1, p.y + mid, p.z + sinf(a1) * r1};
            Vector3 n0 = Vector3Normalize({cosf(a0), 0.3f, sinf(a0)});
            Vector3 n1 = Vector3Normalize({cosf(a1), 0.3f, sinf(a1)});
            mb.triN(top, m1, m0, {0, 1, 0}, n1, n0, c);
            mb.triN(bot, m0, m1, {0, -1, 0}, n0, n1, c);
        }
    }
}

}  // namespace

void WorldMeshes::build(const Terrain& t) {
    terrain = &t;
    g_info = &t.info();
    const bool green = t.info().biome != Biome::Desert;
    noise = makeNoiseTexture();
    detailGrad = makeDetailGradTexture();
    puff = makeSprite(0);
    {
        // the four cumulus sprites share one 2x2 atlas: the whole cloud field draws in one batch
        // (switching textures per sprite flushed the batch hundreds of times a frame, which
        // stalls Apple's OpenGL driver)
        Image atlas = GenImageColor(512, 512, BLANK);
        for (int i = 0; i < 4; ++i) {
            Image im = makeCloudSprite(100u + i * 17u);
            ImageDraw(&atlas, im, {0, 0, 256, 256}, {static_cast<float>((i % 2) * 256), static_cast<float>((i / 2) * 256), 256, 256}, WHITE);
            UnloadImage(im);
        }
        cloudAtlas = LoadTextureFromImage(atlas);
        UnloadImage(atlas);
        GenTextureMipmaps(&cloudAtlas);
        SetTextureFilter(cloudAtlas, TEXTURE_FILTER_TRILINEAR);
    }
    glow = makeSprite(1);
    spark = makeSprite(2);

    // ---- land cover (forest / grass / farmland / rock) ----
    computeLandCover(t, coverPixels);
    g_cover = &coverPixels;
    {
        Image img = {coverPixels.data(), kCoverN, kCoverN, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
        landCover = LoadTextureFromImage(img);
        SetTextureFilter(landCover, TEXTURE_FILTER_BILINEAR);
        SetTextureWrap(landCover, TEXTURE_WRAP_CLAMP);
    }

    // ---- terrain chunks ----
    chunks.resize(kChunks * kChunks);
    for (int cz = 0; cz < kChunks; ++cz)
        for (int cx = 0; cx < kChunks; ++cx) {
            TerrainChunk& c = chunks[cz * kChunks + cx];
            double x0 = -Terrain::kHalfSize + cx * kChunkSize, z0 = -Terrain::kHalfSize + cz * kChunkSize;
            c.x0 = x0; c.z0 = z0;
            c.hi = buildGridMesh(t, x0, z0, kChunkSize, 65, 40.0, 0.0, false);
            c.lo = buildGridMesh(t, x0, z0, kChunkSize, 17, 80.0, 0.0, false);
            double cy = t.height(x0 + kChunkSize / 2, z0 + kChunkSize / 2);
            c.center = V3(x0 + kChunkSize / 2, cy, z0 + kChunkSize / 2);
            c.radius = static_cast<float>(kChunkSize * 0.75 + 600.0);
            // highest ground in the chunk (incl. a margin around it): chunks entirely under the
            // opaque sea are never visible and are skipped when drawing
            float mh = -1e9f;
            for (double sz = z0 - 60; sz <= z0 + kChunkSize + 60; sz += 31.25)
                for (double sx = x0 - 60; sx <= x0 + kChunkSize + 60; sx += 31.25) mh = std::max(mh, static_cast<float>(t.height(sx, sz)));
            c.maxH = mh;

            // trees
            MeshBuilder tb;
            Rng rng(1000 + cz * 31 + cx * 7);
            const double step = green ? 38.0 : 46.0;
            for (double z = z0; z < z0 + kChunkSize; z += step)
                for (double x = x0; x < x0 + kChunkSize; x += step) {
                    double px = x + rng.range(0, step), pz = z + rng.range(0, step);
                    double h = t.height(px, pz);
                    double f = forestMask(px, pz, h) * (1.0 - townMask(t, px, pz));
                    double wadi = wadiMask(px, pz);
                    if (rng.uniform() > f * 0.55 + wadi * 0.5) { rng.next(); continue; }
                    if (t.onRunway(px, pz, 450.0)) continue;
                    Vec3 n = t.normal(px, pz, 8.0);
                    if (n.y < (green ? 0.80 : 0.85)) continue;
                    if (green) {
                        // conifers (alpine / temperate) or broadleaf canopy (tropical)
                        bool tropical = t.info().biome == Biome::Tropical;
                        float hgt = static_cast<float>(tropical ? rng.range(9, 17) : rng.range(8, 19));
                        double v = rng.uniform();
                        Color col = tropical ? Color{u8(0.07 + 0.04 * v), u8(0.15 + 0.06 * v), u8(0.05 + 0.02 * v), 255}
                                             : Color{u8(0.05 + 0.03 * v), u8(0.10 + 0.04 * v), u8(0.06 + 0.02 * v), 255};
                        addTree(tb, V3(px, h - 0.6, pz), hgt, tropical ? hgt * 0.45f : hgt * 0.28f, col, tropical ? 1 : 0);
                        continue;
                    }
                    // palms along the wadi, low acacia-like shrubs elsewhere
                    bool palm = wadi > 0.4 && rng.uniform() < 0.5;
                    float hgt = palm ? static_cast<float>(rng.range(9, 14)) : static_cast<float>(rng.range(2.5, 5.5));
                    float rad = palm ? hgt * 0.28f : static_cast<float>(hgt * rng.range(0.7, 1.1));
                    double v = rng.uniform();
                    Color col = palm ? Color{u8(0.16 + 0.05 * v), u8(0.22 + 0.06 * v), u8(0.10 + 0.03 * v), 255}
                                     : Color{u8(0.24 + 0.06 * v), u8(0.25 + 0.05 * v), u8(0.15 + 0.03 * v), 255};
                    addTree(tb, V3(px, h - 0.6, pz), hgt, palm ? hgt * 0.42f : rad, col, palm ? 2 : 1);
                }
            if (tb.vertexCount() > 0) { c.trees = tb.build(); c.hasTrees = true; }
        }

    // ---- terrain data texture: per-pixel normal + ambient occlusion (+ sun shadow, baked later) ----
    {
        const int N = 2048;
        const double cell = 2.0 * Terrain::kHalfSize / N;
        std::vector<float> H(static_cast<size_t>(N) * N);
        auto par = [&](auto&& fn) { parallelRows(N, fn); };
        par([&](int j) {
            for (int i = 0; i < N; ++i)
                H[static_cast<size_t>(j) * N + i] = static_cast<float>(t.height(-Terrain::kHalfSize + (i + 0.5) * cell, -Terrain::kHalfSize + (j + 0.5) * cell));
        });
        auto Hc = [&](int i, int j) { i = std::clamp(i, 0, N - 1); j = std::clamp(j, 0, N - 1); return double(H[static_cast<size_t>(j) * N + i]); };
        dataPixels.assign(static_cast<size_t>(N) * N * 4, 255);
        const int dirs = 8;
        const int radii[] = {2, 5, 12, 28};
        par([&](int j) {
            for (int i = 0; i < N; ++i) {
                double h = Hc(i, j);
                Vec3 nn = normalize(Vec3{Hc(i - 1, j) - Hc(i + 1, j), 2 * cell, Hc(i, j - 1) - Hc(i, j + 1)});
                // horizon-based ambient occlusion over four radii and eight directions
                double occ = 0;
                for (int d = 0; d < dirs; ++d) {
                    double a = d * 2.0 * kPi / dirs;
                    double cx = std::cos(a), cz = std::sin(a);
                    double maxTan = 0;
                    for (int r : radii) {
                        double dh = Hc(i + static_cast<int>(std::lround(cx * r)), j + static_cast<int>(std::lround(cz * r))) - h;
                        maxTan = std::max(maxTan, dh / (r * cell));
                    }
                    occ += std::atan(maxTan) / (kPi * 0.5);
                }
                double ao = std::clamp(1.0 - 1.35 * occ / dirs, 0.0, 1.0);
                size_t o = (static_cast<size_t>(j) * N + i) * 4;
                dataPixels[o + 0] = u8(nn.x * 0.5 + 0.5);
                dataPixels[o + 1] = u8(nn.z * 0.5 + 0.5);
                dataPixels[o + 2] = u8(ao);
                dataPixels[o + 3] = 255;
            }
        });
        Image img = {dataPixels.data(), N, N, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
        terrainData = LoadTextureFromImage(img);
        SetTextureFilter(terrainData, TEXTURE_FILTER_BILINEAR);
        SetTextureWrap(terrainData, TEXTURE_WRAP_CLAMP);
    }

    // ---- far ring (coarse terrain out to the horizon) ----
    farRing = buildGridMesh(t, -60000.0, -60000.0, 120000.0, 241, 0.0, 12.0, true);

    // ---- sea, cloud plane ----
    {
        MeshBuilder mb;
        float E = 70000.0f;
        mb.quad({-E, 0, -E}, {-E, 0, E}, {E, 0, E}, {E, 0, -E}, WHITE);
        water = mb.build();
        MeshBuilder cb;
        float C = 42000.0f;
        cb.quad({-C, 0, -C}, {-C, 0, C}, {C, 0, C}, {C, 0, -C}, WHITE);
        cloudPlane = cb.build();
    }

    // ---- runway, taxiways, apron ----
    const RunwayInfo& rw = t.runway();
    {
        MeshBuilder mb;
        float L = static_cast<float>(rw.length), W = static_cast<float>(rw.width);
        float y = static_cast<float>(rw.center.y) + 0.02f;
        float cx = static_cast<float>(rw.center.x), cz = static_cast<float>(rw.center.z);
        // heading 0 => along +X
        int segs = 30;
        for (int i = 0; i < segs; ++i) {
            float a0 = -L / 2 + L * i / segs, a1 = -L / 2 + L * (i + 1) / segs;
            Vector3 p0 = {cx + a0, y, cz - W / 2}, p1 = {cx + a1, y, cz - W / 2};
            Vector3 p2 = {cx + a1, y, cz + W / 2}, p3 = {cx + a0, y, cz + W / 2};
            Vector3 up = {0, 1, 0};
            mb.vertex(p0, up, WHITE, {a0 + L / 2, -W / 2}); mb.vertex(p3, up, WHITE, {a0 + L / 2, W / 2}); mb.vertex(p2, up, WHITE, {a1 + L / 2, W / 2});
            mb.vertex(p0, up, WHITE, {a0 + L / 2, -W / 2}); mb.vertex(p2, up, WHITE, {a1 + L / 2, W / 2}); mb.vertex(p1, up, WHITE, {a1 + L / 2, -W / 2});
        }
        runway = mb.build();
        // edge lights every 60 m
        for (float a = -L / 2; a <= L / 2; a += 60.0f) {
            edgeLights.push_back({cx + a, y + 0.4f, cz - W / 2 - 1.5f});
            edgeLights.push_back({cx + a, y + 0.4f, cz + W / 2 + 1.5f});
        }
        // taxiway (parallel, south side = +z), connectors and apron
        MeshBuilder tw;
        auto strip = [&](float xA, float zA, float xB, float zB, float width) {
            Vector3 d = Vector3Normalize({xB - xA, 0, zB - zA});
            Vector3 s = {-d.z * width / 2, 0, d.x * width / 2};
            float len = sqrtf((xB - xA) * (xB - xA) + (zB - zA) * (zB - zA));
            Vector3 up = {0, 1, 0};
            Vector3 a = {xA - s.x, y - 0.005f, zA - s.z}, b = {xA + s.x, y - 0.005f, zA + s.z};
            Vector3 c = {xB + s.x, y - 0.005f, zB + s.z}, e = {xB - s.x, y - 0.005f, zB - s.z};
            tw.vertex(a, up, WHITE, {0, -width / 2}); tw.vertex(b, up, WHITE, {0, width / 2}); tw.vertex(c, up, WHITE, {len, width / 2});
            tw.vertex(a, up, WHITE, {0, -width / 2}); tw.vertex(c, up, WHITE, {len, width / 2}); tw.vertex(e, up, WHITE, {len, -width / 2});
        };
        strip(cx - L / 2 + 100, cz + 190, cx + L / 2 - 100, cz + 190, 23);
        for (float a : {-L / 2 + 150, -350.0f, 350.0f, L / 2 - 150})
            strip(cx + a, cz + W / 2, cx + a, cz + 190, 20);
        // apron
        strip(cx - 450, cz + 330, cx + 450, cz + 330, 260);
        strip(cx - 120, cz + 190, cx - 120, cz + 220, 30);
        strip(cx + 120, cz + 190, cx + 120, cz + 220, 30);
        taxiways = tw.build();

        // airbase buildings
        MeshBuilder ab;
        Color hangar = {150, 154, 146, 255}, roof = {120, 126, 122, 255}, concrete = {170, 168, 160, 255};
        for (int i = 0; i < 5; ++i) {
            float hx = cx - 400 + i * 190, hz = cz + 520;
            ab.box({hx, y + 7, hz}, {60, 14, 46}, hangar);
            // arched roof
            int seg = 8;
            for (int k = 0; k < seg; ++k) {
                float a0 = PI * k / seg, a1 = PI * (k + 1) / seg;
                Vector3 p0 = {hx - 30, y + 14 + sinf(a0) * 9, hz - cosf(a0) * 23};
                Vector3 p1 = {hx - 30, y + 14 + sinf(a1) * 9, hz - cosf(a1) * 23};
                Vector3 p2 = {hx + 30, y + 14 + sinf(a1) * 9, hz - cosf(a1) * 23};
                Vector3 p3 = {hx + 30, y + 14 + sinf(a0) * 9, hz - cosf(a0) * 23};
                ab.quad(p0, p1, p2, p3, roof);
                ab.tri({hx - 30, y + 14, hz}, p1, p0, hangar);
                ab.tri({hx + 30, y + 14, hz}, p3, p2, hangar);
            }
            // dark doors facing the apron
            ab.box({hx, y + 6, hz - 23.2f}, {50, 12, 0.4f}, {45, 48, 50, 255});
        }
        // hardened aircraft shelters along the west
        for (int i = 0; i < 4; ++i) {
            float hx = cx - 1100 + i * 110, hz = cz + 420;
            int seg = 7;
            for (int k = 0; k < seg; ++k) {
                float a0 = PI * k / seg, a1 = PI * (k + 1) / seg;
                Vector3 p0 = {hx - cosf(a0) * 16, y + sinf(a0) * 9, hz - 22};
                Vector3 p1 = {hx - cosf(a1) * 16, y + sinf(a1) * 9, hz - 22};
                Vector3 p2 = {hx - cosf(a1) * 16, y + sinf(a1) * 9, hz + 22};
                Vector3 p3 = {hx - cosf(a0) * 16, y + sinf(a0) * 9, hz + 22};
                ab.quad(p0, p3, p2, p1, concrete);
            }
            ab.box({hx, y + 4.5f, hz + 22}, {32, 9, 0.6f}, {120, 118, 110, 255});
        }
        // control tower
        ab.cylinder({cx + 520, y, cz + 400}, 5, 28, 10, {200, 198, 190, 255});
        ab.cylinder({cx + 520, y + 28, cz + 400}, 9, 5, 10, {40, 60, 70, 255}, MatrixIdentity(), 10);
        ab.cylinder({cx + 520, y + 33, cz + 400}, 10, 1.2f, 10, {190, 190, 185, 255}, MatrixIdentity(), 4);
        // fuel tanks
        for (int i = 0; i < 3; ++i) ab.cylinder({cx + 700 + i * 34.0f, y, cz + 560}, 13, 12, 14, {225, 225, 220, 255});
        // radar dome
        ab.cylinder({cx + 820, y, cz + 360}, 4, 10, 8, {180, 180, 175, 255});
        ab.cylinder({cx + 820, y + 10, cz + 360}, 9, 7, 12, {235, 235, 235, 255}, MatrixIdentity(), 2);
        // terminal / ops buildings
        for (int i = 0; i < 6; ++i) ab.box({cx + 250 + i * 45.0f, y + 5, cz + 620}, {38, 10, 24}, {196, 186, 165, 255});
        airbase = ab.build();
        parkedJets = {{cx - 380, 1.5708f, cz + 360}, {cx - 290, 1.5708f, cz + 360}, {cx - 200, 1.5708f, cz + 360},
                      {cx + 200, 1.5708f, cz + 360}, {cx + 290, 1.5708f, cz + 360}};
    }

    // ---- town (fictional; real maps get their towns from the scenery generator) ----
    if (t.info().real) {
        MeshBuilder mb;
        mb.box({0, -5000, 0}, {1, 1, 1}, BLACK);   // placeholder so the draw call stays valid
        town = mb.build();
    } else {
        MeshBuilder mb;
        Vec3 c = t.townCenter();
        Rng rng(4242);
        Rng wrng(777);   // windows: separate stream so the town layout stays as it was
        const double cell = 30.0;
        const Color palette[] = {{178, 166, 146, 255}, {196, 184, 160, 255}, {160, 142, 118, 255},
                                 {150, 146, 140, 255}, {186, 160, 130, 255}, {205, 196, 178, 255}};
        const Color roofs[] = {{150, 78, 52, 255}, {128, 64, 44, 255}, {110, 108, 104, 255}, {165, 92, 60, 255}};
        for (double z = -1150; z <= 1150; z += cell)
            for (double x = -1150; x <= 1150; x += cell) {
                double d = std::sqrt(x * x + z * z);
                bool road = std::fmod(std::fabs(x) + 15.0, 150.0) < 30.0 || std::fmod(std::fabs(z) + 15.0, 150.0) < 30.0;
                if (road) continue;
                double dens = 1.0 - smoothstep(250.0, 1150.0, d);
                if (rng.uniform() > dens * 0.7 + 0.03) continue;
                double px = c.x + x + rng.range(-4, 4), pz = c.z + z + rng.range(-4, 4);
                double h = std::min({t.height(px - 8, pz - 8), t.height(px + 8, pz - 8), t.height(px - 8, pz + 8), t.height(px + 8, pz + 8)});
                float w = static_cast<float>(rng.range(11, 22)), dd = static_cast<float>(rng.range(11, 22));
                float hh = static_cast<float>(rng.range(4, 10) * (d < 350 && rng.uniform() < 0.3 ? rng.range(1.5, 3.0) : 1.0));
                Color col = palette[rng.next() % 6];
                Matrix rot = MatrixMultiply(MatrixRotateY(static_cast<float>(rng.range(-0.08, 0.08))), MatrixTranslate(static_cast<float>(px), 0, static_cast<float>(pz)));
                float baseY = static_cast<float>(h) - 3.0f;
                mb.box({0, baseY + (hh + 3) / 2, 0}, {w, hh + 3, dd}, col, rot);
                // windows band (darker strips)
                // (vertex alpha 253 = lit at night, 251 = dark: see the lit shader)
                for (float wy = baseY + 5.0f; wy < baseY + hh; wy += 3.4f) {
                    float r = static_cast<float>(wrng.uniform());
                    Color wc = r < 0.45f ? Color{255, 200, 130, 253} : (r < 0.55f ? Color{210, 225, 255, 253} : Color{70, 74, 80, 251});
                    mb.box({0, wy, 0}, {w + 0.15f, 0.9f, dd + 0.15f}, wc, rot);
                }
                if (hh < 14 && rng.uniform() < 0.9) {
                    Color rc = roofs[rng.next() % 4];
                    float top = baseY + hh + 3;
                    Vector3 a = Vector3Transform({-w / 2, top, -dd / 2}, rot), b = Vector3Transform({w / 2, top, -dd / 2}, rot);
                    Vector3 cc = Vector3Transform({w / 2, top, dd / 2}, rot), e = Vector3Transform({-w / 2, top, dd / 2}, rot);
                    Vector3 r0 = Vector3Transform({-w / 2, top + 4, 0}, rot), r1 = Vector3Transform({w / 2, top + 4, 0}, rot);
                    mb.quad(a, r0, r1, b, rc);
                    mb.quad(e, cc, r1, r0, rc);
                    mb.tri(a, e, r0, col);
                    mb.tri(b, r1, cc, col);
                }
            }
        // roads following the ground
        Color road = {68, 68, 70, 255};
        auto roadSeg = [&](double xA, double zA, double xB, double zB) {
            double len = std::sqrt((xB - xA) * (xB - xA) + (zB - zA) * (zB - zA));
            int n = std::max(1, static_cast<int>(len / 25.0));
            double dx = (xB - xA) / len, dz = (zB - zA) / len;
            for (int i = 0; i < n; ++i) {
                double ax = xA + (xB - xA) * i / n, az = zA + (zB - zA) * i / n;
                double bx = xA + (xB - xA) * (i + 1) / n, bz = zA + (zB - zA) * (i + 1) / n;
                double sx = -dz * 5, sz = dx * 5;
                Vector3 p0 = V3(ax - sx, t.height(ax - sx, az - sz) + 0.35, az - sz), p1 = V3(ax + sx, t.height(ax + sx, az + sz) + 0.35, az + sz);
                Vector3 p2 = V3(bx + sx, t.height(bx + sx, bz + sz) + 0.35, bz + sz), p3 = V3(bx - sx, t.height(bx - sx, bz - sz) + 0.35, bz - sz);
                mb.quad(p0, p3, p2, p1, road);
            }
        };
        for (double k = -1050; k <= 1050; k += 150) {
            double ext = std::sqrt(std::max(0.0, 1250.0 * 1250.0 - k * k));
            roadSeg(c.x - ext, c.z + k, c.x + ext, c.z + k);
            roadSeg(c.x + k, c.z - ext, c.x + k, c.z + ext);
        }
        // highway from town to the airbase
        roadSeg(c.x - 1250, c.z, rw.center.x + 900, rw.center.z + 620);
        town = mb.build();
    }

    // ---- depth map + minimap ----
    {
        const int N = 256;
        Image img = GenImageColor(N, N, BLACK);
        Color* px = static_cast<Color*>(img.data);
        for (int y = 0; y < N; ++y)
            for (int x = 0; x < N; ++x) {
                double wx = -Terrain::kHalfSize + (x + 0.5) / N * 2 * Terrain::kHalfSize;
                double wz = -Terrain::kHalfSize + (y + 0.5) / N * 2 * Terrain::kHalfSize;
                double h = t.height(wx, wz);
                px[y * N + x] = {u8((h + 100.0) / 200.0), 0, 0, 255};
            }
        depthMap = LoadTextureFromImage(img);
        SetTextureFilter(depthMap, TEXTURE_FILTER_BILINEAR);
        SetTextureWrap(depthMap, TEXTURE_WRAP_CLAMP);
        UnloadImage(img);

        const int M = 480;
        const double E = kMinimapHalf;
        Image mm = GenImageColor(M, M, BLACK);
        Color* mp = static_cast<Color*>(mm.data);
        for (int y = 0; y < M; ++y)
            for (int x = 0; x < M; ++x) {
                double wx = -E + (x + 0.5) / M * 2 * E;
                double wz = -E + (y + 0.5) / M * 2 * E;
                if (std::fabs(wx) > Terrain::kHalfSize || std::fabs(wz) > Terrain::kHalfSize) {
                    // beyond the playable map: flat desert plain or open sea, slightly darkened
                    double ex = std::clamp(wx, -Terrain::kHalfSize, Terrain::kHalfSize), ez = std::clamp(wz, -Terrain::kHalfSize, Terrain::kHalfSize);
                    bool sea = t.height(ex, ez) < 0;
                    mp[y * M + x] = sea ? Color{11, 40, 63, 255} : Color{150, 134, 104, 255};
                    continue;
                }
                double h = t.height(wx, wz);
                Vec3 n = t.normal(wx, wz, 60.0);
                double shade = std::clamp(0.55 + 0.9 * dot(n, normalize(Vec3{-0.6, 0.7, -0.4})) - 0.45, 0.25, 1.15);
                double r, g, b;
                if (h < 0) { double d = std::clamp(-h / 60.0, 0.0, 1.0); r = 0.05; g = 0.18 - 0.08 * d; b = 0.28 - 0.06 * d; shade = 1; }
                else if (h < 8) { r = 0.72; g = 0.66; b = 0.52; }
                else if (h > t.info().snowLine) { r = g = b = 0.80; }
                else if (green) {
                    double veg = 1.0 - smoothstep(t.info().treeLine - 150.0, t.info().treeLine + 120.0, h);
                    double f = forestMask(wx, wz, h);
                    r = lerpd(0.48, 0.24 - 0.08 * f, veg); g = lerpd(0.47, 0.36 - 0.08 * f, veg); b = lerpd(0.46, 0.18 - 0.05 * f, veg);
                    if (n.y < 0.8) { r = 0.42; g = 0.42; b = 0.43; }
                    if (h > t.info().snowLine) { r = g = b = 0.86; }
                } else {
                    double f = forestMask(wx, wz, h);
                    r = 0.64 - 0.2 * f; g = 0.56 - 0.12 * f; b = 0.43 - 0.14 * f;
                    if (n.y < 0.8) { r = 0.46; g = 0.42; b = 0.38; }
                }
                if (townMask(t, wx, wz) > 0.5) { r = 0.50; g = 0.48; b = 0.44; }
                if (t.onRunway(wx, wz, 20.0)) { r = g = b = 0.9; shade = 1; }
                mp[y * M + x] = {u8(r * shade), u8(g * shade), u8(b * shade), 255};
            }
        minimap = LoadTextureFromImage(mm);
        SetTextureFilter(minimap, TEXTURE_FILTER_BILINEAR);
        // Beyond the map edge show the edge itself, never the opposite side of the map (the
        // default REPEAT wrap drew the eastern sea when flying off the western desert edge).
        SetTextureWrap(minimap, TEXTURE_WRAP_CLAMP);
        UnloadImage(mm);
    }
}

void WorldMeshes::bakeSunShadows(Vector3 sunDir) {
    if (!terrain || dataPixels.empty()) return;
    bakedSun = sunDir;
    const int N = 2048, M = 1024;
    const double cellM = 2.0 * Terrain::kHalfSize / M;
    const Terrain& t = *terrain;
    // heights straight from the shared grid (every 2nd sample = 31.25 m)
    auto H = [&](double x, double z) {
        double gx = (x + Terrain::kHalfSize) / Terrain::kCell, gz = (z + Terrain::kHalfSize) / Terrain::kCell;
        if (gx < 0 || gz < 0 || gx >= Terrain::kGridCells || gz >= Terrain::kGridCells) return 0.0;
        int i = static_cast<int>(gx), j = static_cast<int>(gz);
        double u = gx - i, v = gz - j;
        double a = t.gridAt(i, j), b = t.gridAt(i + 1, j), c = t.gridAt(i, j + 1), d = t.gridAt(i + 1, j + 1);
        return (a + (b - a) * u) * (1 - v) + (c + (d - c) * u) * v;
    };
    Vec3 sd = normalize(Vec3{sunDir.x, std::max(sunDir.y, 0.02f), sunDir.z});
    double horiz = std::sqrt(sd.x * sd.x + sd.z * sd.z);
    Vec3 hd = horiz > 1e-6 ? Vec3{sd.x / horiz, 0, sd.z / horiz} : Vec3{1, 0, 0};
    double rise = sd.y / std::max(horiz, 1e-6);
    std::vector<float> sh(static_cast<size_t>(M) * M, 1.0f);
    auto par = [&](int rows, auto&& fn) { parallelRows(rows, fn); };
    par(M, [&](int j) {
        for (int i = 0; i < M; ++i) {
            double x = -Terrain::kHalfSize + (i + 0.5) * cellM, z = -Terrain::kHalfSize + (j + 0.5) * cellM;
            double h0 = std::max(H(x, z), 0.0) + 2.0;
            double lit = 1.0;
            for (double d = cellM * 0.75; d < 8000.0; d += std::max(cellM * 0.75, d * 0.09)) {
                double ray = h0 + d * rise;
                if (ray > 2300.0) break;
                lit = std::min(lit, (ray - H(x + hd.x * d, z + hd.z * d)) / (d * 0.04 + 3.0));
                if (lit <= 0.0) { lit = 0.0; break; }
            }
            sh[static_cast<size_t>(j) * M + i] = static_cast<float>(lit);
        }
    });
    par(N, [&](int j) {
        for (int i = 0; i < N; ++i) {
            double gx = std::clamp((i + 0.5) * 0.5 - 0.5, 0.0, M - 1.001), gz = std::clamp((j + 0.5) * 0.5 - 0.5, 0.0, M - 1.001);
            int a = static_cast<int>(gx), b = static_cast<int>(gz);
            double u = gx - a, v = gz - b;
            auto S = [&](int p, int q) { return double(sh[static_cast<size_t>(std::min(q, M - 1)) * M + std::min(p, M - 1)]); };
            double val = (S(a, b) * (1 - u) + S(a + 1, b) * u) * (1 - v) + (S(a, b + 1) * (1 - u) + S(a + 1, b + 1) * u) * v;
            dataPixels[(static_cast<size_t>(j) * N + i) * 4 + 3] = u8(std::clamp(val, 0.0, 1.0));
        }
    });
    UpdateTexture(terrainData, dataPixels.data());
}

bool WorldMeshes::ensureL0(TerrainChunk& c, double now, int& budget) {
    c.l0Used = now;
    if (c.hasL0) return true;
    if (budget <= 0 || !terrain) return false;
    --budget;
    c.l0 = buildGridMesh(*terrain, c.x0, c.z0, kChunkSize, 129, 30.0, 0.0, false);
    c.hasL0 = true;
    return true;
}

void WorldMeshes::evictL0(Vector3 camPos, double now) {
    int count = 0;
    for (auto& c : chunks) count += c.hasL0;
    if (count <= 20) return;
    for (auto& c : chunks) {
        if (!c.hasL0) continue;
        if (now - c.l0Used > 2.0 && Vector3Distance(c.center, camPos) > 5000.0f) {
            UnloadMesh(c.l0);
            c.l0 = Mesh{};
            c.hasL0 = false;
            if (--count <= 20) break;
        }
    }
}

void WorldMeshes::unload() {
    for (auto& c : chunks) {
        if (c.hasL0) UnloadMesh(c.l0);
        UnloadMesh(c.hi);
        UnloadMesh(c.lo);
        if (c.hasTrees) UnloadMesh(c.trees);
    }
    chunks.clear();
    UnloadMesh(farRing); UnloadMesh(water); UnloadMesh(cloudPlane);
    UnloadMesh(runway); UnloadMesh(taxiways); UnloadMesh(airbase); UnloadMesh(town);
    UnloadTexture(noise); UnloadTexture(detailGrad); UnloadTexture(landCover); UnloadTexture(depthMap); UnloadTexture(minimap); UnloadTexture(terrainData);
    UnloadTexture(puff); UnloadTexture(glow); UnloadTexture(spark);
    UnloadTexture(cloudAtlas);
}

}  // namespace sky
