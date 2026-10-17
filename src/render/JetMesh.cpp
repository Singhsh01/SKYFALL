#include "JetMesh.hpp"

#include "MeshBuilder.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace sky {

namespace {

constexpr unsigned char kPaint = 255, kMetal = 128, kGlass = 0;

struct Section { float x, w, ht, hb, chine; };
struct Planform {
    float rootLE, rootTE, tipLE, tipTE;  // x positions
    float rootZ, tipZ;                   // spanwise positions (right side)
    float y, tipDy;                      // vertical position and tip offset (dihedral)
    float thick;                         // root thickness
};
struct TailDef { float rootLE, rootTE, tipLE, tipTE, height, z, y, cant; };

struct Geo {
    std::vector<Section> fus;
    float canX0, canX1, canW, canH, canY;
    Planform wing;
    bool lerx = false; Planform lerxP{};
    bool stabs = true; Planform stab{};
    bool canards = false; Planform canard{};
    std::vector<TailDef> tails;
    int engines = 2; float engineZ = 0.7f, nozzleX = -9.0f, nozzleR = 0.55f, nozzleLen = 1.1f; bool squareNozzle = false;
    bool nacelles = false; float nacX0 = 0, nacX1 = 0, nacR = 0.7f, nacZ = 1.3f, nacY = -0.5f;
    int pylonMissiles = 0;
    float noseGearX = 5.5f, mainGearX = -1.5f, mainGearZ = 1.6f;
    float intakeX = 3.0f, intakeZ = 1.5f, intakeW = 0.9f, intakeH = 1.0f, intakeY = -0.2f; bool ventral = false;
    bool faceted = false;
    float pu = 2.4f, pl = 2.6f;   // superellipse exponents of the upper / lower fuselage section
    bool roundNozzle = true;
};

Color paint(unsigned char shade = 255) { return {shade, shade, shade, kPaint}; }
Color metal(unsigned char r, unsigned char g, unsigned char b) { return {r, g, b, kMetal}; }

/// Emit triangle oriented so that its normal points away from `inside`.
void triOut(MeshBuilder& mb, Vector3 a, Vector3 b, Vector3 c, Vector3 inside, Color col) {
    Vector3 n = Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(c, a));
    Vector3 cen = Vector3Scale(Vector3Add(Vector3Add(a, b), c), 1.0f / 3.0f);
    if (Vector3DotProduct(n, Vector3Subtract(cen, inside)) < 0) mb.tri(a, c, b, col);
    else mb.tri(a, b, c, col);
}
void quadOut(MeshBuilder& mb, Vector3 a, Vector3 b, Vector3 c, Vector3 d, Vector3 inside, Color col) {
    triOut(mb, a, b, c, inside, col);
    triOut(mb, a, c, d, inside, col);
}

std::vector<Vector2> sectionRing(const Section& s, float pu, float pl) {
    // (z, y) points of a closed superellipse section with a chine line at y = s.chine.
    // Higher exponents give squarer, "stealthy" sections; the chine reads as a crisp edge.
    const int R = 40;
    std::vector<Vector2> r;
    r.reserve(R);
    for (int k = 0; k < R; ++k) {
        float th = 2.0f * PI * k / R;
        float c = cosf(th), sn = sinf(th);
        bool upper = sn >= 0.0f;
        float p = upper ? pu : pl;
        float z = s.w * (c >= 0 ? 1.0f : -1.0f) * powf(fabsf(c), 2.0f / p);
        float y = upper ? s.chine + (s.ht - s.chine) * powf(fabsf(sn), 2.0f / p)
                        : s.chine - (s.hb + s.chine) * powf(fabsf(sn), 2.0f / p);
        r.push_back({z, y});
    }
    return r;
}

/// Catmull-Rom resampling of fuselage stations for a smooth longitudinal profile.
std::vector<Section> smoothStations(const std::vector<Section>& in, int sub) {
    std::vector<Section> out;
    auto cr = [](float p0, float p1, float p2, float p3, float t) {
        return 0.5f * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t * t + (-p0 + 3 * p1 - 3 * p2 + p3) * t * t * t);
    };
    for (size_t i = 0; i + 1 < in.size(); ++i) {
        const Section& a = in[i > 0 ? i - 1 : i];
        const Section& b = in[i];
        const Section& c = in[i + 1];
        const Section& d = in[i + 2 < in.size() ? i + 2 : i + 1];
        for (int k = 0; k < sub; ++k) {
            float t = float(k) / sub;
            Section q;
            q.x = b.x + (c.x - b.x) * t;
            q.w = std::max(0.015f, cr(a.w, b.w, c.w, d.w, t));
            q.ht = std::max(0.015f, cr(a.ht, b.ht, c.ht, d.ht, t));
            q.hb = std::max(0.015f, cr(a.hb, b.hb, c.hb, d.hb, t));
            q.chine = cr(a.chine, b.chine, c.chine, d.chine, t);
            out.push_back(q);
        }
    }
    out.push_back(in.back());
    return out;
}

