#include "Dressing.hpp"

#include "MeshBuilder.hpp"
#include "Props.hpp"
#include "raymath.h"

#include <algorithm>
#include <cmath>

namespace sky {

namespace {

Vector3 V(const Vec3& v) { return {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)}; }

/// Quad whose face normal points towards `towards` (vertices in any winding).
void quadFacing(MeshBuilder& m, Vector3 a, Vector3 b, Vector3 c, Vector3 d, Color col, Vector3 towards) {
    Vector3 n = Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(c, a));
    if (Vector3DotProduct(n, towards) < 0) { std::swap(b, d); }
    m.quad(a, b, c, d, col);
}

struct Piecer {
    std::vector<Dressing::Piece>* out;
    MeshBuilder mb;
    Vector3 lo{1e9f, 1e9f, 1e9f}, hi{-1e9f, -1e9f, -1e9f};
    void grow(Vector3 p) { lo = Vector3Min(lo, p); hi = Vector3Max(hi, p); }
    void flush() {
        if (mb.vertexCount() == 0) return;
        Dressing::Piece p;
        p.mesh = mb.build();
        p.center = Vector3Scale(Vector3Add(lo, hi), 0.5f);
        p.radius = Vector3Distance(lo, hi) * 0.5f + 1.0f;
        p.valid = true;
        out->push_back(p);
        mb = MeshBuilder{};
        lo = {1e9f, 1e9f, 1e9f};
        hi = {-1e9f, -1e9f, -1e9f};
    }
};

