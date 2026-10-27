// Meshes for the infrastructure in the world layout: lattice pylons and sagging conductors
// (the wires are their own pieces with a shorter draw distance), substations, the hydro dam and
// its powerhouse, water works, and the metro viaduct with its stations. All static and merged.
#include "Dressing.hpp"

#include "MeshBuilder.hpp"
#include "raymath.h"

#include <algorithm>
#include <cmath>

namespace sky {

namespace {

Vector3 V(const Vec3& v) { return {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)}; }

struct Piecer {
    std::vector<Dressing::Piece>* out;
    float distScale = 1.0f;
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
        p.distScale = distScale;
        out->push_back(p);
        mb = MeshBuilder{};
        lo = {1e9f, 1e9f, 1e9f};
        hi = {-1e9f, -1e9f, -1e9f};
    }
};

/// A box from a to b (a beam), `tw` x `th` in cross-section.
void beam(MeshBuilder& m, Vector3 a, Vector3 b, float tw, float th, Color c) {
    Vector3 d = Vector3Subtract(b, a);
    float len = Vector3Length(d);
    if (len < 1e-3f) return;
    Vector3 x = Vector3Scale(d, 1.0f / len);
    Vector3 ref = std::fabs(x.y) > 0.9f ? Vector3{1, 0, 0} : Vector3{0, 1, 0};
    Vector3 z = Vector3Normalize(Vector3CrossProduct(x, ref));
    Vector3 y = Vector3CrossProduct(z, x);
    Vector3 o = Vector3Scale(Vector3Add(a, b), 0.5f);
    Matrix M = {x.x, y.x, z.x, o.x,
                x.y, y.y, z.y, o.y,
                x.z, y.z, z.z, o.z,
                0, 0, 0, 1};
    m.box({0, 0, 0}, {len, th, tw}, c, M);
}

const Color kSteel = {150, 154, 158, 255};
const Color kSteelDark = {104, 108, 112, 255};
const Color kInsulator = {70, 92, 104, 255};
const Color kConcrete = {178, 174, 166, 255};
const Color kConcreteDark = {136, 132, 126, 255};
const Color kGravel = {132, 128, 120, 255};

// arm heights and half spans (conductor attachment points), and the earth-wire peak
constexpr float kArmY[3] = {22.0f, 27.5f, 33.0f};
constexpr float kArmX[3] = {7.5f, 9.0f, 6.5f};
constexpr float kPeak = 38.0f;
constexpr float kDrop = 2.6f;   // insulator string length