/// Smooth-shaded loft through cross sections (rings of equal size).
void loft(MeshBuilder& mb, const std::vector<float>& xs, const std::vector<std::vector<Vector2>>& rings, Color col,
          float axisY = 0.0f, float axisZ = 0.0f, bool capEnd = true, bool faceted = false) {
    size_t S = xs.size(), R = rings[0].size();
    auto P = [&](size_t s, size_t i) {
        const Vector2& p = rings[s][i % R];
        return Vector3{xs[s], p.y + axisY, p.x + axisZ};
    };
    auto N = [&](size_t s, size_t i) {
        size_t s0 = s > 0 ? s - 1 : s, s1 = s + 1 < S ? s + 1 : s;
        Vector3 ds = Vector3Subtract(P(s1, i), P(s0, i));
        Vector3 di = Vector3Subtract(P(s, i + 1), P(s, i + R - 1));
        Vector3 n = Vector3Normalize(Vector3CrossProduct(ds, di));
        Vector3 out = {0, P(s, i).y - axisY, P(s, i).z - axisZ};
        if (Vector3DotProduct(n, out) < 0) n = Vector3Negate(n);
        return n;
    };
    for (size_t s = 0; s + 1 < S; ++s)
        for (size_t i = 0; i < R; ++i) {
            Vector3 a = P(s, i), b = P(s, i + 1), c = P(s + 1, i + 1), d = P(s + 1, i);
            Vector3 na = N(s, i), nb = N(s, i + 1), nc = N(s + 1, i + 1), nd = N(s + 1, i);
            // orient by face normal vs outward
            Vector3 fn = Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(c, a));
            Vector3 cen = Vector3Scale(Vector3Add(a, c), 0.5f);
            Vector3 out = {0, cen.y - axisY, cen.z - axisZ};
            if (faceted) {
                if (Vector3DotProduct(fn, out) >= 0) { mb.tri(a, b, c, col); mb.tri(a, c, d, col); }
                else { mb.tri(a, c, b, col); mb.tri(a, d, c, col); }
                continue;
            }
            if (Vector3DotProduct(fn, out) >= 0) { mb.triN(a, b, c, na, nb, nc, col); mb.triN(a, c, d, na, nc, nd, col); }
            else { mb.triN(a, c, b, na, nc, nb, col); mb.triN(a, d, c, na, nd, nc, col); }
        }
    if (capEnd) {
        Vector3 cen = {xs[S - 1], axisY, axisZ};
        Vector3 inside = {xs[S - 1] + 1.0f, axisY, axisZ};
        for (size_t i = 0; i < R; ++i) triOut(mb, cen, P(S - 1, i), P(S - 1, i + 1), inside, col);
    }
}

/// Smooth airfoil-section lifting surface between a root chord (Rle-Rte) and a tip chord
/// (Tle-Tte). `thickDir` is the local "up" of the airfoil. Symmetric NACA 4-digit thickness
/// with slight camber, cosine chord spacing, smooth normals, closed tip.
void airfoilSurface(MeshBuilder& mb, Vector3 Rle, Vector3 Rte, Vector3 Tle, Vector3 Tte, Vector3 thickDir,
                    float tcRoot, float tcTip, Color col, Color under) {
    const int C = 14, S = 6;
    auto naca = [](float u, float tc) {
        return 5.0f * tc * (0.2969f * sqrtf(u) - 0.1260f * u - 0.3516f * u * u + 0.2843f * u * u * u - 0.1036f * u * u * u * u);
    };
    Vector3 td = Vector3Normalize(thickDir);
    std::vector<Vector3> top(C * S), bot(C * S);
    for (int si = 0; si < S; ++si) {
        float sv = float(si) / (S - 1);
        Vector3 le = Vector3Lerp(Rle, Tle, sv), te = Vector3Lerp(Rte, Tte, sv);
        float chord = Vector3Distance(le, te);
        float tc = tcRoot + (tcTip - tcRoot) * sv;
        for (int ci = 0; ci < C; ++ci) {
            float u = 0.5f - 0.5f * cosf(PI * ci / (C - 1));
            Vector3 mid = Vector3Lerp(le, te, u);
            float yt = naca(u, tc) * chord;
            float camber = 0.012f * chord * 4.0f * u * (1.0f - u);
            top[si * C + ci] = Vector3Add(mid, Vector3Scale(td, camber + yt));
            bot[si * C + ci] = Vector3Add(mid, Vector3Scale(td, camber - yt * 0.9f));
        }
    }
    auto normalAt = [&](const std::vector<Vector3>& P, int si, int ci, float side) {
        int s0 = std::max(si - 1, 0), s1 = std::min(si + 1, S - 1), c0 = std::max(ci - 1, 0), c1 = std::min(ci + 1, C - 1);
        Vector3 ds = Vector3Subtract(P[s1 * C + ci], P[s0 * C + ci]);
        Vector3 dc = Vector3Subtract(P[si * C + c1], P[si * C + c0]);
        Vector3 n = Vector3Normalize(Vector3CrossProduct(ds, dc));
        if (Vector3DotProduct(n, td) * side < 0) n = Vector3Negate(n);
        if (ci == 0) {  // leading edge: blend towards the forward direction
            Vector3 fwd = Vector3Normalize(Vector3Subtract(P[si * C], P[si * C + 1]));
            n = Vector3Normalize(Vector3Add(Vector3Scale(fwd, 0.8f), Vector3Scale(td, 0.2f * side)));
        }
        return n;
    };
    auto emitSurface = [&](const std::vector<Vector3>& P, float side, Color c) {
        for (int si = 0; si + 1 < S; ++si)
            for (int ci = 0; ci + 1 < C; ++ci) {
                Vector3 a = P[si * C + ci], b = P[si * C + ci + 1], cc = P[(si + 1) * C + ci + 1], d = P[(si + 1) * C + ci];
                Vector3 na = normalAt(P, si, ci, side), nb = normalAt(P, si, ci + 1, side);
                Vector3 nc = normalAt(P, si + 1, ci + 1, side), nd = normalAt(P, si + 1, ci, side);
                Vector3 fn = Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(cc, a));
                Vector3 avg = Vector3Add(Vector3Add(na, nb), Vector3Add(nc, nd));
                if (Vector3DotProduct(fn, avg) >= 0) { mb.triN(a, b, cc, na, nb, nc, c); mb.triN(a, cc, d, na, nc, nd, c); }
                else { mb.triN(a, cc, b, na, nc, nb, c); mb.triN(a, d, cc, na, nd, nc, c); }
            }
    };
    emitSurface(top, 1.0f, col);
    emitSurface(bot, -1.0f, under);
    // tip cap
    Vector3 spanDir = Vector3Normalize(Vector3Subtract(Vector3Lerp(Tle, Tte, 0.5f), Vector3Lerp(Rle, Rte, 0.5f)));
    Vector3 tipInside = Vector3Subtract(Vector3Lerp(Tle, Tte, 0.5f), spanDir);
    int last = (S - 1) * C;
    for (int ci = 0; ci + 1 < C; ++ci)
        quadOut(mb, top[last + ci], top[last + ci + 1], bot[last + ci + 1], bot[last + ci], tipInside, col);
}