/// Road or railway along an engineered polyline: deck on the profile (never below the ground),
/// embankment slopes where it runs above the terrain, a bridge with piers where the gap is large.
void corridor(const Terrain& t, const Polyline& p, bool rail, std::vector<Dressing::Piece>& out) {
    const float hw = rail ? 3.0f : 4.6f;
    const Color top = rail ? Color{112, 102, 92, 255} : Color{58, 58, 60, 255};
    const Color bank = rail ? Color{150, 132, 108, 255} : Color{160, 140, 112, 255};
    const Color deck = {158, 154, 146, 255}, pier = {150, 145, 135, 255}, railCol = {60, 56, 52, 255}, line = {210, 205, 190, 255};
    const double bridgeThr = rail ? 6.0 : 7.0;
    Piecer pc{&out};
    double lastPier = -1e9, pieceStart = 0;
    for (size_t i = 0; i + 1 < p.pts.size(); ++i) {
        const Vec3 &a = p.pts[i], &b = p.pts[i + 1];
        Vec3 dir = normalize(Vec3{b.x - a.x, 0, b.z - a.z});
        Vec3 nrm{-dir.z, 0, dir.x};
        double ga = t.surfaceHeight(a.x, a.z), gb = t.surfaceHeight(b.x, b.z);
        bool bridge = (a.y - ga > bridgeThr) || (b.y - gb > bridgeThr);
        double ya = (bridge ? a.y : std::max(a.y, ga)) + 0.35, yb = (bridge ? b.y : std::max(b.y, gb)) + 0.35;
        Vector3 aL = V(Vec3{a.x, ya, a.z} + nrm * hw), aR = V(Vec3{a.x, ya, a.z} - nrm * hw);
        Vector3 bL = V(Vec3{b.x, yb, b.z} + nrm * hw), bR = V(Vec3{b.x, yb, b.z} - nrm * hw);
        Vector3 up = {0, 1, 0};
        quadFacing(pc.mb, aL, bL, bR, aR, bridge ? deck : top, up);
        pc.grow(aL); pc.grow(bR);
        if (rail) {
            for (float off : {-0.75f, 0.75f}) {
                Vec3 o = nrm * off;
                Vector3 r0 = V(Vec3{a.x, ya + 0.2, a.z} + o + nrm * 0.08), r1 = V(Vec3{b.x, yb + 0.2, b.z} + o + nrm * 0.08);
                Vector3 r2 = V(Vec3{b.x, yb + 0.2, b.z} + o - nrm * 0.08), r3 = V(Vec3{a.x, ya + 0.2, a.z} + o - nrm * 0.08);
                quadFacing(pc.mb, r0, r1, r2, r3, railCol, up);
            }
        } else if ((static_cast<int>(p.cum[i] / 12.0) % 2) == 0) {
            // dashed centre line
            Vector3 c0 = V(Vec3{a.x, ya + 0.03, a.z} + nrm * 0.12), c1 = V(Vec3{b.x, yb + 0.03, b.z} + nrm * 0.12);
            Vector3 c2 = V(Vec3{b.x, yb + 0.03, b.z} - nrm * 0.12), c3 = V(Vec3{a.x, ya + 0.03, a.z} - nrm * 0.12);
            quadFacing(pc.mb, c0, c1, c2, c3, line, up);
        }
        if (bridge) {
            // deck edge beams + parapets
            for (int side = -1; side <= 1; side += 2) {
                Vec3 e = nrm * (side * hw);
                Vector3 t0 = V(Vec3{a.x, ya + 1.0, a.z} + e), t1 = V(Vec3{b.x, yb + 1.0, b.z} + e);
                Vector3 b0 = V(Vec3{a.x, ya - 2.2, a.z} + e), b1 = V(Vec3{b.x, yb - 2.2, b.z} + e);
                quadFacing(pc.mb, t0, t1, b1, b0, deck, V(nrm * static_cast<double>(side)));
            }
            Vector3 u0 = V(Vec3{a.x, ya - 2.2, a.z} + nrm * hw), u1 = V(Vec3{b.x, yb - 2.2, b.z} + nrm * hw);
            Vector3 u2 = V(Vec3{b.x, yb - 2.2, b.z} - nrm * hw), u3 = V(Vec3{a.x, ya - 2.2, a.z} - nrm * hw);
            quadFacing(pc.mb, u0, u1, u2, u3, deck, Vector3{0, -1, 0});
            double spacing = rail ? 28.0 : 34.0;
            if (p.cum[i] - lastPier >= spacing) {
                lastPier = p.cum[i];
                double g = t.surfaceHeight(a.x, a.z);
                double hgt = ya - 2.2 - g + 1.0;
                if (hgt > 1.5) {
                    float yaw = static_cast<float>(std::atan2(dir.z, dir.x));
                    Matrix xf = MatrixMultiply(MatrixRotateY(-yaw), MatrixTranslate(static_cast<float>(a.x), 0, static_cast<float>(a.z)));
                    pc.mb.box({0, static_cast<float>(g - 1.0 + hgt * 0.5), 0}, {rail ? 3.0f : 2.4f, static_cast<float>(hgt), hw * 1.6f}, pier, xf);
                    if (rail && hgt > 12) {   // viaduct cross-beam
                        pc.mb.box({0, static_cast<float>(ya - 4.0), 0}, {2.0f, 2.2f, hw * 2.0f}, pier, xf);
                    }
                }
            }
        } else {
            // embankment slopes where the deck sits above the terrain
            double da = ya - ga, db = yb - gb;
            if (da > 0.9 || db > 0.9) {
                for (int side = -1; side <= 1; side += 2) {
                    Vec3 e = nrm * (side * hw);
                    Vec3 oa = Vec3{a.x, 0, a.z} + e + nrm * (side * std::max(0.0, da) * 1.6);
                    Vec3 ob = Vec3{b.x, 0, b.z} + e + nrm * (side * std::max(0.0, db) * 1.6);
                    oa.y = t.surfaceHeight(oa.x, oa.z) - 0.3;
                    ob.y = t.surfaceHeight(ob.x, ob.z) - 0.3;
                    Vector3 ea = V(Vec3{a.x, ya - 0.02, a.z} + e), eb = V(Vec3{b.x, yb - 0.02, b.z} + e);
                    quadFacing(pc.mb, ea, eb, V(ob), V(oa), bank, Vector3{static_cast<float>(nrm.x * side), 0.8f, static_cast<float>(nrm.z * side)});
                }
            }
        }
        if (p.cum[i + 1] - pieceStart > 1500.0 || pc.mb.vertexCount() > 60000) { pc.flush(); pieceStart = p.cum[i + 1]; }
    }
    pc.flush();
}