/// A lattice tower at `g` facing across the line (`across` = unit vector along the arms).
void pylon(MeshBuilder& m, Vector3 g, Vector3 along, Vector3 across) {
    auto P = [&](float u, float y, float v) {   // u along the arms, v along the line
        return Vector3Add(g, Vector3Add(Vector3Add(Vector3Scale(across, u), Vector3Scale(along, v)), Vector3{0, y, 0}));
    };
    const float b0 = 3.6f, b1 = 1.0f, top = kArmY[2] + 1.0f;
    auto half = [&](float y) { return b0 + (b1 - b0) * std::min(1.0f, y / top); };
    // four legs
    for (int k = 0; k < 4; ++k) {
        float su = (k & 1) ? 1.0f : -1.0f, sv = (k & 2) ? 1.0f : -1.0f;
        beam(m, P(su * b0, -1.0f, sv * b0), P(su * b1, top, sv * b1), 0.35f, 0.35f, kSteel);
    }
    // bracing rings and X bracing on the four faces
    const float levels[6] = {0.0f, 6.0f, 12.0f, 17.5f, kArmY[0], top};
    auto corner = [&](int k, float y) {
        static const float su[4] = {-1, 1, 1, -1}, sv[4] = {-1, -1, 1, 1};
        float h = half(y);
        return P(su[k & 3] * h, y, sv[k & 3] * h);
    };
    for (int l = 0; l < 6; ++l)
        for (int f = 0; f < 4; ++f) {
            Vector3 p0 = corner(f, levels[l]), p1 = corner(f + 1, levels[l]);
            beam(m, p0, p1, 0.18f, 0.18f, kSteelDark);
            if (l + 1 < 6) {
                Vector3 q0 = corner(f, levels[l + 1]), q1 = corner(f + 1, levels[l + 1]);
                beam(m, p0, q1, 0.14f, 0.14f, kSteelDark);
                beam(m, p1, q0, 0.14f, 0.14f, kSteelDark);
            }
        }
    // cross arms (tapered trusses) with hanging insulator strings
    for (int a = 0; a < 3; ++a) {
        float y = kArmY[a], hw = half(y);
        for (int s = -1; s <= 1; s += 2) {
            Vector3 root0 = P(s * hw, y, -hw * 0.8f), root1 = P(s * hw, y, hw * 0.8f), tip = P(s * kArmX[a], y, 0);
            Vector3 rootUp = P(s * hw, y + 2.2f, 0);
            beam(m, root0, tip, 0.2f, 0.2f, kSteel);
            beam(m, root1, tip, 0.2f, 0.2f, kSteel);
            beam(m, rootUp, tip, 0.16f, 0.16f, kSteelDark);
            m.cylinder(Vector3Subtract(tip, Vector3{0, kDrop, 0}), 0.22f, kDrop, 6, kInsulator);
        }
    }
    // earth-wire peak
    beam(m, P(-b1, top, 0), P(0, kPeak, 0), 0.2f, 0.2f, kSteel);
    beam(m, P(b1, top, 0), P(0, kPeak, 0), 0.2f, 0.2f, kSteel);
}

/// One sagging conductor from a to b (a parabola close to the catenary), as points.
void wire(Dressing& D, Vector3 a, Vector3 b, float sag) {
    for (int i = 0; i < Dressing::kWirePoints; ++i) {
        float f = static_cast<float>(i) / (Dressing::kWirePoints - 1);
        Vector3 p = Vector3Lerp(a, b, f);
        p.y -= sag * 4.0f * f * (1.0f - f);
        D.wirePts.push_back(p);
    }
}

void powerLines(const Terrain& t, const WorldLayout& L, Dressing& D) {
    std::vector<Dressing::Piece>& out = D.statics;
    for (const PowerLine& pl : L.powerLines) {
        Piecer towers{&out, 0.55f};
        size_t n = pl.pylons.size();
        std::vector<Vector3> across(n), along(n);
        for (size_t i = 0; i < n; ++i) {
            Vec3 a = pl.pylons[i > 0 ? i - 1 : i], b = pl.pylons[i + 1 < n ? i + 1 : i];
            Vec3 d = normalize(Vec3{b.x - a.x, 0, b.z - a.z});
            along[i] = V(d);
            across[i] = {static_cast<float>(-d.z), 0, static_cast<float>(d.x)};
        }
        for (size_t i = 0; i < n; ++i) {
            Vector3 g = V(pl.pylons[i]);
            g.y = static_cast<float>(t.height(pl.pylons[i].x, pl.pylons[i].z));
            pylon(towers.mb, g, along[i], across[i]);
            towers.grow(g);
            towers.grow(Vector3Add(g, Vector3{0, kPeak, 0}));
            if (i + 1 < n) {
                Vector3 g2 = V(pl.pylons[i + 1]);
                g2.y = static_cast<float>(t.height(pl.pylons[i + 1].x, pl.pylons[i + 1].z));
                float span = Vector3Distance(g, g2);
                float sag = std::clamp(span * 0.028f, 4.0f, 22.0f);
                // never sag into the ground between the towers
                for (int k = 1; k < 8; ++k) {
                    float f = k / 8.0f;
                    Vector3 p = Vector3Lerp(g, g2, f);
                    float gy = static_cast<float>(t.surfaceHeight(p.x, p.z));
                    float wireY = p.y + kArmY[0] - kDrop - sag * 4.0f * f * (1.0f - f);
                    if (wireY < gy + 8.0f) sag = std::max(0.5f, sag - (gy + 8.0f - wireY) / (4.0f * f * (1.0f - f)));
                }
                Dressing::WireSpan sp;
                sp.first = static_cast<int>(D.wirePts.size());
                sp.center = Vector3Add(Vector3Lerp(g, g2, 0.5f), Vector3{0, kArmY[1], 0});
                sp.radius = span * 0.5f + 20.0f;
                for (int a = 0; a < 3; ++a)
                    for (int s = -1; s <= 1; s += 2) {
                        Vector3 p0 = Vector3Add(g, Vector3Add(Vector3Scale(across[i], s * kArmX[a]), Vector3{0, kArmY[a] - kDrop, 0}));
                        Vector3 p1 = Vector3Add(g2, Vector3Add(Vector3Scale(across[i + 1], s * kArmX[a]), Vector3{0, kArmY[a] - kDrop, 0}));
                        wire(D, p0, p1, sag);
                    }
                wire(D, Vector3Add(g, Vector3{0, kPeak, 0}), Vector3Add(g2, Vector3{0, kPeak, 0}), sag * 0.8f);
                D.wireSpans.push_back(sp);
            }
            if ((i + 1) % 8 == 0) towers.flush();
        }
        towers.flush();
    }
}