void wingSurface(MeshBuilder& mb, const Planform& p, bool right, Color col) {
    float sgn = right ? 1.0f : -1.0f;
    float rootChord = std::max(0.1f, p.rootLE - p.rootTE);
    float tc = std::clamp(p.thick / rootChord, 0.025f, 0.09f);
    Color under = {static_cast<unsigned char>(col.r * 0.94f), static_cast<unsigned char>(col.g * 0.94f), static_cast<unsigned char>(col.b * 0.94f), col.a};
    airfoilSurface(mb, {p.rootLE, p.y, sgn * p.rootZ}, {p.rootTE, p.y, sgn * p.rootZ},
                   {p.tipLE, p.y + p.tipDy, sgn * p.tipZ}, {p.tipTE, p.y + p.tipDy, sgn * p.tipZ}, {0, 1, 0}, tc, tc * 0.7f, col, under);
}

/// Thin plate (tail fin / stabiliser / canard) with real thickness.
void plate(MeshBuilder& mb, Vector3 a, Vector3 b, Vector3 c, Vector3 d, Vector3 normal, float thick, Color col) {
    Vector3 o = Vector3Scale(Vector3Normalize(normal), thick * 0.5f);
    Vector3 A = Vector3Add(a, o), B = Vector3Add(b, o), C = Vector3Add(c, o), D = Vector3Add(d, o);
    Vector3 A2 = Vector3Subtract(a, o), B2 = Vector3Subtract(b, o), C2 = Vector3Subtract(c, o), D2 = Vector3Subtract(d, o);
    Vector3 cen = Vector3Scale(Vector3Add(Vector3Add(a, b), Vector3Add(c, d)), 0.25f);
    quadOut(mb, A, B, C, D, cen, col);
    quadOut(mb, A2, B2, C2, D2, cen, col);
    quadOut(mb, A, B, B2, A2, cen, col);
    quadOut(mb, B, C, C2, B2, cen, col);
    quadOut(mb, C, D, D2, C2, cen, col);
    quadOut(mb, D, A, A2, D2, cen, col);
}

void tail(MeshBuilder& mb, const TailDef& t, float side, Color col) {
    // fin plane: x along body, "up" tilted outward by cant; airfoil thickness across the plane
    float c = cosf(t.cant), s = sinf(t.cant);
    Vector3 up = {0, c, side * s};
    Vector3 base = {0, t.y, side * t.z};
    auto P = [&](float x, float h) { return Vector3{x, base.y + up.y * h, base.z + up.z * h}; };
    Vector3 n = {0, -up.z, up.y};
    if (n.z * side < 0 || (side == 0 && n.z < 0)) n = Vector3Negate(n);
    float chord = std::max(0.1f, t.rootLE - t.rootTE);
    airfoilSurface(mb, P(t.rootLE, -0.15f), P(t.rootTE, -0.15f), P(t.tipLE, t.height), P(t.tipTE, t.height), n,
                   std::clamp(0.16f / chord, 0.03f, 0.06f), 0.035f, col, col);
}

void tube(MeshBuilder& mb, float x0, float x1, float r0, float r1, float y, float z, int sides, Color col, bool square = false,
          float petals = 0.0f) {
    std::vector<float> xs = {x0, x0 + (x1 - x0) * 0.5f, x1};
    std::vector<std::vector<Vector2>> rings(3);
    for (int k = 0; k < 3; ++k) {
        float r = r0 + (r1 - r0) * (k * 0.5f);
        for (int i = 0; i < sides; ++i) {
            float a = 2 * PI * i / sides + (square ? PI / 4 : 0);
            float rr = r * (1.0f + (k > 0 ? petals * ((i % 2) ? 1.0f : -1.0f) * (k * 0.5f) : 0.0f));
            float cz = cosf(a) * rr, cy = sinf(a) * rr;
            if (square) { cz = (cz > 0 ? 1 : -1) * r; cy = (cy > 0 ? 1 : -1) * r * 0.75f; }
            rings[k].push_back({cz, cy});
        }
    }
    loft(mb, xs, rings, col, y, z, false);
}