struct Palette { Color walls[4]; Color roof[3]; bool pitched; };

Palette paletteFor(BuildingStyle s) {
    switch (s) {
        case BuildingStyle::Temperate:
            return {{{205, 196, 182, 255}, {184, 166, 146, 255}, {222, 218, 208, 255}, {170, 120, 96, 255}}, {{150, 62, 46, 255}, {96, 96, 100, 255}, {120, 70, 52, 255}}, true};
        case BuildingStyle::Alpine:
            return {{{150, 112, 82, 255}, {196, 188, 176, 255}, {132, 96, 70, 255}, {214, 206, 194, 255}}, {{70, 72, 78, 255}, {90, 64, 52, 255}, {60, 62, 66, 255}}, true};
        case BuildingStyle::Tropical:
            return {{{238, 214, 176, 255}, {190, 222, 206, 255}, {244, 242, 232, 255}, {236, 190, 170, 255}}, {{166, 72, 52, 255}, {150, 150, 146, 255}, {180, 96, 60, 255}}, true};
        default:
            return {{{196, 178, 146, 255}, {214, 204, 184, 255}, {170, 140, 104, 255}, {150, 128, 100, 255}}, {{168, 154, 128, 255}, {120, 112, 100, 255}, {150, 136, 112, 255}}, false};
    }
}

unsigned hash2(int a, int b) {
    unsigned h = static_cast<unsigned>(a) * 73856093u ^ static_cast<unsigned>(b) * 19349663u;
    h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
    return h;
}

/// Window panes on the four faces of a box building (local frame xf, footprint w x d). Vertex
/// alpha flags them for the lit shader: 253 = lit at night (warm), 251 = dark. Towers get a
/// continuous glazing band per floor; houses and shops get separate windows.
void windows(MeshBuilder& m, Matrix xf, float w, float d, float y0, float y1, bool tower, bool industrial, unsigned seed) {
    const float floorH = 3.2f;
    int floor = 0;
    for (float y = y0 + 1.3f; y + 1.1f < y1 - 0.6f; y += floorH, ++floor) {
        for (int face = 0; face < 4; ++face) {
            bool alongX = face < 2;
            float len = alongX ? w : d;
            float off = (alongX ? d : w) * 0.5f + 0.06f;
            float sgn = (face & 1) ? 1.0f : -1.0f;
            auto P = [&](float u, float v) {
                Vector3 l = alongX ? Vector3{u, v, sgn * off} : Vector3{sgn * off, v, u};
                return Vector3Transform(l, xf);
            };
            Vector3 out = Vector3Subtract(Vector3Transform(alongX ? Vector3{0, 0, sgn} : Vector3{sgn, 0, 0}, xf), Vector3Transform({0, 0, 0}, xf));
            unsigned h = seed * 2654435761u ^ static_cast<unsigned>(floor * 97 + face * 13);
            h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
            if (tower || industrial) {
                bool lit = (h & 255) < (industrial ? 50u : 150u);
                Color c = lit ? ((h >> 8) & 1 ? Color{210, 226, 255, 253} : Color{255, 214, 150, 253}) : Color{18, 22, 28, 251};
                float hh = industrial ? 0.8f : 1.5f;
                quadFacing(m, P(-len * 0.45f, y), P(len * 0.45f, y), P(len * 0.45f, y + hh), P(-len * 0.45f, y + hh), c, out);
            } else {
                int n = std::max(1, static_cast<int>(len / 3.6f));
                for (int k = 0; k < n; ++k) {
                    unsigned hk = h ^ static_cast<unsigned>(k * 0x9E3779B9u);
                    hk ^= hk >> 11; hk *= 0x2C1B3C6Du; hk ^= hk >> 15;
                    bool lit = (hk & 255) < 105u;
                    Color c = lit ? ((hk >> 9) % 3 == 0 ? Color{255, 230, 180, 253} : Color{255, 190, 110, 253}) : Color{20, 24, 30, 251};
                    float u = -len * 0.5f + (k + 0.5f) * len / n;
                    quadFacing(m, P(u - 0.65f, y), P(u + 0.65f, y), P(u + 0.65f, y + 1.2f), P(u - 0.65f, y + 1.2f), c, out);
                }
            }
        }
    }
}

