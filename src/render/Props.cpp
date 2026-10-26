// Geometry for the world-detail props (army compound, forward bases, street furniture, yards):
// every prop of an area is merged into a few static meshes, so hundreds of vehicles, tents and
// lamps cost a handful of draw calls.
#include "Props.hpp"

#include "raymath.h"

#include <algorithm>
#include <cmath>

namespace sky {

namespace {

Color shade(Color c, float k) {
    return {static_cast<unsigned char>(std::clamp(c.r * k, 0.0f, 255.0f)), static_cast<unsigned char>(std::clamp(c.g * k, 0.0f, 255.0f)),
            static_cast<unsigned char>(std::clamp(c.b * k, 0.0f, 255.0f)), c.a};
}

const Color kSteel = {78, 80, 82, 255}, kTyre = {34, 34, 34, 255}, kGlass = {40, 52, 62, 255}, kConcrete = {168, 166, 158, 255};
const Color kWhite = {226, 226, 220, 255}, kRed = {178, 40, 34, 255}, kYellow = {214, 170, 40, 255};

Color containerColor(int v) {
    static const Color c[8] = {{170, 58, 44, 255}, {44, 86, 150, 255}, {58, 116, 70, 255}, {204, 120, 40, 255},
                               {130, 132, 134, 255}, {212, 212, 206, 255}, {112, 40, 44, 255}, {40, 118, 124, 255}};
    return c[v & 7];
}
Color carColor(int v) {
    static const Color c[16] = {{200, 200, 196, 255}, {30, 30, 32, 255}, {120, 124, 128, 255}, {150, 26, 28, 255},
                                {28, 54, 110, 255}, {226, 226, 222, 255}, {60, 70, 60, 255}, {170, 150, 110, 255},
                                {90, 20, 24, 255}, {40, 90, 140, 255}, {210, 170, 40, 255}, {80, 80, 84, 255},
                                {240, 240, 236, 255}, {20, 40, 60, 255}, {140, 60, 30, 255}, {100, 110, 120, 255}};
    return c[v & 15];
}
Color adColor(int v) {
    static const Color c[8] = {{214, 52, 40, 255}, {30, 110, 190, 255}, {240, 190, 40, 255}, {40, 150, 90, 255},
                               {120, 50, 150, 255}, {230, 110, 30, 255}, {20, 20, 24, 255}, {30, 170, 180, 255}};
    return c[v & 7];
}

void gable(MeshBuilder& m, float L, float D, float yTop, float rise, Color roof, Color wall, Matrix xf) {
    Vector3 p0 = Vector3Transform({-L / 2, yTop, -D / 2}, xf), p1 = Vector3Transform({L / 2, yTop, -D / 2}, xf);
    Vector3 p2 = Vector3Transform({L / 2, yTop, D / 2}, xf), p3 = Vector3Transform({-L / 2, yTop, D / 2}, xf);
    Vector3 r0 = Vector3Transform({-L / 2, yTop + rise, 0}, xf), r1 = Vector3Transform({L / 2, yTop + rise, 0}, xf);
    m.tri2(p0, p1, r1, roof); m.tri2(p0, r1, r0, roof);
    m.tri2(p3, p2, r1, roof); m.tri2(p3, r1, r0, roof);
    m.tri2(p0, r0, p3, wall); m.tri2(p1, r1, p2, wall);
}

/// Barrel / boom along local +X starting at `from`.
void rod(MeshBuilder& m, Vector3 from, float len, float r, Color c, Matrix xf, int sides = 6) {
    Matrix R = MatrixMultiply(MatrixRotateZ(-PI / 2), MatrixTranslate(from.x, from.y, from.z));
    m.cylinder({0, 0, 0}, r, len, sides, c, MatrixMultiply(R, xf));
}

void tank(MeshBuilder& m, Matrix xf, Color body, bool apc) {
    if (apc) {
        m.box({0, 1.35f, 0}, {6.6f, 1.7f, 2.9f}, body, xf);
        m.box({2.9f, 1.05f, 0}, {1.2f, 1.1f, 2.8f}, shade(body, 0.92f), xf);
        for (float x : {-2.2f, -0.7f, 0.8f, 2.3f})
            for (float z : {-1.35f, 1.35f}) m.box({x, 0.55f, z}, {1.1f, 1.1f, 0.5f}, kTyre, xf);
        m.box({0.4f, 2.45f, 0}, {1.4f, 0.55f, 1.3f}, shade(body, 0.85f), xf);
        rod(m, {1.1f, 2.5f, 0}, 2.2f, 0.07f, kSteel, xf, 5);
        return;
    }
    m.box({0, 1.05f, 0}, {7.0f, 1.2f, 3.3f}, body, xf);
    for (float z : {-1.55f, 1.55f}) m.box({0, 0.55f, z}, {7.3f, 1.1f, 0.68f}, shade(kTyre, 1.4f), xf);
    m.box({-0.3f, 2.0f, 0}, {3.6f, 0.8f, 2.7f}, shade(body, 0.9f), xf);
    m.box({-2.4f, 2.0f, 0}, {0.8f, 0.6f, 2.2f}, shade(body, 0.8f), xf);   // bustle
    rod(m, {1.5f, 2.05f, 0}, 4.8f, 0.12f, shade(body, 0.7f), xf);
}

void truck(MeshBuilder& m, Matrix xf, Color body, Color cargo, bool fuel) {
    m.box({0, 0.75f, 0}, {7.0f, 0.4f, 1.9f}, kSteel, xf);
    m.box({2.75f, 1.65f, 0}, {1.7f, 1.9f, 2.4f}, body, xf);
    m.box({3.5f, 2.0f, 0}, {0.1f, 0.8f, 2.1f}, kGlass, xf);
    if (fuel) m.cylinder({0, 0, 0}, 1.15f, 4.6f, 10, cargo, MatrixMultiply(MatrixMultiply(MatrixRotateZ(PI / 2), MatrixTranslate(1.9f, 2.0f, 0)), xf));
    else m.box({-0.9f, 2.05f, 0}, {4.9f, 2.3f, 2.45f}, cargo, xf);
    for (float x : {2.6f, -0.6f, -2.2f})
        for (float z : {-1.0f, 1.0f}) m.box({x, 0.5f, z}, {1.0f, 1.0f, 0.35f}, kTyre, xf);
}

void jeep(MeshBuilder& m, Matrix xf, Color body) {
    m.box({0, 0.95f, 0}, {4.4f, 1.0f, 2.0f}, body, xf);
    m.box({-0.4f, 1.75f, 0}, {2.4f, 0.7f, 1.9f}, shade(body, 0.85f), xf);
    m.box({0.85f, 1.75f, 0}, {0.1f, 0.6f, 1.8f}, kGlass, xf);
    for (float x : {1.4f, -1.4f})
        for (float z : {-0.95f, 0.95f}) m.box({x, 0.42f, z}, {0.85f, 0.85f, 0.32f}, kTyre, xf);
}

void car(MeshBuilder& m, Matrix xf, Color body) {
    m.box({0, 0.62f, 0}, {4.3f, 0.75f, 1.8f}, body, xf);
    m.box({-0.2f, 1.25f, 0}, {2.3f, 0.6f, 1.62f}, shade(body, 0.9f), xf);
    m.box({0.97f, 1.22f, 0}, {0.06f, 0.5f, 1.5f}, kGlass, xf);
    m.box({-1.37f, 1.22f, 0}, {0.06f, 0.5f, 1.5f}, kGlass, xf);
}

}  // namespace

void buildHelicopter(MeshBuilder& m, Matrix xf, Color body, bool attack, bool blades) {
    const Color dark = shade(body, 0.7f);
    if (attack) {
        // tandem two-seat attack helicopter: narrow fuselage, stub wings with pods, skids
        m.box({0.2f, 2.0f, 0}, {7.6f, 1.6f, 1.25f}, body, xf);
        m.box({4.4f, 1.75f, 0}, {1.6f, 1.1f, 1.0f}, body, xf);
        m.box({3.2f, 2.95f, 0}, {1.4f, 0.75f, 0.95f}, kGlass, xf);
        m.box({1.6f, 3.15f, 0}, {1.4f, 0.8f, 0.95f}, kGlass, xf);
        m.box({-0.4f, 3.1f, 0}, {2.8f, 0.9f, 1.5f}, dark, xf);                 // engines
        m.box({0.3f, 2.0f, 0}, {1.4f, 0.18f, 4.6f}, dark, xf);                 // stub wings
        for (float z : {-1.9f, 1.9f}) rod(m, {-0.6f, 1.65f, z}, 1.9f, 0.24f, dark, xf, 6);
        rod(m, {4.9f, 1.15f, 0}, 1.8f, 0.05f, kSteel, xf, 4);                  // chin gun
        m.box({-5.6f, 2.45f, 0}, {6.0f, 0.55f, 0.5f}, body, xf);               // tail boom
        m.box({-8.4f, 3.4f, 0}, {1.2f, 2.3f, 0.22f}, body, xf);                // fin
        m.box({-8.2f, 2.55f, 0}, {1.0f, 0.1f, 2.6f}, body, xf);                // stabiliser
        for (float z : {-0.95f, 0.95f}) {
            m.box({0.4f, 0.12f, z}, {4.6f, 0.12f, 0.12f}, kSteel, xf);         // skids
            m.box({1.4f, 0.65f, z * 0.85f}, {0.12f, 1.0f, 0.12f}, kSteel, xf);
            m.box({-0.8f, 0.65f, z * 0.85f}, {0.12f, 1.0f, 0.12f}, kSteel, xf);
        }
        m.cylinder({-0.2f, 3.55f, 0}, 0.18f, 0.55f, 6, kSteel, xf);
        if (blades)
            for (int k = 0; k < 4; ++k) {
                Matrix R = MatrixMultiply(MatrixRotateY(k * PI / 2 + 0.4f), xf);
                m.box({3.5f, 4.12f, 0}, {7.0f, 0.06f, 0.5f}, kSteel, MatrixMultiply(MatrixTranslate(-0.2f, 0, 0), R));
            }
        m.box({-8.4f, 3.6f, 0.3f}, {0.18f, 2.2f, 0.06f}, kSteel, xf);           // tail rotor
        return;
    }
    // utility / transport helicopter: cabin with doors, wheels
    m.box({0, 2.25f, 0}, {6.6f, 2.5f, 2.6f}, body, xf);
    m.box({3.9f, 2.0f, 0}, {1.6f, 1.9f, 2.3f}, body, xf);
    m.box({4.25f, 2.75f, 0}, {1.0f, 0.9f, 2.2f}, kGlass, xf);
    m.box({0.6f, 2.2f, 0}, {2.4f, 1.6f, 2.66f}, dark, xf);                      // doors
    m.box({-0.6f, 3.85f, 0}, {4.0f, 0.8f, 1.6f}, dark, xf);                     // engine housing
    m.box({-6.4f, 3.0f, 0}, {7.0f, 0.8f, 0.7f}, body, xf);                      // tail boom
    m.box({-9.8f, 4.1f, 0}, {1.4f, 2.6f, 0.26f}, body, xf);
    m.box({-9.3f, 3.0f, 0}, {1.3f, 0.12f, 3.2f}, body, xf);
    for (float z : {-1.25f, 1.25f}) m.box({0.8f, 0.45f, z * 1.15f}, {0.75f, 0.9f, 0.3f}, kTyre, xf);
    m.box({-7.6f, 0.9f, 0}, {0.4f, 0.4f, 0.2f}, kTyre, xf);
    m.cylinder({-0.4f, 4.25f, 0}, 0.22f, 0.6f, 6, kSteel, xf);
    if (blades)
        for (int k = 0; k < 4; ++k) {
            Matrix R = MatrixMultiply(MatrixRotateY(k * PI / 2 + 0.2f), xf);
            m.box({4.0f, 4.9f, 0}, {8.0f, 0.07f, 0.55f}, kSteel, MatrixMultiply(MatrixTranslate(-0.4f, 0, 0), R));
        }
    m.box({-9.9f, 4.3f, 0.32f}, {0.2f, 2.4f, 0.06f}, kSteel, xf);
}

void buildCarBox(MeshBuilder& m, Matrix xf, Color body, int kind) {
    switch (kind) {
        case 1: m.box({0, 1.1f, 0}, {5.0f, 1.9f, 2.0f}, body, xf); m.box({1.6f, 1.45f, 0}, {0.06f, 0.7f, 1.8f}, kGlass, xf); break;
        case 2: truck(m, xf, Color{200, 200, 196, 255}, body, false); break;
        case 3: m.box({0, 1.75f, 0}, {11.5f, 2.9f, 2.5f}, body, xf); m.box({0, 2.15f, 0}, {11.0f, 0.9f, 2.56f}, kGlass, xf); break;
        case 4: truck(m, xf, Color{96, 100, 74, 255}, Color{106, 108, 82, 255}, false); break;
        case 5: jeep(m, xf, Color{98, 104, 76, 255}); break;
        default: car(m, xf, body); break;
    }
}

Color sceneCarColor(int v) { return carColor(v); }

namespace {
// airbase ground equipment (local +X forward, ground at y = 0)
void tug(MeshBuilder& m, Matrix xf, Color body) {
    m.box({0, 0.75f, 0}, {4.2f, 0.9f, 2.2f}, body, xf);
    m.box({-1.1f, 1.55f, 0}, {1.3f, 0.8f, 1.9f}, kGlass, xf);
    m.box({2.0f, 0.6f, 0}, {0.4f, 0.3f, 1.2f}, kSteel, xf);   // tow bar
    for (float x : {1.3f, -1.3f})
        for (float z : {-1.1f, 1.1f}) m.box({x, 0.4f, z}, {0.8f, 0.8f, 0.35f}, kTyre, xf);
}
void forklift(MeshBuilder& m, Matrix xf, Color body) {
    m.box({-0.3f, 0.75f, 0}, {2.4f, 1.0f, 1.3f}, body, xf);
    m.box({-0.5f, 1.6f, 0}, {1.1f, 0.06f, 1.2f}, kSteel, xf);             // roof guard
    for (float z : {-0.55f, 0.55f}) m.box({-0.5f, 1.25f, z}, {0.06f, 0.9f, 0.06f}, kSteel, xf);
    for (float z : {-0.35f, 0.35f}) m.box({1.0f, 1.3f, z}, {0.12f, 2.4f, 0.1f}, kSteel, xf);   // mast
    for (float z : {-0.35f, 0.35f}) m.box({1.6f, 0.25f, z}, {1.2f, 0.06f, 0.12f}, kSteel, xf);  // forks
    for (float x : {0.6f, -1.1f})
        for (float z : {-0.6f, 0.6f}) m.box({x, 0.3f, z}, {0.55f, 0.6f, 0.25f}, kTyre, xf);
}
void fireTruck(MeshBuilder& m, Matrix xf) {
    Color red = {190, 32, 28, 255};
    m.box({0, 1.6f, 0}, {9.5f, 2.4f, 2.6f}, red, xf);
    m.box({3.6f, 2.0f, 0}, {0.1f, 1.0f, 2.3f}, kGlass, xf);
    m.box({0.5f, 3.0f, 0}, {4.0f, 0.4f, 2.0f}, kWhite, xf);
    m.box({3.2f, 2.95f, 0}, {0.5f, 0.25f, 1.4f}, Color{60, 120, 255, 253}, xf);   // light bar
    rod(m, {-1.0f, 3.3f, 0}, 3.0f, 0.12f, kSteel, xf);                           // monitor
    for (float x : {3.0f, -1.5f, -3.0f})
        for (float z : {-1.2f, 1.2f}) m.box({x, 0.55f, z}, {1.1f, 1.1f, 0.4f}, kTyre, xf);
}
void radarTruck(MeshBuilder& m, Matrix xf, Color body) {
    truck(m, xf, body, shade(body, 0.92f), false);
    m.box({-1.0f, 3.5f, 0}, {0.3f, 0.6f, 0.3f}, kSteel, xf);
    m.box({-1.0f, 4.3f, 0}, {0.2f, 1.4f, 4.2f}, Color{200, 202, 196, 255}, MatrixMultiply(MatrixRotateZ(0.35f), xf));   // array
}
}  // namespace

void buildGroundVehicle(MeshBuilder& m, Matrix xf, PropKind k, Color body) {
    switch (k) {
        case PropKind::Tug: tug(m, xf, Color{214, 170, 40, 255}); break;
        case PropKind::Forklift: forklift(m, xf, Color{220, 180, 30, 255}); break;
        case PropKind::FireTruck: fireTruck(m, xf); break;
        case PropKind::RadarTruck: radarTruck(m, xf, body); break;
        case PropKind::FuelTruck: truck(m, xf, body, shade(body, 0.9f), true); break;
        default: truck(m, xf, body, shade(body, 1.1f), false); break;
    }
}

void buildSceneProps(const Terrain& t, const WorldLayout& L, bool desertArmy, std::vector<Dressing::Piece>& out, std::vector<Vector3>& beacons) {
    const Color army = desertArmy ? Color{178, 158, 118, 255} : Color{94, 100, 72, 255};
    const Color canvas = desertArmy ? Color{196, 180, 140, 255} : Color{110, 112, 84, 255};
    const Color heli = desertArmy ? Color{150, 138, 108, 255} : Color{70, 78, 64, 255};
    const Color sand = desertArmy ? Color{190, 172, 132, 255} : Color{150, 140, 112, 255};
    // group by 1.2 km cells so each merged piece can be culled on its own
    struct Bucket { MeshBuilder mb; Vector3 lo{1e9f, 1e9f, 1e9f}, hi{-1e9f, -1e9f, -1e9f}; };
    std::vector<std::pair<long long, Bucket>> buckets;
    auto bucketFor = [&](const Vec3& p) -> Bucket& {
        long long k = (static_cast<long long>(std::floor(p.x / 1200.0)) << 20) ^ static_cast<long long>(std::floor(p.z / 1200.0) + 4096);
        for (auto& b : buckets) if (b.first == k) return b.second;
        buckets.push_back({k, Bucket{}});
        return buckets.back().second;
    };
    for (const SceneProp& p : L.props) {
        Bucket& B = bucketFor(p.pos);
        MeshBuilder& m = B.mb;
        float gy = static_cast<float>(p.pos.y);
        Matrix xf = MatrixMultiply(MatrixRotateY(-static_cast<float>(p.yaw)), MatrixTranslate(static_cast<float>(p.pos.x), gy - 0.1f, static_cast<float>(p.pos.z)));
        float reach = 12.0f, top = 12.0f;
        switch (p.kind) {
            case PropKind::Helicopter: buildHelicopter(m, xf, heli, false, true); break;
            case PropKind::AttackHelicopter: buildHelicopter(m, xf, shade(heli, 0.8f), true, true); break;
            case PropKind::Tank: tank(m, xf, shade(army, 1.0f - 0.06f * p.variant), false); break;
            case PropKind::Apc: tank(m, xf, army, true); break;
            case PropKind::Truck: truck(m, xf, army, canvas, false); break;
            case PropKind::FuelTruck: truck(m, xf, army, shade(army, 0.9f), true); break;
            case PropKind::Jeep: jeep(m, xf, army); break;
            case PropKind::Barracks: {
                Color wall = p.variant ? Color{206, 196, 176, 255} : Color{190, 184, 170, 255};
                m.box({0, 2.6f, 0}, {40.0f, 5.6f, 12.0f}, wall, xf);
                gable(m, 40.6f, 12.6f, 5.4f, 2.2f, Color{110, 96, 82, 255}, wall, xf);
                for (int i = 0; i < 8; ++i)
                    for (float z : {-6.05f, 6.05f}) {
                        Color win = ((i * 7 + p.variant * 3 + (z > 0 ? 1 : 0)) % 3) ? Color{255, 206, 140, 253} : Color{20, 24, 30, 251};
                        m.box({-17.0f + i * 4.8f, 3.0f, z}, {1.6f, 1.3f, 0.06f}, win, xf);
                    }
                reach = 22; top = 8;
                break;
            }
            case PropKind::Tent: {
                m.box({0, 1.1f, 0}, {12.0f, 2.2f, 6.0f}, canvas, xf);
                gable(m, 12.2f, 6.2f, 2.2f, 1.7f, shade(canvas, 0.9f), canvas, xf);
                m.box({6.05f, 1.0f, 0}, {0.06f, 1.4f, 1.4f}, ((p.variant & 1) ? Color{255, 196, 120, 253} : Color{20, 22, 24, 251}), xf);
                break;
            }
            case PropKind::Helipad: {
                m.cylinder({0, 0, 0}, 13.0f, 0.32f, 20, kConcrete, xf);
                m.cylinder({0, 0.02f, 0}, 11.6f, 0.32f, 20, shade(kConcrete, 0.82f), xf);
                Color w = {236, 236, 230, 255};
                m.box({0, 0.36f, -2.6f}, {7.0f, 0.06f, 0.9f}, w, xf);
                m.box({0, 0.36f, 2.6f}, {7.0f, 0.06f, 0.9f}, w, xf);
                m.box({0, 0.36f, 0}, {0.9f, 0.06f, 5.2f}, w, xf);
                top = 2;
                break;
            }
            case PropKind::RadarMast: {
                m.box({0, 1.4f, 0}, {5.0f, 2.8f, 4.0f}, Color{180, 178, 170, 255}, xf);
                m.cylinder({0, 2.8f, 0}, 0.9f, 19.0f, 6, Color{200, 200, 196, 255}, xf, 0.45f);
                m.box({0, 21.9f, 0}, {1.0f, 0.4f, 1.0f}, kSteel, xf);   // turntable (the antenna rotates: Renderer::drawAirbaseLife)
                beacons.push_back(Vector3Transform({0, 24.5f, 0}, xf));
                top = 26;
                break;
            }
            case PropKind::Antenna:
                m.cylinder({0, 0, 0}, 0.45f, 30.0f, 4, Color{190, 60, 50, 255}, xf, 0.12f);
                for (float y : {8.0f, 16.0f, 24.0f}) m.box({0, y, 0}, {1.8f, 0.12f, 0.12f}, kSteel, xf);
                beacons.push_back(Vector3Transform({0, 30.6f, 0}, xf));
                top = 31;
                break;
            case PropKind::FloodMast:
                m.cylinder({0, 0, 0}, 0.3f, 17.0f, 6, kSteel, xf, 0.18f);
                m.box({0, 17.3f, 0}, {1.4f, 0.8f, 2.6f}, Color{60, 62, 64, 255}, xf);
                top = 18;
                break;
            case PropKind::Checkpoint:
                m.box({0, 1.5f, -4.0f}, {3.2f, 3.0f, 2.6f}, Color{200, 196, 184, 255}, xf);
                m.box({0, 3.1f, -4.0f}, {3.8f, 0.3f, 3.2f}, Color{80, 84, 82, 255}, xf);
                m.box({0, 1.6f, -4.0f}, {3.26f, 0.9f, 2.66f}, Color{255, 220, 170, 253}, xf);
                for (int i = 0; i < 5; ++i) m.box({0, 1.1f, -2.4f + i * 1.2f}, {0.3f, 0.18f, 1.2f}, (i & 1) ? kWhite : kRed, xf);
                for (float x : {-6.0f, 6.0f})
                    for (float z : {-1.8f, 1.8f}) m.box({x, 0.5f, z}, {1.0f, 1.0f, 2.2f}, kConcrete, xf);
                break;
            case PropKind::Watchtower:
                for (float x : {-1.4f, 1.4f})
                    for (float z : {-1.4f, 1.4f}) m.box({x, 3.5f, z}, {0.25f, 7.0f, 0.25f}, Color{110, 96, 76, 255}, xf);
                m.box({0, 7.8f, 0}, {3.6f, 1.6f, 3.6f}, sand, xf);
                m.box({0, 9.6f, 0}, {4.2f, 0.25f, 4.2f}, Color{90, 92, 86, 255}, xf);
                top = 10;
                break;
            case PropKind::Revetment: {
                float L2 = static_cast<float>(p.size);
                m.box({0, 1.2f, 0}, {L2, 2.4f, 3.0f}, sand, xf);
                top = 3; reach = L2 * 0.5f + 2;
                break;
            }
            case PropKind::Hesco: {
                float R = static_cast<float>(p.size);
                int n = static_cast<int>(2 * PI * R / 4.2f);
                for (int i = 0; i < n; ++i) {
                    float a = 2 * PI * (i + 0.5f) / n;
                    if (std::fabs(std::remainder(a, 2 * PI)) < 0.11f) continue;   // the entrance
                    Matrix sx = MatrixMultiply(MatrixMultiply(MatrixRotateY(-a), MatrixTranslate(std::cos(a) * R, 0, std::sin(a) * R)), xf);
                    float g = static_cast<float>(t.height(p.pos.x + std::cos(a + p.yaw) * R, p.pos.z + std::sin(a + p.yaw) * R)) - gy;
                    m.box({0, g + 1.1f, 0}, {1.2f, 2.4f, 4.3f}, sand, sx);
                }
                reach = R + 4; top = 3;
                break;
            }
            case PropKind::FuelBladder: m.cylinder({0, 0, 0}, 4.2f, 1.2f, 12, Color{48, 50, 44, 255}, xf, 3.4f); break;
            case PropKind::Container: m.box({0, 1.3f, 0}, {6.1f, 2.6f, 2.44f}, containerColor(p.variant), xf); break;
            case PropKind::Crane: {
                Color c = p.variant ? Color{200, 60, 44, 255} : kYellow;
                m.box({0, 20.0f, 0}, {2.2f, 40.0f, 2.2f}, c, xf);
                m.box({14.0f, 40.8f, 0}, {34.0f, 1.6f, 1.6f}, c, xf);
                m.box({-7.0f, 40.8f, 0}, {9.0f, 1.6f, 1.8f}, c, xf);
                m.box({-9.5f, 39.0f, 0}, {3.0f, 2.4f, 2.6f}, kConcrete, xf);
                m.box({1.8f, 38.5f, 0}, {1.8f, 2.0f, 2.0f}, kGlass, xf);
                m.box({24.0f, 30.0f, 0}, {0.08f, 20.0f, 0.08f}, kSteel, xf);
                m.box({24.0f, 19.8f, 0}, {1.4f, 0.8f, 1.0f}, kSteel, xf);
                reach = 32; top = 44;
                break;
            }
            case PropKind::WaterTower:
                for (float x : {-2.0f, 2.0f})
                    for (float z : {-2.0f, 2.0f}) m.box({x, 7.0f, z}, {0.35f, 14.0f, 0.35f}, kSteel, xf);
                m.cylinder({0, 14.0f, 0}, 3.6f, 5.0f, 12, Color{196, 200, 196, 255}, xf, 2.4f);
                beacons.push_back(Vector3Transform({0, 19.4f, 0}, xf));
                top = 20;
                break;
            case PropKind::CommandPost:
                m.box({0, 2.5f, 0}, {18.0f, 5.0f, 12.0f}, sand, xf);
                m.box({0, 5.2f, 0}, {18.4f, 0.4f, 12.4f}, shade(sand, 0.8f), xf);
                for (int i = 0; i < 4; ++i) m.box({-6.0f + i * 4.0f, 2.8f, 6.02f}, {1.4f, 1.0f, 0.06f}, Color{255, 214, 160, 253}, xf);
                m.cylinder({6.0f, 5.4f, -3.0f}, 0.12f, 8.0f, 4, kSteel, xf);
                m.cylinder({4.0f, 5.4f, 3.0f}, 1.4f, 0.5f, 10, kWhite, xf, 0.2f);   // satcom dish
                reach = 12;
                break;
            case PropKind::Fence: {
                float L2 = static_cast<float>(p.size);
                Color f = {150, 154, 150, 255};
                Vector3 a0 = Vector3Transform({-L2 / 2, 0, 0}, xf), a1 = Vector3Transform({L2 / 2, 0, 0}, xf);
                Vector3 b0 = Vector3Transform({-L2 / 2, 2.4f, 0}, xf), b1 = Vector3Transform({L2 / 2, 2.4f, 0}, xf);
                m.tri2(a0, a1, b1, f); m.tri2(a0, b1, b0, f);
                for (float x = -L2 / 2; x <= L2 / 2; x += 30.0f) m.box({x, 1.3f, 0}, {0.18f, 2.6f, 0.18f}, kSteel, xf);
                reach = L2 * 0.5f; top = 3;
                break;
            }
            case PropKind::Streetlamp: {
                m.cylinder({0, 0, 0}, 0.16f, 9.0f, 5, Color{120, 124, 126, 255}, xf, 0.1f);
                m.box({1.1f, 9.0f, 0}, {2.4f, 0.14f, 0.14f}, Color{120, 124, 126, 255}, xf);
                m.box({2.2f, 8.85f, 0}, {0.9f, 0.22f, 0.4f}, Color{240, 230, 200, 253}, xf);
                reach = 3; top = 9.5f;
                break;
            }
            case PropKind::Billboard: {
                for (float z : {-3.0f, 3.0f}) m.box({0, 3.0f, z}, {0.4f, 6.0f, 0.4f}, kSteel, xf);
                m.box({0, 7.6f, 0}, {0.45f, 4.4f, 10.0f}, adColor(p.variant), xf);
                m.box({0.24f, 8.6f, 0}, {0.05f, 1.2f, 9.0f}, kWhite, xf);
                m.box({0.24f, 6.6f, -2.0f}, {0.05f, 1.4f, 4.0f}, adColor(p.variant + 3), xf);
                m.box({-0.24f, 7.6f, 0}, {0.05f, 4.2f, 9.8f}, Color{80, 80, 80, 255}, xf);
                reach = 6; top = 10;
                break;
            }
            case PropKind::GasStation: {
                Color brand = adColor(p.variant);
                for (float x : {-6.0f, 6.0f})
                    for (float z : {-3.5f, 3.5f}) m.box({x, 2.5f, z}, {0.4f, 5.0f, 0.4f}, kWhite, xf);
                m.box({0, 5.3f, 0}, {16.0f, 0.7f, 10.0f}, kWhite, xf);
                m.box({0, 5.3f, 0}, {16.1f, 0.4f, 10.1f}, brand, xf);
                m.box({0, 4.9f, 0}, {15.0f, 0.06f, 9.0f}, Color{255, 246, 226, 253}, xf);   // lit canopy underside
                for (float x : {-3.0f, 3.0f}) m.box({x, 0.8f, 0}, {1.0f, 1.6f, 0.6f}, Color{200, 200, 196, 255}, xf);
                m.box({0, 2.0f, -11.0f}, {12.0f, 4.0f, 7.0f}, Color{224, 222, 214, 255}, xf);
                m.box({0, 2.0f, -7.46f}, {10.0f, 2.2f, 0.06f}, Color{255, 236, 200, 253}, xf);
                m.box({9.0f, 4.0f, 6.0f}, {0.4f, 8.0f, 0.4f}, kSteel, xf);
                m.box({9.0f, 8.2f, 6.0f}, {0.5f, 2.4f, 2.4f}, brand, xf);
                reach = 16;
                break;
            }
            case PropKind::ParkedCar: car(m, xf, carColor(p.variant)); reach = 3; top = 2; break;
            case PropKind::Tug: tug(m, xf, Color{214, 170, 40, 255}); reach = 3; top = 2; break;
            case PropKind::Forklift: forklift(m, xf, Color{220, 180, 30, 255}); reach = 2; top = 3; break;
            case PropKind::FireTruck: fireTruck(m, xf); reach = 5; top = 4; break;
            case PropKind::RadarTruck: radarTruck(m, xf, army); reach = 5; top = 6; break;
            case PropKind::AmmoBunker: {
                // earth-covered igloo with a concrete headwall and steel doors
                Color earth = desertArmy ? Color{170, 150, 112, 255} : Color{96, 104, 72, 255};
                int seg = 8;
                for (int k = 0; k < seg; ++k) {
                    float a0 = PI * k / seg, a1 = PI * (k + 1) / seg;
                    Vector3 p0 = Vector3Transform({-12, sinf(a0) * 6.0f, -cosf(a0) * 8.0f}, xf), p1 = Vector3Transform({-12, sinf(a1) * 6.0f, -cosf(a1) * 8.0f}, xf);
                    Vector3 p2 = Vector3Transform({12, sinf(a1) * 6.0f, -cosf(a1) * 8.0f}, xf), p3 = Vector3Transform({12, sinf(a0) * 6.0f, -cosf(a0) * 8.0f}, xf);
                    m.tri2(p0, p1, p2, earth); m.tri2(p0, p2, p3, earth);
                }
                m.box({12.3f, 3.2f, 0}, {0.6f, 6.4f, 17.0f}, kConcrete, xf);
                m.box({12.7f, 2.0f, 0}, {0.2f, 4.0f, 5.0f}, Color{80, 84, 78, 255}, xf);
                reach = 14; top = 7;
                break;
            }
            case PropKind::HeliShelter: {
                // open-front steel shelter with a curved roof
                Color shell = {150, 156, 150, 255};
                int seg = 7;
                for (int k = 0; k < seg; ++k) {
                    float a0 = PI * k / seg, a1 = PI * (k + 1) / seg;
                    Vector3 p0 = Vector3Transform({-cosf(a0) * 14.0f, sinf(a0) * 9.0f, -12}, xf), p1 = Vector3Transform({-cosf(a1) * 14.0f, sinf(a1) * 9.0f, -12}, xf);
                    Vector3 p2 = Vector3Transform({-cosf(a1) * 14.0f, sinf(a1) * 9.0f, 12}, xf), p3 = Vector3Transform({-cosf(a0) * 14.0f, sinf(a0) * 9.0f, 12}, xf);
                    m.tri2(p0, p1, p2, shell); m.tri2(p0, p2, p3, shell);
                }
                m.box({0, 4.5f, -12}, {28, 9, 0.4f}, shade(shell, 0.8f), xf);
                reach = 16; top = 10;
                break;
            }
            case PropKind::CargoStack: {
                // pallets of crates under nets, two high
                for (int k = 0; k < 4; ++k) {
                    float x = (k % 2) * 2.9f - 1.45f, z = (k / 2) * 2.4f - 1.2f;
                    m.box({x, 0.12f, z}, {2.7f, 0.24f, 2.2f}, Color{110, 96, 70, 255}, xf);
                    m.box({x, 1.0f, z}, {2.4f, 1.5f, 2.0f}, (k & 1) ? sand : canvas, xf);
                    if (k < 2) m.box({x, 2.4f, z}, {2.2f, 1.2f, 1.9f}, shade(canvas, 0.9f), xf);
                }
                reach = 4; top = 3.2f;
                break;
            }
            case PropKind::FireStation: {
                m.box({0, 4.0f, 0}, {24, 8, 14}, Color{196, 190, 178, 255}, xf);
                for (int k = 0; k < 3; ++k) m.box({-8.0f + k * 8.0f, 2.8f, 7.05f}, {6.0f, 5.4f, 0.1f}, Color{170, 40, 34, 255}, xf);
                m.box({0, 8.3f, 0}, {24.4f, 0.6f, 14.4f}, kWhite, xf);
                reach = 14; top = 9;
                break;
            }
            case PropKind::ParkedTransport: break;   // drawn with the aircraft model
            case PropKind::TrafficSignal: {
                m.cylinder({0, 0, 0}, 0.12f, 5.6f, 5, Color{70, 72, 74, 255}, xf);
                m.box({1.6f, 5.5f, 0}, {3.2f, 0.12f, 0.12f}, Color{70, 72, 74, 255}, xf);
                m.box({3.0f, 4.9f, 0}, {0.35f, 1.1f, 0.35f}, Color{24, 24, 24, 255}, xf);
                m.box({3.0f, 5.25f, 0.19f}, {0.2f, 0.2f, 0.04f}, Color{220, 40, 30, 253}, xf);
                m.box({3.0f, 4.55f, 0.19f}, {0.2f, 0.2f, 0.04f}, Color{40, 210, 90, 253}, xf);
                reach = 4; top = 6;
                break;
            }
            case PropKind::RoadSign: {
                for (float z : {-1.3f, 1.3f}) m.box({0, 1.4f, z}, {0.12f, 2.8f, 0.12f}, kSteel, xf);
                m.box({0, 3.3f, 0}, {0.1f, 1.4f, 3.2f}, p.variant & 1 ? Color{30, 110, 60, 255} : Color{30, 70, 150, 255}, xf);
                m.box({0.06f, 3.3f, 0}, {0.02f, 0.25f, 2.6f}, kWhite, xf);
                reach = 2; top = 4;
                break;
            }
            case PropKind::Hospital: {
                // ward blocks with lit windows, a red cross and a rooftop helipad
                Color wall = {222, 222, 214, 255};
                m.box({0, 9.0f, 0}, {46, 18, 22}, wall, xf);
                m.box({0, 5.0f, 20}, {30, 10, 18}, shade(wall, 0.95f), xf);
                for (int f2 = 0; f2 < 4; ++f2)
                    for (float sd : {-11.05f, 11.05f})
                        m.box({0, 2.6f + f2 * 4.2f, sd}, {42, 1.5f, 0.08f}, Color{250, 236, 196, static_cast<unsigned char>((f2 + p.variant) % 3 ? 253 : 251)}, xf);
                m.box({0, 18.2f, 0}, {14, 0.3f, 14}, Color{60, 60, 60, 255}, xf);
                m.box({0, 18.4f, 0}, {9, 0.06f, 1.6f}, kWhite, xf);
                m.box({0, 18.4f, 0}, {1.6f, 0.06f, 9}, kWhite, xf);
                m.box({23.1f, 12.0f, 0}, {0.1f, 4.0f, 1.2f}, kRed, xf);
                m.box({23.1f, 12.0f, 0}, {0.1f, 1.2f, 4.0f}, kRed, xf);
                reach = 28; top = 19;
                break;
            }
            case PropKind::Warehouse: {
                Color wall = p.variant & 1 ? Color{170, 172, 168, 255} : Color{188, 176, 150, 255};
                m.box({0, 5.0f, 0}, {48, 10, 26}, wall, xf);
                gable(m, 48, 26, 10, 2.5f, Color{110, 116, 118, 255}, wall, xf);
                for (int k = 0; k < 4; ++k) m.box({-18.0f + k * 12.0f, 2.5f, 13.05f}, {6.0f, 5.0f, 0.1f}, Color{120, 124, 126, 255}, xf);
                reach = 26; top = 13;
                break;
            }
            case PropKind::Bush: {
                Color leaf = p.variant & 1 ? Color{58, 82, 40, 255} : Color{74, 92, 46, 255};
                float r = 1.2f + 0.3f * (p.variant & 3);
                m.sphere({0, r * 0.7f, 0}, r, 6, 5, leaf, xf);
                m.sphere({r * 0.8f, r * 0.5f, r * 0.3f}, r * 0.7f, 5, 4, shade(leaf, 0.9f), xf);
                reach = 3; top = 2.5f;
                break;
            }
        }
        Vector3 c = {static_cast<float>(p.pos.x), gy, static_cast<float>(p.pos.z)};
        B.lo = Vector3Min(B.lo, Vector3Subtract(c, {reach, 1, reach}));
        B.hi = Vector3Max(B.hi, Vector3Add(c, {reach, top, reach}));
        if (m.vertexCount() > 60000) {
            Dressing::Piece pc;
            pc.mesh = m.build();
            pc.center = Vector3Scale(Vector3Add(B.lo, B.hi), 0.5f);
            pc.radius = Vector3Distance(B.lo, B.hi) * 0.5f + 1.0f;
            pc.valid = true;
            out.push_back(pc);
            B = Bucket{};
        }
    }
    for (auto& [k, B] : buckets) {
        if (B.mb.vertexCount() == 0) continue;
        Dressing::Piece pc;
        pc.mesh = B.mb.build();
        pc.center = Vector3Scale(Vector3Add(B.lo, B.hi), 0.5f);
        pc.radius = Vector3Distance(B.lo, B.hi) * 0.5f + 1.0f;
        pc.valid = true;
        out.push_back(pc);
    }
}

}  // namespace sky