/// Per-vertex ambient occlusion: voxelise the occluders, cast a fixed hemisphere of rays from
/// every vertex and darken the vertex colour (material id in alpha is preserved).
void bakeAmbientOcclusion(MeshBuilder& target, const std::vector<const MeshBuilder*>& occluders) {
    Vector3 lo = {1e9f, 1e9f, 1e9f}, hi = {-1e9f, -1e9f, -1e9f};
    for (const MeshBuilder* m : occluders)
        for (size_t i = 0; i < m->pos.size(); i += 3) {
            lo = Vector3Min(lo, {m->pos[i], m->pos[i + 1], m->pos[i + 2]});
            hi = Vector3Max(hi, {m->pos[i], m->pos[i + 1], m->pos[i + 2]});
        }
    const float vs = 0.12f;   // voxel size (m)
    lo = Vector3Subtract(lo, {0.5f, 0.5f, 0.5f});
    int nx = static_cast<int>((hi.x - lo.x) / vs) + 2, ny = static_cast<int>((hi.y - lo.y) / vs) + 2, nz = static_cast<int>((hi.z - lo.z) / vs) + 2;
    std::vector<unsigned char> vox(static_cast<size_t>(nx) * ny * nz, 0);
    auto mark = [&](Vector3 p) {
        int i = static_cast<int>((p.x - lo.x) / vs), j = static_cast<int>((p.y - lo.y) / vs), k = static_cast<int>((p.z - lo.z) / vs);
        if (i >= 0 && j >= 0 && k >= 0 && i < nx && j < ny && k < nz) vox[(static_cast<size_t>(k) * ny + j) * nx + i] = 1;
    };
    for (const MeshBuilder* m : occluders)
        for (size_t t = 0; t + 8 < m->pos.size(); t += 9) {
            Vector3 a = {m->pos[t], m->pos[t + 1], m->pos[t + 2]}, b = {m->pos[t + 3], m->pos[t + 4], m->pos[t + 5]}, c = {m->pos[t + 6], m->pos[t + 7], m->pos[t + 8]};
            float e = std::max({Vector3Distance(a, b), Vector3Distance(b, c), Vector3Distance(c, a)});
            int n = std::max(1, static_cast<int>(e / (vs * 0.5f)));
            for (int u = 0; u <= n; ++u)
                for (int v = 0; v <= n - u; ++v) {
                    float fu = float(u) / n, fv = float(v) / n;
                    mark(Vector3Add(a, Vector3Add(Vector3Scale(Vector3Subtract(b, a), fu), Vector3Scale(Vector3Subtract(c, a), fv))));
                }
        }
    auto solid = [&](Vector3 p) {
        int i = static_cast<int>((p.x - lo.x) / vs), j = static_cast<int>((p.y - lo.y) / vs), k = static_cast<int>((p.z - lo.z) / vs);
        if (i < 0 || j < 0 || k < 0 || i >= nx || j >= ny || k >= nz) return false;
        return vox[(static_cast<size_t>(k) * ny + j) * nx + i] != 0;
    };
    // fixed set of 24 directions (Fibonacci sphere)
    std::vector<Vector3> dirs;
    for (int i = 0; i < 48; ++i) {
        float yy = 1.0f - (i + 0.5f) / 48.0f * 2.0f, r = sqrtf(std::max(0.0f, 1.0f - yy * yy)), ph = i * 2.39996f;
        dirs.push_back({cosf(ph) * r, yy, sinf(ph) * r});
    }
    for (size_t v = 0; v * 3 < target.pos.size(); ++v) {
        Vector3 p = {target.pos[v * 3], target.pos[v * 3 + 1], target.pos[v * 3 + 2]};
        Vector3 n = {target.nrm[v * 3], target.nrm[v * 3 + 1], target.nrm[v * 3 + 2]};
        Vector3 o = Vector3Add(p, Vector3Scale(n, vs * 1.8f));
        float occ = 0, cnt = 0;
        for (const Vector3& d : dirs) {
            float cosT = Vector3DotProduct(d, n);
            if (cosT <= 0.05f) continue;
            cnt += cosT;
            for (float t = vs; t < 2.6f; t += vs * 0.9f) {
                if (solid(Vector3Add(o, Vector3Scale(d, t)))) { occ += cosT * (1.0f - t / 3.2f); break; }
            }
        }
        float ao = cnt > 0 ? 1.0f - 0.8f * occ / cnt : 1.0f;
        for (int k = 0; k < 3; ++k) target.col[v * 4 + k] = static_cast<unsigned char>(target.col[v * 4 + k] * std::clamp(ao, 0.25f, 1.0f));
    }
}

void canopy(MeshBuilder& mb, const Geo& g) {
    const int S = 9, R = 9;
    std::vector<float> xs;
    std::vector<std::vector<Vector2>> rings;
    for (int s = 0; s < S; ++s) {
        float t = float(s) / (S - 1);
        float x = g.canX0 + (g.canX1 - g.canX0) * t;
        float prof = sinf(PI * powf(t, 0.75f));        // bubble profile along x
        float w = g.canW * (0.25f + 0.75f * prof), h = g.canH * prof + 0.02f;
        std::vector<Vector2> ring;
        for (int i = 0; i < R; ++i) {
            float a = PI * i / (R - 1);
            ring.push_back({cosf(a) * w, sinf(a) * h});
        }
        xs.push_back(x);
        rings.push_back(ring);
    }
    // open loft (half shell)
    for (int s = 0; s + 1 < S; ++s)
        for (int i = 0; i + 1 < R; ++i) {
            auto P = [&](int ss, int ii) { return Vector3{xs[ss], rings[ss][ii].y + g.canY, rings[ss][ii].x}; };
            Vector3 inside = {(xs[s] + xs[s + 1]) * 0.5f, g.canY - 0.5f, 0};
            Vector3 a = P(s, i), b = P(s, i + 1), c = P(s + 1, i + 1), d = P(s + 1, i);
            Vector3 n = Vector3Normalize(Vector3Subtract(Vector3Scale(Vector3Add(a, c), 0.5f), inside));
            Color glass = {255, 255, 255, kGlass};
            // smooth normals approximated radially from the canopy axis
            auto rn = [&](Vector3 p) { return Vector3Normalize(Vector3Subtract(p, Vector3{p.x, g.canY - 0.2f, 0})); };
            Vector3 fn = Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(c, a));
            if (Vector3DotProduct(fn, n) >= 0) { mb.triN(a, b, c, rn(a), rn(b), rn(c), glass); mb.triN(a, c, d, rn(a), rn(c), rn(d), glass); }
            else { mb.triN(a, c, b, rn(a), rn(c), rn(b), glass); mb.triN(a, d, c, rn(a), rn(d), rn(c), glass); }
        }
    // canopy frame bow
    float fx = g.canX0 + (g.canX1 - g.canX0) * 0.62f;
    for (int i = 0; i + 1 < R; ++i) {
        float a0 = PI * i / (R - 1), a1 = PI * (i + 1) / (R - 1);
        float t = 0.62f, prof = sinf(PI * powf(t, 0.75f));
        float w = g.canW * (0.25f + 0.75f * prof) + 0.03f, h = g.canH * prof + 0.05f;
        Vector3 p0 = {fx - 0.08f, g.canY + sinf(a0) * h, cosf(a0) * w}, p1 = {fx - 0.08f, g.canY + sinf(a1) * h, cosf(a1) * w};
        Vector3 p2 = {fx + 0.08f, g.canY + sinf(a1) * h, cosf(a1) * w}, p3 = {fx + 0.08f, g.canY + sinf(a0) * h, cosf(a0) * w};
        quadOut(mb, p0, p1, p2, p3, {fx, g.canY, 0}, metal(40, 42, 45));
    }
}