void town(const Terrain& t, const WorldLayout& L, const Town& tw, BuildingStyle style, std::vector<Dressing::Piece>& out) {
    Palette pal = paletteFor(style);
    Piecer pc{&out};
    // road points near the town (buildings keep clear of them)
    std::vector<Vec3> nearRoad;
    for (const Polyline& r : L.roads)
        for (const Vec3& p : r.pts) if (std::hypot(p.x - tw.center.x, p.z - tw.center.z) < tw.radius + 40) nearRoad.push_back(p);
    for (const Vec3& p : L.railway.pts) if (std::hypot(p.x - tw.center.x, p.z - tw.center.z) < tw.radius + 40) nearRoad.push_back(p);
    const double cell = tw.kind == TownKind::City ? 24.0 : 28.0;
    int n = static_cast<int>(tw.radius / cell);
    for (int j = -n; j <= n; ++j)
        for (int i = -n; i <= n; ++i) {
            unsigned h = hash2(i + static_cast<int>(tw.center.x), j + static_cast<int>(tw.center.z));
            double r01 = (h & 1023) / 1023.0, r02 = ((h >> 10) & 1023) / 1023.0, r03 = ((h >> 20) & 1023) / 1023.0;
            double x = tw.center.x + i * cell + (r01 - 0.5) * 5.0, z = tw.center.z + j * cell + (r02 - 0.5) * 5.0;
            // streets every fourth row/column, ragged outline, thinning towards the edge
            if ((i % 4 + 4) % 4 == 0 || (j % 4 + 4) % 4 == 0) continue;
            double ang = std::atan2(z - tw.center.z, x - tw.center.x);
            double edge = 0.78 + 0.22 * std::sin(ang * 3.0 + tw.center.x * 0.001) * std::cos(ang * 5.0 + tw.center.z * 0.001);
            double dc = std::hypot(x - tw.center.x, z - tw.center.z) / (tw.radius * edge);
            if (dc > 1.0) continue;
            if (r03 < dc * dc * 0.7) continue;
            bool clear = true;
            for (const Vec3& p : nearRoad) if (std::hypot(p.x - x, p.z - z) < 13.0) { clear = false; break; }
            if (!clear) continue;
            double w = 9.0 + r01 * 9.0, d = 8.0 + r02 * 9.0;
            double h0 = t.height(x - w / 2, z - d / 2), h1 = t.height(x + w / 2, z - d / 2), h2 = t.height(x - w / 2, z + d / 2), h3 = t.height(x + w / 2, z + d / 2);
            double lo = std::min(std::min(h0, h1), std::min(h2, h3)), hiH = std::max(std::max(h0, h1), std::max(h2, h3));
            if (hiH - lo > 5.0 || lo < 1.5) continue;
            double hgt;
            if (tw.kind == TownKind::City && dc < 0.32) hgt = 18.0 + r03 * 42.0;          // downtown towers
            else if (tw.kind == TownKind::Village) hgt = 3.5 + r03 * 3.5;
            else hgt = 5.0 + r03 * 9.0;
            if (tw.kind == TownKind::Industrial) { w *= 1.6; d *= 1.3; hgt = 6.0 + r03 * 5.0; }
            Color wall = pal.walls[h % 4];
            if (tw.kind == TownKind::Industrial) wall = Color{168, 170, 172, 255};
            float base = static_cast<float>(lo - 1.0);
            float yaw = static_cast<float>(((h >> 7) % 4) * 0.0 + (r02 - 0.5) * 0.15);
            Matrix xf = MatrixMultiply(MatrixRotateY(yaw), MatrixTranslate(static_cast<float>(x), 0, static_cast<float>(z)));
            float H = static_cast<float>(hgt + (hiH - lo) + 1.0);
            pc.mb.box({0, base + H * 0.5f, 0}, {static_cast<float>(w), H, static_cast<float>(d)}, wall, xf);
            pc.grow(Vector3{static_cast<float>(x), base + H, static_cast<float>(z)});
            pc.grow(Vector3{static_cast<float>(x), base, static_cast<float>(z)});
            windows(pc.mb, xf, static_cast<float>(w), static_cast<float>(d), static_cast<float>(hiH), base + H,
                    hgt > 25.0, tw.kind == TownKind::Industrial, h);
            Color roof = pal.roof[(h >> 3) % 3];
            float top = base + H;
            if (pal.pitched && hgt < 16.0 && tw.kind != TownKind::Industrial) {
                // gable roof along the longer side
                float rw2 = static_cast<float>(w * 0.55), rd2 = static_cast<float>(d * 0.55), rh = static_cast<float>(std::min(w, d) * (style == BuildingStyle::Alpine ? 0.55 : 0.35));
                Vector3 p0 = Vector3Transform({-rw2, top, -rd2}, xf), p1 = Vector3Transform({rw2, top, -rd2}, xf);
                Vector3 p2 = Vector3Transform({rw2, top, rd2}, xf), p3 = Vector3Transform({-rw2, top, rd2}, xf);
                Vector3 r0 = Vector3Transform({-rw2, top + rh, 0}, xf), r1 = Vector3Transform({rw2, top + rh, 0}, xf);
                quadFacing(pc.mb, p0, p1, r1, r0, roof, Vector3Subtract(Vector3Transform({0, top, -10}, xf), Vector3Transform({0, top, 0}, xf)));
                quadFacing(pc.mb, p3, p2, r1, r0, roof, Vector3Subtract(Vector3Transform({0, top, 10}, xf), Vector3Transform({0, top, 0}, xf)));
                pc.mb.tri2(p0, r0, p3, wall);
                pc.mb.tri2(p1, r1, p2, wall);
            } else {
                pc.mb.box({0, top + 0.5f, 0}, {static_cast<float>(w) * 0.98f, 1.0f, static_cast<float>(d) * 0.98f}, roof, xf);
                if (((h >> 12) & 7) == 0) pc.mb.cylinder({static_cast<float>(w * 0.2), top + 1.0f, 0}, 1.1f, 2.0f, 8, Color{190, 190, 186, 255}, xf);   // rooftop tank
            }
            // gardens: a tree beside some houses
            if (((h >> 15) & 3) == 0 && tw.kind != TownKind::Industrial) {
                double tx = x + w * 0.5 + 3.5, tz = z + d * 0.3;
                float ty = static_cast<float>(t.height(tx, tz));
                Vector3 tb = {static_cast<float>(tx), ty, static_cast<float>(tz)};
                pc.mb.cylinder(tb, 0.35f, 4.0f, 5, Color{96, 74, 52, 255});
                Color leaf = style == BuildingStyle::Desert ? Color{92, 116, 60, 255} : Color{60, 96, 46, 255};
                if (style == BuildingStyle::Desert) pc.mb.cylinder({tb.x, ty + 5.5f, tb.z}, 3.2f, 0.9f, 6, leaf, MatrixIdentity(), 0.4f);   // palm crown
                else pc.mb.cylinder({tb.x, ty + 2.5f, tb.z}, 3.0f, 7.0f, 7, leaf, MatrixIdentity(), 0.0f);
            }
            if (pc.mb.vertexCount() > 60000) pc.flush();
        }
    pc.flush();
}

}  // namespace