void substation(MeshBuilder& m, Matrix xf, float g) {
    m.box({0, g - 0.3f, 0}, {74, 0.9f, 52}, kGravel, xf);
    // fence
    for (int s = -1; s <= 1; s += 2) {
        m.box({0, g + 1.2f, s * 26.0f}, {74, 2.4f, 0.15f}, Color{120, 124, 126, 255}, xf);
        m.box({s * 37.0f, g + 1.2f, 0}, {0.15f, 2.4f, 52}, Color{120, 124, 126, 255}, xf);
    }
    // transformers (with radiator fins) and switchgear gantries
    for (int k = 0; k < 3; ++k) {
        float x = -22.0f + k * 15.0f;
        m.box({x, g + 2.6f, -12}, {7.5f, 5.2f, 5.0f}, Color{122, 130, 120, 255}, xf);
        m.box({x, g + 2.4f, -15.2f}, {6.5f, 4.2f, 1.4f}, Color{100, 108, 100, 255}, xf);
        for (int b = -1; b <= 1; ++b) m.cylinder(Vector3Transform({x + b * 2.0f, g + 5.2f, -12}, xf), 0.25f, 2.6f, 6, kInsulator);
    }
    for (int k = 0; k < 4; ++k) {
        float x = -27.0f + k * 18.0f;
        for (int s = -1; s <= 1; s += 2) m.box({x, g + 7.0f, 6 + s * 7.0f}, {0.6f, 14.0f, 0.6f}, kSteel, xf);
        m.box({x, g + 13.6f, 6}, {0.6f, 0.6f, 15.0f}, kSteel, xf);
        for (int b = -1; b <= 1; ++b) m.cylinder(Vector3Transform({x, g + 11.0f, 6 + b * 4.0f}, xf), 0.22f, 2.4f, 6, kInsulator);
    }
    m.box({26, g + 2.5f, 16}, {14, 5.0f, 9}, Color{196, 188, 172, 255}, xf);   // control building
}