void missileShape(MeshBuilder& mb, Vector3 c, float len, float r, Color body) {
    std::vector<float> xs = {c.x + len * 0.5f, c.x + len * 0.42f, c.x + len * 0.3f, c.x - len * 0.5f};
    std::vector<std::vector<Vector2>> rings;
    for (float rr : {0.02f, r * 0.8f, r, r}) {
        std::vector<Vector2> ring;
        for (int i = 0; i < 8; ++i) ring.push_back({cosf(2 * PI * i / 8) * rr, sinf(2 * PI * i / 8) * rr});
        rings.push_back(ring);
    }
    loft(mb, xs, rings, body, c.y, c.z, true);
    // fins
    for (int k = 0; k < 4; ++k) {
        float a = PI / 4 + k * PI / 2;
        Vector3 dir = {0, sinf(a), cosf(a)};
        Vector3 n = {0, -dir.z, dir.y};
        Vector3 p0 = {c.x - len * 0.5f, c.y + dir.y * r, c.z + dir.z * r};
        Vector3 p1 = {c.x - len * 0.32f, c.y + dir.y * r, c.z + dir.z * r};
        Vector3 p2 = {c.x - len * 0.45f, c.y + dir.y * r * 3.2f, c.z + dir.z * r * 3.2f};
        Vector3 p3 = {c.x - len * 0.5f, c.y + dir.y * r * 3.2f, c.z + dir.z * r * 3.2f};
        plate(mb, p0, p1, p2, p3, n, 0.02f, body);
    }
}

