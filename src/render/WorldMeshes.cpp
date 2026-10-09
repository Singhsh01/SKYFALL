#include "WorldMeshes.hpp"

#include "MeshBuilder.hpp"

#include <algorithm>
#include <cmath>

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

Color surfaceTint(const Terrain& t, double x, double z, double h, double forest, double town) {
    // vertex mask colour for the terrain shader (r forest, g town, b unused)
    return {u8(forest), u8(town), 0, 255};
}

double forestMask(double x, double z, double h) {
    double f = smoothstep(0.47, 0.60, fbm2(x / 900.0, z / 900.0, 3, 99u));
    f *= 1.0 - smoothstep(850.0, 1150.0, h);
    f *= smoothstep(6.0, 20.0, h);
    return f;
}
double townMask(const Terrain& t, double x, double z) {
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
            mb.vertex(V3(x, h - lower, z), nn, surfaceTint(t, x, z, h, fm, tm));
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
            Vector3 m0 = {p.x + cosf(a0) * radius, p.y + mid, p.z + sinf(a0) * radius};
            Vector3 m1 = {p.x + cosf(a1) * radius, p.y + mid, p.z + sinf(a1) * radius};
            Vector3 n0 = Vector3Normalize({cosf(a0), 0.3f, sinf(a0)});
            Vector3 n1 = Vector3Normalize({cosf(a1), 0.3f, sinf(a1)});
            mb.triN(top, m1, m0, {0, 1, 0}, n1, n0, c);
            mb.triN(bot, m0, m1, {0, -1, 0}, n0, n1, c);
        }
    }
}

}  // namespace

void WorldMeshes::build(const Terrain& t) {
    noise = makeNoiseTexture();
    puff = makeSprite(0);
    glow = makeSprite(1);
    spark = makeSprite(2);

    // ---- terrain chunks ----
    chunks.resize(kChunks * kChunks);
    for (int cz = 0; cz < kChunks; ++cz)
        for (int cx = 0; cx < kChunks; ++cx) {
            TerrainChunk& c = chunks[cz * kChunks + cx];
            double x0 = -Terrain::kHalfSize + cx * kChunkSize, z0 = -Terrain::kHalfSize + cz * kChunkSize;
            c.hi = buildGridMesh(t, x0, z0, kChunkSize, 65, 40.0, 0.0, false);
            c.lo = buildGridMesh(t, x0, z0, kChunkSize, 17, 80.0, 0.0, false);
            double cy = t.height(x0 + kChunkSize / 2, z0 + kChunkSize / 2);
            c.center = V3(x0 + kChunkSize / 2, cy, z0 + kChunkSize / 2);
            c.radius = static_cast<float>(kChunkSize * 0.75 + 600.0);

            // trees
            MeshBuilder tb;
            Rng rng(1000 + cz * 31 + cx * 7);
            const double step = 46.0;
            for (double z = z0; z < z0 + kChunkSize; z += step)
                for (double x = x0; x < x0 + kChunkSize; x += step) {
                    double px = x + rng.range(0, step), pz = z + rng.range(0, step);
                    double h = t.height(px, pz);
                    double f = forestMask(px, pz, h) * (1.0 - townMask(t, px, pz));
                    double sparse = 0.04 * smoothstep(10.0, 40.0, h) * (1.0 - smoothstep(800, 1000, h));
                    if (rng.uniform() > f * 0.95 + sparse) { rng.next(); continue; }
                    if (t.onRunway(px, pz, 450.0)) continue;
                    Vec3 n = t.normal(px, pz, 8.0);
                    if (n.y < 0.8) continue;
                    double kind = rng.uniform() < 0.55 ? 0 : 1;
                    float hgt = static_cast<float>(rng.range(9, 17));
                    float rad = static_cast<float>(hgt * rng.range(0.28, 0.42));
                    double v = rng.uniform();
                    Color col = kind == 0 ? Color{u8(0.10 + 0.04 * v), u8(0.17 + 0.06 * v), u8(0.08 + 0.03 * v), 255}
                                          : Color{u8(0.17 + 0.07 * v), u8(0.23 + 0.07 * v), u8(0.09 + 0.03 * v), 255};
                    addTree(tb, V3(px, h - 1.0, pz), hgt, rad, col, static_cast<int>(kind));
                }
            if (tb.vertexCount() > 0) { c.trees = tb.build(); c.hasTrees = true; }
        }

    // ---- far ring (coarse terrain out to the horizon) ----
    farRing = buildGridMesh(t, -60000.0, -60000.0, 120000.0, 101, 0.0, 12.0, true);

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

    // ---- town (fictional) ----
    {
        MeshBuilder mb;
        Vec3 c = t.townCenter();
        Rng rng(4242);
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
                for (float wy = baseY + 5.0f; wy < baseY + hh; wy += 3.4f)
                    mb.box({0, wy, 0}, {w + 0.15f, 0.9f, dd + 0.15f}, {70, 74, 80, 255}, rot);
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

        const int M = 384;
        Image mm = GenImageColor(M, M, BLACK);
        Color* mp = static_cast<Color*>(mm.data);
        for (int y = 0; y < M; ++y)
            for (int x = 0; x < M; ++x) {
                double wx = -Terrain::kHalfSize + (x + 0.5) / M * 2 * Terrain::kHalfSize;
                double wz = -Terrain::kHalfSize + (y + 0.5) / M * 2 * Terrain::kHalfSize;
                double h = t.height(wx, wz);
                Vec3 n = t.normal(wx, wz, 60.0);
                double shade = std::clamp(0.55 + 0.9 * dot(n, normalize(Vec3{-0.6, 0.7, -0.4})) - 0.45, 0.25, 1.15);
                double r, g, b;
                if (h < 0) { double d = std::clamp(-h / 60.0, 0.0, 1.0); r = 0.05; g = 0.18 - 0.08 * d; b = 0.28 - 0.06 * d; shade = 1; }
                else if (h < 8) { r = 0.55; g = 0.52; b = 0.40; }
                else if (h > 1300) { r = g = b = 0.85; }
                else {
                    double f = forestMask(wx, wz, h);
                    r = 0.30 - 0.12 * f; g = 0.34 - 0.08 * f; b = 0.20 - 0.08 * f;
                    if (n.y < 0.8) { r = 0.40; g = 0.38; b = 0.34; }
                }
                if (townMask(t, wx, wz) > 0.5) { r = 0.50; g = 0.48; b = 0.44; }
                if (t.onRunway(wx, wz, 20.0)) { r = g = b = 0.9; shade = 1; }
                mp[y * M + x] = {u8(r * shade), u8(g * shade), u8(b * shade), 255};
            }
        minimap = LoadTextureFromImage(mm);
        SetTextureFilter(minimap, TEXTURE_FILTER_BILINEAR);
        UnloadImage(mm);
    }
}

void WorldMeshes::unload() {
    for (auto& c : chunks) {
        UnloadMesh(c.hi);
        UnloadMesh(c.lo);
        if (c.hasTrees) UnloadMesh(c.trees);
    }
    chunks.clear();
    UnloadMesh(farRing); UnloadMesh(water); UnloadMesh(cloudPlane);
    UnloadMesh(runway); UnloadMesh(taxiways); UnloadMesh(airbase); UnloadMesh(town);
    UnloadTexture(noise); UnloadTexture(depthMap); UnloadTexture(minimap);
    UnloadTexture(puff); UnloadTexture(glow); UnloadTexture(spark);
}

}  // namespace sky