void waterWorks(MeshBuilder& m, Matrix xf, float g) {
    m.box({0, g - 0.3f, 0}, {130, 0.8f, 90}, Color{150, 150, 142, 255}, xf);
    // clarifiers: round tanks with water inside and a rotating bridge
    for (int k = 0; k < 3; ++k) {
        Vector3 c = Vector3Transform({-40.0f + k * 34.0f, g - 0.5f, -18}, xf);
        m.cylinder(c, 14.0f, 4.0f, 24, kConcrete);
        m.cylinder({c.x, c.y + 4.02f, c.z}, 13.2f, 0.05f, 24, Color{70, 96, 92, 255});
        beam(m, {c.x - 13, c.y + 4.6f, c.z}, {c.x + 13, c.y + 4.6f, c.z}, 1.2f, 0.6f, kSteel);
    }
    // filter beds and the treatment hall
    for (int k = 0; k < 4; ++k) {
        m.box({-38.0f + k * 15.0f, g + 1.2f, 22}, {13, 2.4f, 20}, kConcrete, xf);
        m.box({-38.0f + k * 15.0f, g + 2.42f, 22}, {12, 0.05f, 19}, Color{64, 90, 84, 255}, xf);
    }
    m.box({38, g + 5.0f, 18}, {34, 10.0f, 24}, Color{204, 198, 184, 255}, xf);
    m.box({38, g + 10.4f, 18}, {34.5f, 0.8f, 24.5f}, Color{110, 112, 116, 255}, xf);
    // elevated storage tank
    Vector3 tp = Vector3Transform({52, g, -26}, xf);
    for (int k = 0; k < 4; ++k) {
        float a = k * PI / 2 + PI / 4;
        beam(m, {tp.x + std::cos(a) * 5.0f, g, tp.z + std::sin(a) * 5.0f}, {tp.x + std::cos(a) * 3.2f, g + 22, tp.z + std::sin(a) * 3.2f}, 0.6f, 0.6f, kConcreteDark);
    }
    m.cylinder({tp.x, g + 22, tp.z}, 6.5f, 7.0f, 16, kConcrete);
}

void powerhouse(MeshBuilder& m, Matrix xf, float g) {
    m.box({0, g + 9.0f, 0}, {28, 22.0f, 58}, Color{190, 184, 170, 255}, xf);
    m.box({0, g + 20.4f, 0}, {28.5f, 1.0f, 58.5f}, Color{110, 112, 116, 255}, xf);
    for (int k = -1; k <= 1; ++k) m.box({14.2f, g + 2.0f, k * 16.0f}, {1.0f, 6.0f, 9.0f}, Color{60, 66, 70, 255}, xf);   // tailrace outlets
    for (int k = 0; k < 3; ++k) m.box({-4, g + 24.0f, -18.0f + k * 18.0f}, {6, 6.0f, 6}, Color{122, 130, 120, 255}, xf);   // transformers on the roof
}