Geo geoFor(AirframeStyle s) {
    Geo g;
    switch (s) {
        case AirframeStyle::StealthTwinTail:
        default:
            g.fus = {{9.45f, 0.02f, 0.02f, 0.02f, 0.0f}, {8.4f, 0.40f, 0.26f, 0.22f, -0.04f}, {6.7f, 0.76f, 0.48f, 0.40f, -0.08f},
                     {4.9f, 1.00f, 0.56f, 0.52f, -0.12f}, {3.3f, 1.65f, 0.62f, 0.60f, -0.14f}, {1.0f, 2.05f, 0.64f, 0.62f, -0.12f},
                     {-2.0f, 2.15f, 0.62f, 0.60f, -0.08f}, {-5.0f, 2.05f, 0.56f, 0.54f, -0.04f}, {-7.6f, 1.80f, 0.48f, 0.44f, 0.0f},
                     {-8.9f, 1.60f, 0.40f, 0.36f, 0.0f}};
            g.faceted = true; g.pu = 3.4f; g.pl = 4.0f; g.roundNozzle = false;
            g.canX0 = 6.9f; g.canX1 = 2.4f; g.canW = 0.56f; g.canH = 0.6f; g.canY = 0.5f;
            g.wing = {2.2f, -6.4f, -3.7f, -5.0f, 1.9f, 6.8f, -0.05f, -0.12f, 0.32f};
            g.stab = {-6.3f, -9.0f, -8.5f, -9.7f, 1.55f, 4.5f, -0.1f, 0.0f, 0.12f};
            g.tails = {{-4.4f, -8.0f, -6.7f, -8.5f, 2.8f, 1.75f, 0.40f, 0.49f}};
            g.engines = 2; g.engineZ = 0.68f; g.nozzleX = -8.9f; g.nozzleR = 0.5f; g.nozzleLen = 1.0f; g.squareNozzle = true;
            g.intakeX = 3.4f; g.intakeZ = 1.5f; g.intakeW = 0.8f; g.intakeH = 0.9f; g.intakeY = -0.2f;
            g.pylonMissiles = 4;   // 2 ATA + 2 ATS shown on wing stations
            break;
        case AirframeStyle::StealthSingle:
            g.fus = {{7.85f, 0.02f, 0.02f, 0.02f, 0.0f}, {6.9f, 0.40f, 0.32f, 0.32f, -0.02f}, {5.4f, 0.80f, 0.66f, 0.56f, -0.05f},
                     {3.6f, 1.15f, 0.90f, 0.76f, -0.10f}, {2.0f, 1.80f, 0.96f, 0.90f, -0.12f}, {-0.5f, 2.00f, 0.98f, 0.98f, -0.10f},
                     {-3.5f, 1.90f, 0.90f, 0.90f, -0.05f}, {-6.0f, 1.30f, 0.70f, 0.70f, 0.0f}, {-7.3f, 0.85f, 0.55f, 0.55f, 0.0f}};
            g.canX0 = 5.6f; g.canX1 = 1.4f; g.canW = 0.55f; g.canH = 0.6f; g.canY = 0.66f;
            g.faceted = true; g.pu = 3.0f; g.pl = 3.4f;
            g.wing = {1.2f, -4.6f, -2.8f, -4.0f, 1.8f, 5.35f, -0.05f, -0.08f, 0.32f};
            g.stab = {-5.0f, -7.4f, -7.1f, -8.0f, 1.0f, 3.4f, -0.05f, 0.0f, 0.12f};
            g.tails = {{-3.6f, -6.6f, -5.8f, -7.0f, 2.4f, 1.15f, 0.55f, 0.44f}};
            g.engines = 1; g.engineZ = 0; g.nozzleX = -7.3f; g.nozzleR = 0.62f; g.nozzleLen = 1.0f;
            g.intakeX = 2.6f; g.intakeZ = 1.55f; g.intakeW = 0.75f; g.intakeH = 0.9f; g.intakeY = 0.05f;
            g.noseGearX = 4.5f; g.mainGearX = -1.2f; g.mainGearZ = 1.5f;
            break;
        case AirframeStyle::DeltaCanard:
            g.fus = {{7.6f, 0.02f, 0.02f, 0.02f, 0.0f}, {6.6f, 0.35f, 0.32f, 0.30f, 0.0f}, {5.0f, 0.62f, 0.60f, 0.52f, -0.05f},
                     {3.2f, 0.80f, 0.78f, 0.70f, -0.10f}, {1.4f, 1.35f, 0.72f, 0.85f, -0.20f}, {-1.5f, 1.45f, 0.70f, 0.85f, -0.15f},
                     {-4.5f, 1.35f, 0.65f, 0.72f, -0.05f}, {-6.6f, 1.15f, 0.55f, 0.55f, 0.0f}, {-7.6f, 1.05f, 0.48f, 0.45f, 0.0f}};
            g.canX0 = 5.3f; g.canX1 = 1.2f; g.canW = 0.5f; g.canH = 0.62f; g.canY = 0.6f;
            g.wing = {0.8f, -7.4f, -5.6f, -7.2f, 1.3f, 5.45f, -0.45f, 0.0f, 0.28f};
            g.stabs = false;
            g.canards = true; g.canard = {3.0f, 1.4f, 1.6f, 1.0f, 1.2f, 2.7f, 0.05f, 0.15f, 0.08f};
            g.tails = {{-3.4f, -7.2f, -6.4f, -7.6f, 3.2f, 0.0f, 0.5f, 0.0f}};
            g.engines = 2; g.engineZ = 0.48f; g.nozzleX = -7.6f; g.nozzleR = 0.42f;
            g.intakeX = 2.2f; g.intakeZ = 1.05f; g.intakeW = 0.55f; g.intakeH = 0.8f; g.intakeY = -0.55f;
            g.noseGearX = 4.5f; g.mainGearX = -2.0f; g.mainGearZ = 1.4f;
            g.pylonMissiles = 2;
            break;
        case AirframeStyle::FlankerFamily:
            g.fus = {{11.0f, 0.02f, 0.02f, 0.02f, 0.0f}, {10.0f, 0.42f, 0.38f, 0.40f, -0.1f}, {8.2f, 0.72f, 0.66f, 0.62f, -0.15f},
                     {6.2f, 0.85f, 0.88f, 0.72f, -0.15f}, {4.0f, 1.10f, 0.85f, 0.72f, -0.10f}, {1.0f, 1.25f, 0.70f, 0.60f, 0.0f},
                     {-3.0f, 1.10f, 0.55f, 0.55f, 0.0f}, {-7.0f, 0.75f, 0.40f, 0.40f, 0.0f}, {-10.4f, 0.35f, 0.25f, 0.25f, 0.0f}};
            g.canX0 = 7.6f; g.canX1 = 3.2f; g.canW = 0.52f; g.canH = 0.72f; g.canY = 0.72f;
            g.wing = {0.2f, -5.6f, -5.8f, -7.4f, 1.9f, 7.5f, -0.15f, -0.25f, 0.34f};
            g.lerx = true; g.lerxP = {6.0f, 0.0f, 0.4f, -0.2f, 0.9f, 2.6f, -0.1f, 0.0f, 0.1f};
            g.stab = {-6.0f, -8.8f, -8.7f, -9.8f, 2.2f, 5.0f, -0.4f, 0.0f, 0.12f};
            g.tails = {{-4.0f, -7.8f, -7.0f, -8.6f, 3.3f, 2.25f, 0.3f, 0.0f}};
            g.engines = 2; g.engineZ = 1.35f; g.nozzleX = -9.0f; g.nozzleR = 0.62f; g.nozzleLen = 1.0f;
            g.nacelles = true; g.nacX0 = 3.6f; g.nacX1 = -8.6f; g.nacR = 0.75f; g.nacZ = 1.35f; g.nacY = -0.6f;
            g.intakeX = 3.6f; g.intakeZ = 1.35f; g.intakeW = 0.7f; g.intakeH = 0.9f; g.intakeY = -0.85f;
            g.noseGearX = 6.8f; g.mainGearX = -1.2f; g.mainGearZ = 2.0f;
            g.pylonMissiles = 4;
            break;
        case AirframeStyle::Fulcrum:
            g.fus = {{9.0f, 0.02f, 0.02f, 0.02f, 0.0f}, {8.0f, 0.38f, 0.34f, 0.34f, -0.1f}, {6.4f, 0.62f, 0.60f, 0.55f, -0.12f},
                     {4.6f, 0.75f, 0.80f, 0.62f, -0.12f}, {2.6f, 0.95f, 0.75f, 0.62f, -0.05f}, {0.0f, 1.05f, 0.62f, 0.52f, 0.0f},
                     {-4.0f, 0.85f, 0.48f, 0.45f, 0.0f}, {-7.6f, 0.45f, 0.30f, 0.30f, 0.0f}};
            g.canX0 = 6.0f; g.canX1 = 2.6f; g.canW = 0.48f; g.canH = 0.62f; g.canY = 0.62f;
            g.wing = {-0.4f, -4.6f, -4.4f, -5.8f, 1.7f, 5.7f, -0.2f, -0.15f, 0.3f};
            g.lerx = true; g.lerxP = {4.6f, -0.4f, 0.0f, -0.6f, 0.8f, 2.2f, -0.12f, 0.0f, 0.1f};
            g.stab = {-5.0f, -7.4f, -7.4f, -8.3f, 1.9f, 4.0f, -0.35f, 0.0f, 0.12f};
            g.tails = {{-3.0f, -6.4f, -5.6f, -7.0f, 2.8f, 1.9f, 0.25f, 0.12f}};
            g.engines = 2; g.engineZ = 1.15f; g.nozzleX = -7.4f; g.nozzleR = 0.55f;
            g.nacelles = true; g.nacX0 = 2.8f; g.nacX1 = -7.0f; g.nacR = 0.64f; g.nacZ = 1.15f; g.nacY = -0.5f;
            g.intakeX = 2.8f; g.intakeZ = 1.15f; g.intakeW = 0.6f; g.intakeH = 0.75f; g.intakeY = -0.7f;
            g.noseGearX = 5.2f; g.mainGearX = -1.0f; g.mainGearZ = 1.7f;
            g.pylonMissiles = 2;
            break;
        case AirframeStyle::ViperSingle:
            g.fus = {{7.5f, 0.02f, 0.02f, 0.02f, 0.0f}, {6.6f, 0.34f, 0.32f, 0.32f, 0.0f}, {5.0f, 0.58f, 0.60f, 0.50f, -0.05f},
                     {3.0f, 0.70f, 0.78f, 0.70f, -0.05f}, {0.5f, 0.85f, 0.72f, 0.80f, 0.0f}, {-3.0f, 0.85f, 0.62f, 0.62f, 0.0f},
                     {-6.0f, 0.62f, 0.50f, 0.50f, 0.0f}, {-7.3f, 0.55f, 0.48f, 0.48f, 0.0f}};
            g.canX0 = 5.4f; g.canX1 = 1.4f; g.canW = 0.46f; g.canH = 0.66f; g.canY = 0.6f;
            g.wing = {-0.6f, -4.0f, -3.4f, -4.6f, 0.85f, 4.98f, -0.1f, 0.0f, 0.28f};
            g.lerx = true; g.lerxP = {3.6f, -0.6f, -0.2f, -1.0f, 0.6f, 1.4f, -0.05f, 0.0f, 0.08f};
            g.stab = {-5.0f, -7.0f, -6.8f, -7.6f, 0.8f, 2.9f, -0.2f, -0.3f, 0.1f};
            g.tails = {{-3.2f, -6.6f, -5.8f, -7.0f, 2.6f, 0.0f, 0.5f, 0.0f}};
            g.engines = 1; g.engineZ = 0; g.nozzleX = -7.3f; g.nozzleR = 0.5f;
            g.ventral = true; g.intakeX = 1.8f; g.intakeZ = 0.0f; g.intakeW = 0.55f; g.intakeH = 0.55f; g.intakeY = -0.95f;
            g.noseGearX = 4.0f; g.mainGearX = -1.0f; g.mainGearZ = 1.15f;
            g.pylonMissiles = 2;
            break;
    }
    return g;
}

}  // namespace