void Dressing::build(const Terrain& t, const WorldLayout& L, BuildingStyle style, bool skipFirstTown) {
    unload();
    layout = &L;
    for (const Polyline& r : L.roads) corridor(t, r, false, statics);
    if (!L.railway.pts.empty()) corridor(t, L.railway, true, statics);
    for (size_t i = 0; i < L.towns.size(); ++i) {
        if (i == 0 && skipFirstTown) continue;   // the hand-built desert town already stands there
        town(t, L, L.towns[i], style, statics);
    }
    // industrial yard extras: warehouses and storage tanks around the factory slots
    if (!L.factories.empty()) {
        Piecer pc{&statics};
        for (size_t k = 0; k < L.factories.size(); ++k) {
            const Vec3& f = L.factories[k];
            Matrix xf = MatrixTranslate(static_cast<float>(f.x), 0, static_cast<float>(f.z));
            float g = static_cast<float>(f.y);
            pc.mb.box({0, g - 0.4f, 0}, {120.0f, 1.0f, 70.0f}, Color{128, 126, 120, 255}, xf);   // concrete yard
            chimneyTops.push_back({static_cast<float>(f.x - 24.0), g + 38.0f, static_cast<float>(f.z - 20.0)});
            pc.grow({static_cast<float>(f.x), g, static_cast<float>(f.z)});
        }
        const Vec3& f0 = L.factories[0];
        for (int k = 0; k < 4; ++k) {
            float x = static_cast<float>(f0.x + 260 + k * 22), z = static_cast<float>(f0.z - 60);
            float g = static_cast<float>(t.height(x, z));
            pc.mb.cylinder({x, g - 1.0f, z}, 8.0f, 13.0f, 18, Color{220, 220, 214, 255});
            pc.grow({x, g + 12, z});
        }
        pc.flush();
    }
    // army compound, forward bases, street furniture, yards and ports
    buildSceneProps(t, L, style == BuildingStyle::Desert, statics, beacons);
    for (const LightPoint& lp : L.lights)
        if (lp.kind == LightKind::Beacon) beacons.push_back(V(lp.pos));
    // wind turbines: merged towers + nacelles; rotors drawn separately so they can spin
    if (!L.turbines.empty()) {
        Piecer pc{&statics};
        for (const WindTurbine& w : L.turbines) {
            float g = static_cast<float>(t.height(w.pos.x, w.pos.z));
            Vector3 b = {static_cast<float>(w.pos.x), g - 1.0f, static_cast<float>(w.pos.z)};
            pc.mb.cylinder(b, 2.4f, 78.0f, 12, Color{232, 232, 228, 255}, MatrixIdentity(), 1.4f);
            float yaw = static_cast<float>(w.heading);
            Matrix xf = MatrixMultiply(MatrixRotateY(-yaw), MatrixTranslate(b.x, b.y + 78.0f, b.z));
            pc.mb.box({0.5f, 1.4f, 0}, {9.0f, 3.2f, 3.2f}, Color{226, 226, 222, 255}, xf);
            rotorHubs.push_back(Vector3Transform({5.4f, 1.4f, 0}, xf));
            rotorYaw.push_back(yaw);
            pc.grow(b); pc.grow({b.x, b.y + 120, b.z});
        }
        pc.flush();
        MeshBuilder r;
        for (int k = 0; k < 3; ++k) {
            float a = 2.0f * PI * k / 3.0f;
            Matrix xf = MatrixRotateX(a);
            Vector3 p0 = Vector3Transform({0, 1.0f, -1.1f}, xf), p1 = Vector3Transform({0, 1.0f, 1.1f}, xf);
            Vector3 p2 = Vector3Transform({0.3f, 40.0f, 0.35f}, xf), p3 = Vector3Transform({0.3f, 40.0f, -0.35f}, xf);
            r.quad(p0, p1, p2, p3, Color{236, 236, 232, 255});
            r.quad(p0, p3, p2, p1, Color{236, 236, 232, 255});
        }
        r.box({0.6f, 0, 0}, {2.4f, 2.0f, 2.0f}, Color{230, 230, 226, 255});
        rotor = r.build();
    }
}

void Dressing::unload() {
    for (Piece& p : statics) if (p.valid) UnloadMesh(p.mesh);
    statics.clear();
    if (rotor.vertexCount) UnloadMesh(rotor);
    rotor = Mesh{};
    rotorHubs.clear();
    rotorYaw.clear();
    chimneyTops.clear();
    beacons.clear();
}

}  // namespace sky