/// A concrete gravity dam: vertical upstream face, a sloped downstream face, crest road, a
/// stepped spillway with white water in the middle.
void dam(const Terrain& t, const Dam& D, MeshBuilder& m) {
    Vec3 axis = Vec3{D.b.x - D.a.x, 0, D.b.z - D.a.z};
    double len = std::hypot(axis.x, axis.z);
    if (len < 10) return;
    axis = axis * (1.0 / len);
    Vec3 dn = D.downstream;
    float crest = static_cast<float>(D.a.y);
    int n = std::max(2, static_cast<int>(len / 8.0));
    struct Col { Vector3 up0, up1, dnTop, dnBase; bool ok; };
    std::vector<Col> cols(n + 1);
    for (int i = 0; i <= n; ++i) {
        double s = len * i / n;
        Vec3 p = D.a + axis * s;
        float g = static_cast<float>(std::min(t.height(p.x, p.z), t.surfaceHeight(p.x, p.z)));
        float H = crest - g;
        Col c;
        c.ok = H > 0.5f;
        float base = g - 4.0f;
        float foot = 5.0f + std::max(0.0f, H) * 0.72f;
        c.up0 = V(Vec3{p.x, base, p.z} - dn * 5.0);
        c.up1 = V(Vec3{p.x, crest, p.z} - dn * 5.0);
        c.dnTop = V(Vec3{p.x, crest, p.z} + dn * 5.0);
        c.dnBase = V(Vec3{p.x, base, p.z} + dn * foot);
        cols[i] = c;
    }
    Vector3 up = {0, 1, 0};
    Vector3 vdn = V(dn), vup = Vector3Negate(vdn);
    for (int i = 0; i < n; ++i) {
        const Col &a = cols[i], &b = cols[i + 1];
        if (!a.ok && !b.ok) continue;
        double mid = (i + 0.5) / n;
        bool spill = mid > 0.4 && mid < 0.6;
        Color face = spill ? Color{226, 234, 238, 255} : kConcrete;
        m.quad(a.up0, b.up0, b.up1, a.up1, kConcreteDark);
        m.quad(a.up0, a.up1, b.up1, b.up0, kConcreteDark);   // both windings: seen from the reservoir side too
        m.quad(a.up1, b.up1, b.dnTop, a.dnTop, kConcreteDark);
        m.tri2(a.dnTop, b.dnTop, b.dnBase, face);
        m.tri2(a.dnTop, b.dnBase, a.dnBase, face);
        (void)up; (void)vup;
        // parapets along the crest road
        for (float off : {-4.6f, 4.6f}) {
            Vector3 o = Vector3Scale(vdn, off);
            Vector3 p0 = Vector3Add(a.up1, Vector3Add(Vector3Scale(vdn, 5.0f), o)), p1 = Vector3Add(b.up1, Vector3Add(Vector3Scale(vdn, 5.0f), o));
            beam(m, Vector3Add(p0, Vector3{0, 0.6f, 0}), Vector3Add(p1, Vector3{0, 0.6f, 0}), 0.4f, 1.2f, kConcrete);
        }
        if (spill && (i % 2 == 0)) {
            // spillway gate piers
            Vector3 c = Vector3Lerp(a.up1, a.dnTop, 0.5f);
            m.box({c.x, c.y + 3.0f, c.z}, {3.0f, 6.0f, 3.0f}, kConcreteDark);
        }
    }
}