JetModel buildJetModel(AirframeStyle style) {
    Geo g = geoFor(style);
    MeshBuilder mb;
    // fuselage
    std::vector<float> xs;
    std::vector<std::vector<Vector2>> rings;
    for (const Section& s : smoothStations(g.fus, 6)) { xs.push_back(s.x); rings.push_back(sectionRing(s, g.pu, g.pl)); }
    loft(mb, xs, rings, paint(), 0.0f, 0.0f, true, false);
    // canopy (separate mesh)
    MeshBuilder cb;
    canopy(cb, g);
    // wings
    wingSurface(mb, g.wing, true, paint());
    wingSurface(mb, g.wing, false, paint());
    if (g.lerx) { wingSurface(mb, g.lerxP, true, paint(245)); wingSurface(mb, g.lerxP, false, paint(245)); }
    if (g.canards) {
        wingSurface(mb, g.canard, true, paint(235));
        wingSurface(mb, g.canard, false, paint(235));
    }
    // tails
    for (const TailDef& t : g.tails) {
        if (t.z == 0.0f) tail(mb, t, 1.0f, paint(245));
        else { tail(mb, t, 1.0f, paint(245)); tail(mb, t, -1.0f, paint(245)); }
    }
    // nacelles (Flanker family)
    if (g.nacelles) {
        for (float sgn : {-1.0f, 1.0f}) tube(mb, g.nacX0, g.nacX1, g.nacR * 0.95f, g.nacR, g.nacY, sgn * g.nacZ, 12, paint(235));
        // tail boom "stinger"
    }
    // intakes
    {
        Color dark = metal(18, 18, 20);
        for (float sgn : {-1.0f, 1.0f}) {
            if (g.ventral && sgn > 0) continue;
            float z = g.ventral ? 0.0f : sgn * g.intakeZ;
            float x0 = g.intakeX, x1 = g.intakeX - 3.0f;
            float hw = g.intakeW * 0.5f, hh = g.intakeH * 0.5f, y = g.intakeY;
            // intake duct box (paint) with dark face
            Vector3 c = {x0 - 1.5f, y, z};
            MeshBuilder& m = mb;
            Vector3 f0 = {x0, y - hh, z - hw}, f1 = {x0, y + hh, z - hw}, f2 = {x0 + 0.25f, y + hh, z + hw}, f3 = {x0, y - hh, z + hw};
            if (sgn < 0 && !g.ventral) { f0.z = z + hw; f1.z = z + hw; f2.z = z - hw; f3.z = z - hw; }
            Vector3 b0 = {x1, y - hh * 0.8f, f0.z}, b1 = {x1, y + hh * 0.8f, f1.z}, b2 = {x1, y + hh * 0.8f, f2.z}, b3 = {x1, y - hh * 0.8f, f3.z};
            quadOut(m, f0, f1, b1, b0, c, paint(235));
            quadOut(m, f1, f2, b2, b1, c, paint(235));
            quadOut(m, f2, f3, b3, b2, c, paint(235));
            quadOut(m, f3, f0, b0, b3, c, paint(220));
            // recessed dark face
            Vector3 inset = {-0.15f, 0, 0};
            quadOut(m, Vector3Add(f0, inset), Vector3Add(f1, inset), Vector3Add(f2, inset), Vector3Add(f3, inset), {x0 - 3.0f, y, z}, dark);
        }
    }
    // nozzles
    JetModel jm;
    jm.nozzleCount = g.engines;
    jm.nozzleRadius = g.nozzleR;
    for (int e = 0; e < g.engines; ++e) {
        float z = g.engines == 1 ? 0.0f : (e == 0 ? -g.engineZ : g.engineZ);
        float y = g.nacelles ? g.nacY : -0.05f;
        tube(mb, g.nozzleX + 0.4f, g.nozzleX - g.nozzleLen, g.nozzleR * 1.08f, g.nozzleR * 0.9f, y, z, g.squareNozzle ? 16 : 24,
             metal(128, 124, 118), g.squareNozzle, g.squareNozzle ? 0.0f : 0.035f);
        // inner liner (darker, slightly recessed)
        tube(mb, g.nozzleX - g.nozzleLen + 0.02f, g.nozzleX - g.nozzleLen + 0.5f, g.nozzleR * 0.86f, g.nozzleR * 0.8f, y, z, g.squareNozzle ? 16 : 24,
             metal(58, 52, 48), g.squareNozzle);
        // inner dark disk at the exit
        Color inner = metal(25, 18, 14);
        for (int i = 0; i < 14; ++i) {
            float a0 = 2 * PI * i / 14, a1 = 2 * PI * (i + 1) / 14;
            float r = g.nozzleR * 0.85f;
            Vector3 c = {g.nozzleX - g.nozzleLen + 0.15f, y, z};
            triOut(mb, c, {c.x, y + sinf(a0) * r * (g.squareNozzle ? 0.7f : 1.0f), z + cosf(a0) * r}, {c.x, y + sinf(a1) * r * (g.squareNozzle ? 0.7f : 1.0f), z + cosf(a1) * r}, {c.x + 1, y, z}, inner);
        }
        jm.nozzles[e] = {g.nozzleX - g.nozzleLen + 0.1f, y, z};
    }
    // pylon missiles
    for (int k = 0; k < g.pylonMissiles; ++k) {
        float side = (k % 2) ? 1.0f : -1.0f;
        float span = g.wing.rootZ + (g.wing.tipZ - g.wing.rootZ) * (k < 2 ? 0.55f : 0.85f);
        float t = (span - g.wing.rootZ) / (g.wing.tipZ - g.wing.rootZ);
        float xle = g.wing.rootLE + (g.wing.tipLE - g.wing.rootLE) * t, xte = g.wing.rootTE + (g.wing.tipTE - g.wing.rootTE) * t;
        Vector3 c = {(xle + xte) * 0.5f, g.wing.y - 0.45f, side * span};
        missileShape(mb, c, 3.6f, 0.09f, metal(210, 210, 205));
        mb.box({c.x, g.wing.y - 0.22f, c.z}, {1.6f, 0.3f, 0.06f}, paint(200));
    }
    bakeAmbientOcclusion(mb, {&mb});
    jm.body = mb.build();
    jm.canopy = cb.build();
    jm.eye = {g.canX1 + (g.canX0 - g.canX1) * 0.40f, g.canY + g.canH * 0.62f, 0.0f};

    // horizontal stabilisers as separate meshes around their pivot
    if (g.stabs) {
        for (int side = 0; side < 2; ++side) {
            MeshBuilder sb;
            Planform p = g.stab;
            float pivotX = p.rootLE + (p.rootTE - p.rootLE) * 0.35f;
            Planform q = p;
            q.rootLE -= pivotX; q.rootTE -= pivotX; q.tipLE -= pivotX; q.tipTE -= pivotX;
            q.y = 0;
            wingSurface(sb, q, side == 1, paint(240));
            Vector3 pivot = {pivotX, p.y, 0};
            if (side == 0) { jm.stabL = sb.build(); jm.stabPivotL = pivot; }
            else { jm.stabR = sb.build(); jm.stabPivotR = pivot; }
        }
    }

    // landing gear (extended position)
    MeshBuilder gb;
    Color strut = metal(170, 170, 168), tyre = metal(22, 22, 24);
    const float ground = -2.35f;
    auto wheel = [&](Vector3 c, float r, float w) {
        Matrix xf = MatrixMultiply(MatrixRotateX(PI / 2), MatrixTranslate(c.x, c.y, c.z));
        gb.cylinder({0, -w / 2, 0}, r, w, 12, tyre, xf);
    };
    gb.cylinder({g.noseGearX, ground + 0.35f, 0}, 0.07f, -ground - 0.6f, 6, strut);
    wheel({g.noseGearX, ground + 0.33f, 0}, 0.33f, 0.22f);
    for (float sgn : {-1.0f, 1.0f}) {
        gb.cylinder({g.mainGearX, ground + 0.45f, sgn * g.mainGearZ}, 0.1f, -ground - 0.5f, 6, strut);
        wheel({g.mainGearX, ground + 0.45f, sgn * g.mainGearZ}, 0.45f, 0.3f);
        gb.box({g.mainGearX, -0.9f, sgn * (g.mainGearZ - 0.1f)}, {1.6f, 1.0f, 0.05f}, paint(210));  // gear door
    }
    gb.box({g.noseGearX, -0.7f, 0.3f}, {1.2f, 0.6f, 0.04f}, paint(210));
    jm.gear = gb.build();

    jm.wingtips[0] = {g.wing.tipTE + 0.4f, g.wing.y + g.wing.tipDy, -g.wing.tipZ};
    jm.wingtips[1] = {g.wing.tipTE + 0.4f, g.wing.y + g.wing.tipDy, g.wing.tipZ};
    jm.length = g.fus.front().x - g.fus.back().x;
    jm.loaded = true;
    return jm;
}