/// Elevated metro: a box-girder deck on single columns with hammerhead caps, two tracks, and a
/// station every ~1.3 km.
void metroViaduct(const Terrain& t, const Polyline& p, std::vector<Dressing::Piece>& out) {
    Piecer pc{&out, 1.0f};
    const float hw = 5.2f;
    double lastPier = -1e9, lastStation = -1300, pieceStart = 0;
    bool lastDone = false;
    for (size_t i = 0; i + 1 < p.pts.size(); ++i) {
        const Vec3 &a = p.pts[i], &b = p.pts[i + 1];
        Vec3 d = normalize(Vec3{b.x - a.x, 0, b.z - a.z});
        Vec3 nrm{-d.z, 0, d.x};
        Vector3 A = V(a), B = V(b);
        // deck: a box girder from a to b
        beam(pc.mb, Vector3{A.x, A.y - 1.0f, A.z}, Vector3{B.x, B.y - 1.0f, B.z}, hw * 2.0f, 2.0f, kConcrete);
        for (float s : {-1.0f, 1.0f}) {
            Vector3 o = V(nrm * (s * (hw - 0.2)));
            beam(pc.mb, Vector3Add(Vector3{A.x, A.y + 0.6f, A.z}, o), Vector3Add(Vector3{B.x, B.y + 0.6f, B.z}, o), 0.3f, 1.2f, Color{200, 196, 188, 255});
        }
        for (float off : {-3.15f, -1.65f, 1.65f, 3.15f}) {
            Vector3 o = V(nrm * off);
            beam(pc.mb, Vector3Add(Vector3{A.x, A.y + 0.1f, A.z}, o), Vector3Add(Vector3{B.x, B.y + 0.1f, B.z}, o), 0.14f, 0.2f, Color{80, 76, 72, 255});
        }
        pc.grow(A); pc.grow(B);
        if (p.cum[i] - lastPier >= 30.0) {
            lastPier = p.cum[i];
            float g = static_cast<float>(t.height(a.x, a.z));
            if (A.y - 2.0f - g > 2.0f && !t.isWater(a.x, a.z)) {
                pc.mb.cylinder({A.x, g - 1.0f, A.z}, 1.2f, A.y - 3.2f - g + 1.0f, 10, kConcreteDark);
                beam(pc.mb, Vector3Add(Vector3{A.x, A.y - 2.6f, A.z}, V(nrm * -4.5)), Vector3Add(Vector3{A.x, A.y - 2.6f, A.z}, V(nrm * 4.5)), 2.4f, 1.4f, kConcreteDark);
            } else if (t.isWater(a.x, a.z)) {
                float wy = static_cast<float>(t.surfaceHeight(a.x, a.z));
                pc.mb.cylinder({A.x, wy - 6.0f, A.z}, 1.5f, A.y - 3.2f - wy + 6.0f, 10, kConcreteDark);
            }
        }
        bool endStation = !lastDone && p.length() - p.cum[i] < 110.0;
        if (endStation || (p.cum[i] - lastStation >= 1300.0 && p.cum[i] > 100 && p.length() - p.cum[i] > 700)) {
            if (endStation) lastDone = true;
            lastStation = p.cum[i];
            float yaw = static_cast<float>(std::atan2(d.z, d.x));
            Matrix xf = MatrixMultiply(MatrixRotateY(-yaw), MatrixTranslate(A.x, A.y, A.z));
            for (float s : {-1.0f, 1.0f}) pc.mb.box({0, 0.5f, s * 7.6f}, {95, 1.0f, 4.4f}, Color{190, 186, 180, 255}, xf);   // platforms
            pc.mb.box({0, 7.4f, 0}, {100, 0.8f, 21}, Color{70, 120, 150, 255}, xf);                                         // canopy roof
            for (int k = -4; k <= 4; ++k)
                for (float s : {-1.0f, 1.0f}) pc.mb.box({k * 11.0f, 4.0f, s * 9.6f}, {0.5f, 6.8f, 0.5f}, kSteel, xf);
            float g = static_cast<float>(t.height(a.x, a.z));
            pc.mb.box({0, (g - A.y) * 0.5f - 1.5f, 13.5f}, {12, A.y - g - 3.0f, 6}, Color{200, 194, 182, 255}, xf);       // stair and lift tower
        }
        if (p.cum[i + 1] - pieceStart > 900.0) { pc.flush(); pieceStart = p.cum[i + 1]; }
    }
    pc.flush();
}

}  // namespace

void buildInfrastructureMeshes(const Terrain& t, const WorldLayout& L, Dressing& D) {
    std::vector<Dressing::Piece>& out = D.statics;
    std::vector<Vector3>& beacons = D.beacons;
    powerLines(t, L, D);
    if (L.metro.pts.size() >= 2) metroViaduct(t, L.metro, out);
    Piecer pc{&out, 1.0f};
    for (const Facility& f : L.facilities) {
        float g = static_cast<float>(f.pos.y);
        Matrix xf = MatrixMultiply(MatrixRotateY(static_cast<float>(-f.yaw)), MatrixTranslate(static_cast<float>(f.pos.x), 0, static_cast<float>(f.pos.z)));
        switch (f.kind) {
            case FacilityKind::Substation: substation(pc.mb, xf, g); break;
            case FacilityKind::WaterWorks: waterWorks(pc.mb, xf, g); beacons.push_back({static_cast<float>(f.pos.x), g + 30, static_cast<float>(f.pos.z)}); break;
            case FacilityKind::Powerhouse: powerhouse(pc.mb, xf, g); break;
        }
        pc.grow({static_cast<float>(f.pos.x), g, static_cast<float>(f.pos.z)});
        pc.grow({static_cast<float>(f.pos.x), g + 30, static_cast<float>(f.pos.z)});
        pc.flush();
    }
    for (const Dam& d : L.dams) {
        dam(t, d, pc.mb);
        pc.grow(V(d.a)); pc.grow(V(d.b));
        pc.grow({static_cast<float>(d.a.x), static_cast<float>(d.bed - 5), static_cast<float>(d.a.z)});
        pc.flush();
    }
}

}  // namespace sky