void unloadJetModel(JetModel& m) {
    if (!m.loaded) return;
    UnloadMesh(m.body);
    UnloadMesh(m.canopy);
    UnloadMesh(m.gear);
    if (m.stabL.vertexCount) UnloadMesh(m.stabL);
    if (m.stabR.vertexCount) UnloadMesh(m.stabR);
    m.loaded = false;
}

Mesh buildFlameMesh() {
    MeshBuilder mb;
    const int S = 10, R = 14;
    std::vector<float> xs;
    std::vector<std::vector<Vector2>> rings;
    for (int s = 0; s < S; ++s) {
        float t = float(s) / (S - 1);
        float r = 1.0f - 0.85f * powf(t, 0.8f);
        xs.push_back(-t);
        std::vector<Vector2> ring;
        for (int i = 0; i < R; ++i) ring.push_back({cosf(2 * PI * i / R) * r, sinf(2 * PI * i / R) * r});
        rings.push_back(ring);
    }
    loft(mb, xs, rings, WHITE, 0, 0, false);
    return mb.build();
}

Mesh buildMissileMesh() {
    MeshBuilder mb;
    missileShape(mb, {0, 0, 0}, 3.7f, 0.1f, metal(220, 220, 215));
    return mb.build();
}

}  // namespace sky
