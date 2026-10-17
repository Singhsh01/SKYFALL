#include "Cockpit.hpp"

#include "../mission/Mission.hpp"
#include "../render/Convert.hpp"
#include "../render/MeshBuilder.hpp"
#include "../render/Renderer.hpp"
#include "Font.hpp"
#include "rlgl.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace sky {

using ui::Align;

namespace {
constexpr int kMfd = 320;
const Color kGreen = {110, 255, 140, 255};
const Color kCyan = {120, 220, 255, 255};
std::string fmt(const char* f, double v) { char b[64]; std::snprintf(b, sizeof b, f, v); return b; }

// Panel geometry relative to the eye (x fwd, y up, z right)
const Vector3 kPanelTop = {0.62f, -0.13f, 0.0f};
const Vector3 kPanelBot = {0.55f, -0.50f, 0.0f};
const float kPanelHalfW = 0.50f;

Vector3 panelPoint(float t, float z) {  // t: 0 top .. 1 bottom
    return {kPanelTop.x + (kPanelBot.x - kPanelTop.x) * t, kPanelTop.y + (kPanelBot.y - kPanelTop.y) * t, z};
}

// Displays are drawn in 320-px logical units into textures with twice the pixels (sharper text
// and symbols on high-resolution screens).
constexpr float kMfdScale = 2.0f;
void beginMfd(RenderTexture2D& rt) {
    BeginTextureMode(rt);
    rlPushMatrix();
    rlScalef(kMfdScale, kMfdScale, 1.0f);
}
void endMfd() {
    rlPopMatrix();
    EndTextureMode();
}

void bezel(int x, int y, int w, int h) {
    // physical push buttons around the display edge
    Color b = {70, 72, 70, 255};
    for (int i = 0; i < 5; ++i) {
        int bx = x + 30 + i * (w - 60) / 4 - 9;
        DrawRectangle(bx, y + 2, 18, 9, b);
        DrawRectangle(bx, y + h - 11, 18, 9, b);
        int by = y + 40 + i * (h - 80) / 4 - 5;
        DrawRectangle(x + 2, by, 9, 16, b);
        DrawRectangle(x + w - 11, by, 9, 16, b);
    }
}
}  // namespace

void Cockpit::init() {
    MeshBuilder mb;
    Color panel = {50, 53, 56, 255}, dark = {20, 21, 23, 255}, wall = {64, 67, 70, 255}, console = {52, 55, 58, 255};
    Color frame = {28, 29, 31, 255}, suit = {74, 80, 56, 255}, knob = {120, 122, 120, 255}, white = {200, 200, 195, 255};
    // instrument panel (slanted) with a slightly raised centre section
    mb.quad({kPanelTop.x, kPanelTop.y, -kPanelHalfW}, {kPanelBot.x, kPanelBot.y, -kPanelHalfW}, {kPanelBot.x, kPanelBot.y, kPanelHalfW},
            {kPanelTop.x, kPanelTop.y, kPanelHalfW}, panel);
    // glare shield: thin hood, front lip lower than the HUD
    mb.quad({kPanelTop.x - 0.02f, kPanelTop.y + 0.015f, -kPanelHalfW}, {kPanelTop.x - 0.02f, kPanelTop.y + 0.015f, kPanelHalfW},
            {0.90f, -0.155f, 0.40f}, {0.90f, -0.155f, -0.40f}, dark);
    mb.quad({kPanelTop.x - 0.02f, kPanelTop.y + 0.015f, -kPanelHalfW}, {kPanelTop.x - 0.02f, kPanelTop.y + 0.015f, kPanelHalfW},
            {kPanelTop.x, kPanelTop.y, kPanelHalfW}, {kPanelTop.x, kPanelTop.y, -kPanelHalfW}, frame);
    // panel under-section towards the floor
    mb.quad({kPanelBot.x, kPanelBot.y, -kPanelHalfW}, {0.42f, -0.90f, -kPanelHalfW}, {0.42f, -0.90f, kPanelHalfW}, {kPanelBot.x, kPanelBot.y, kPanelHalfW}, dark);
    // display bezels (raised frames around every screen)
    auto bezelBox = [&](float t0, float t1, float z0, float z1) {
        Vector3 a = panelPoint(t0, z0), c = panelPoint(t1, z1);
        Vector3 ctr = {(a.x + c.x) * 0.5f + 0.008f, (a.y + c.y) * 0.5f, (z0 + z1) * 0.5f};
        float h = std::fabs(a.y - c.y) / 0.982f + 0.035f, w = (z1 - z0) + 0.035f;
        Matrix tilt = MatrixMultiply(MatrixRotateZ(-atan2f(kPanelTop.x - kPanelBot.x, kPanelTop.y - kPanelBot.y)), MatrixTranslate(ctr.x, ctr.y, ctr.z));
        mb.box({0.0f, 0, 0}, {0.018f, h, w}, frame, tilt);
    };
    bezelBox(0.30f, 0.96f, -0.125f, 0.125f);
    bezelBox(0.30f, 0.90f, -0.44f, -0.17f);
    bezelBox(0.30f, 0.90f, 0.17f, 0.44f);
    bezelBox(0.02f, 0.24f, -0.12f, 0.12f);
    bezelBox(0.04f, 0.22f, -0.40f, -0.16f);
    bezelBox(0.04f, 0.22f, 0.16f, 0.40f);
    // small knobs along the panel edges
    for (int i = 0; i < 7; ++i) {
        float z = -0.46f + i * 0.153f;
        Vector3 p = panelPoint(0.985f, z);
        mb.box({p.x - 0.01f, p.y + 0.005f, z}, {0.02f, 0.012f, 0.02f}, knob);
    }
    // side walls, consoles, switch rows, canopy rails
    for (float sg : {-1.0f, 1.0f}) {
        mb.quad({-0.7f, -0.34f, sg * 0.56f}, {1.0f, -0.20f, sg * 0.52f}, {1.0f, -0.95f, sg * 0.52f}, {-0.7f, -0.95f, sg * 0.56f}, wall);
        mb.box({0.05f, -0.50f, sg * 0.43f}, {1.1f, 0.12f, 0.2f}, console);
        for (int i = 0; i < 7; ++i) {
            mb.box({-0.38f + i * 0.12f, -0.438f, sg * 0.43f}, {0.07f, 0.012f, 0.16f}, Color{40, 42, 44, 255});
            for (int k = 0; k < 3; ++k)
                mb.box({-0.38f + i * 0.12f, -0.425f, sg * 0.43f + (k - 1) * 0.045f}, {0.012f, 0.02f, 0.012f}, k == 1 ? white : knob);
        }
        mb.box({0.1f, -0.34f, sg * 0.56f}, {1.8f, 0.04f, 0.05f}, frame);
        // canopy sill padding
        mb.box({0.35f, -0.30f, sg * 0.545f}, {0.9f, 0.05f, 0.03f}, Color{36, 37, 38, 255});
    }
    // pilot legs (knees towards the panel)
    for (float sg : {-1.0f, 1.0f}) {
        Matrix leg = MatrixMultiply(MatrixRotateZ(0.25f), MatrixTranslate(0.28f, -0.68f, sg * 0.14f));
        mb.box({0, 0, 0}, {0.55f, 0.16f, 0.17f}, suit, leg);
        mb.box({0, -0.05f, 0}, {0.2f, 0.06f, 0.18f}, Color{60, 66, 46, 255}, MatrixMultiply(MatrixTranslate(0.12f, 0.08f, 0), leg));
    }
    // windscreen frame: A-pillars from the glare-shield corners and the top bow
    for (float sg : {-1.0f, 1.0f}) {
        Vector3 a = {0.64f, -0.14f, sg * 0.50f}, b = {-0.15f, 0.55f, sg * 0.44f};
        Vector3 d = Vector3Subtract(b, a);
        float len = Vector3Length(d);
        Matrix rot = MatrixMultiply(MatrixRotateZ(atan2f(d.y, d.x) - PI / 2), MatrixRotateX(-atan2f(d.z, sqrtf(d.x * d.x + d.y * d.y)) * 0.0f));
        mb.box({0, len * 0.5f, 0}, {0.022f, len, 0.03f}, frame, MatrixMultiply(rot, MatrixTranslate(a.x, a.y, a.z)));

    }
    mb.box({-0.15f, 0.56f, 0.0f}, {0.03f, 0.025f, 0.9f}, frame);
    // HUD combiner frame posts
    mb.box({0.70f, -0.03f, -0.205f}, {0.02f, 0.2f, 0.015f}, frame);
    mb.box({0.70f, -0.03f, 0.205f}, {0.02f, 0.2f, 0.015f}, frame);
    mb.box({0.70f, -0.125f, 0.0f}, {0.08f, 0.025f, 0.44f}, frame);
    panel_ = mb.build();

    // backlit switch caps, annunciators and the master caution (drawn emissive)
    MeshBuilder lb;
    const Color amber = {255, 170, 40, 255}, green = {90, 255, 120, 255}, red = {255, 50, 40, 255}, cyan = {120, 220, 255, 255};
    for (float sg : {-1.0f, 1.0f})
        for (int i = 0; i < 7; ++i)
            for (int k = 0; k < 3; ++k) {
                Color c = (i + k) % 4 == 0 ? amber : green;
                lb.box({-0.38f + i * 0.12f + 0.02f, -0.432f, sg * 0.43f + (k - 1) * 0.045f}, {0.012f, 0.004f, 0.02f}, c);
            }
    for (int i = 0; i < 6; ++i) {   // annunciator strip under the glare shield
        Vector3 p = panelPoint(0.02f, -0.36f + i * 0.04f);
        lb.box({p.x - 0.004f, p.y - 0.01f, p.z}, {0.004f, 0.014f, 0.03f}, i == 2 ? amber : (i == 4 ? cyan : green));
    }
    for (float sg : {-1.0f, 1.0f}) {
        Vector3 p = panelPoint(0.03f, sg * 0.46f);
        lb.box({p.x - 0.006f, p.y - 0.012f, p.z}, {0.006f, 0.03f, 0.045f}, sg < 0 ? red : amber);   // MASTER CAUTION / WARNING
    }
    lamps_ = lb.build();

    MeshBuilder sb;  // side stick (right console) — local origin at pivot
    sb.cylinder({0, 0, 0}, 0.018f, 0.13f, 8, Color{30, 30, 30, 255});
    sb.box({0.01f, 0.15f, 0}, {0.05f, 0.06f, 0.04f}, Color{25, 25, 25, 255});
    sb.box({0.035f, 0.17f, 0}, {0.01f, 0.012f, 0.012f}, Color{200, 40, 30, 255});
    stick_ = sb.build();
    MeshBuilder tb;  // throttle (left console)
    tb.box({0, 0.06f, 0}, {0.03f, 0.12f, 0.03f}, Color{30, 30, 30, 255});
    tb.box({0, 0.13f, 0}, {0.08f, 0.05f, 0.05f}, Color{24, 24, 24, 255});
    throttle_ = tb.build();
    for (int i = 0; i < 5; ++i) {
        int sc = static_cast<int>(kMfdScale);
        mfd_[i] = i < 3 ? LoadRenderTexture(kMfd * sc, kMfd * sc) : LoadRenderTexture(384 * sc, 160 * sc);
        SetTextureFilter(mfd_[i].texture, TEXTURE_FILTER_BILINEAR);
    }
    ready_ = true;
}

void Cockpit::unload() {
    if (!ready_) return;
    UnloadMesh(panel_); UnloadMesh(stick_); UnloadMesh(throttle_); UnloadMesh(lamps_);
    for (auto& m : mfd_) UnloadRenderTexture(m);
    ready_ = false;
}

void Cockpit::drawRadar(const Battle& b, const Jet& p) {
    beginMfd(mfd_[0]);
    ClearBackground(Color{4, 10, 8, 255});
    const FlightState& s = p.fm->state();
    float cx = kMfd / 2.0f, cy = kMfd * 0.82f;
    float range = 20000.0f, pxPerM = (kMfd * 0.7f) / range;
    for (int k = 1; k <= 4; ++k) DrawRingLines({cx, cy}, k * kMfd * 0.175f, k * kMfd * 0.175f, 200, 340, 32, Color{60, 160, 90, 160});
    DrawLineEx({cx, cy}, {cx - kMfd * 0.42f, cy - kMfd * 0.55f}, 1, Color{60, 160, 90, 120});
    DrawLineEx({cx, cy}, {cx + kMfd * 0.42f, cy - kMfd * 0.55f}, 1, Color{60, 160, 90, 120});
    double hdg = compassHeadingDeg(s.q) * DEG2RAD;
    Vector2 fw = {static_cast<float>(std::sin(hdg)), static_cast<float>(-std::cos(hdg))};
    Vector2 rt = {static_cast<float>(std::cos(hdg)), static_cast<float>(std::sin(hdg))};
    for (const auto& jp : b.jets()) {
        if (jp->isPlayer || !jp->alive) continue;
        Vec3 d = jp->fm->state().pos - s.pos;
        float x = static_cast<float>(d.x * rt.x + d.z * rt.y) * pxPerM, y = static_cast<float>(d.x * fw.x + d.z * fw.y) * pxPerM;
        if (y < -10 || std::fabs(x) > kMfd * 0.5f || y > kMfd * 0.8f) continue;
        Color c = jp->team == Team::Red ? Color{255, 80, 60, 255} : kCyan;
        Vector2 q = {cx + x, cy - y};
        if (jp->team == Team::Red) DrawRectangleLinesEx({q.x - 6, q.y - 6, 12, 12}, 2, c);
        else DrawCircleLinesV(q, 6, c);
        if (p.lockTarget == jp->id) DrawRing(q, 9, 11, 0, 360, 16, Color{255, 220, 80, 255});
    }
    DrawTriangle({cx, cy - 10}, {cx - 7, cy + 6}, {cx + 7, cy + 6}, kGreen);
    ui::text("TSD", 16, 18, 20, kGreen);
    ui::text("20", kMfd - 18.0f, 18, 18, kGreen, Align::Right);
    ui::text(fmt("HDG %03.0f", compassHeadingDeg(s.q)), cx, kMfd - 34.0f, 18, kGreen, Align::Center);
    bezel(0, 0, kMfd, kMfd);
    endMfd();
}

void Cockpit::drawPfd(const Jet& p) {
    beginMfd(mfd_[1]);
    const FlightState& s = p.fm->state();
    Euler e = toEuler(s.q);
    float cx = kMfd / 2.0f, cy = kMfd / 2.0f;
    ClearBackground(Color{90, 60, 30, 255});
    float pxPerDeg = 4.0f;
    float off = static_cast<float>(e.pitch / kDeg) * pxPerDeg;
    float bank = static_cast<float>(e.bank / kDeg);
    // sky half as a large rotated rectangle
    Vector2 up = {sinf(-bank * DEG2RAD), -cosf(-bank * DEG2RAD)};
    Vector2 hc = {cx - up.x * off, cy - up.y * off};
    DrawRectanglePro({hc.x, hc.y, 1200, 1200}, {600, 1200}, -bank, Color{40, 110, 200, 255});
    // pitch lines
    for (int a = -60; a <= 60; a += 10) {
        if (a == 0) continue;
        float d = (a - static_cast<float>(e.pitch / kDeg)) * pxPerDeg;
        Vector2 c = {cx + up.x * d * -1 + 0, cy + up.y * d * -1};
        c = {cx - up.x * -d, cy - up.y * -d};
        c = {cx + up.x * d, cy + up.y * d};
        Vector2 r = {-up.y, up.x};
        float hw = 26;
        DrawLineEx({c.x - r.x * hw, c.y - r.y * hw}, {c.x + r.x * hw, c.y + r.y * hw}, 2, WHITE);
    }
    DrawLineEx({hc.x + up.y * 400, hc.y - up.x * 400}, {hc.x - up.y * 400, hc.y + up.x * 400}, 3, WHITE);
    // aircraft symbol
    DrawRectangle(static_cast<int>(cx - 60), static_cast<int>(cy - 3), 40, 6, Color{255, 220, 60, 255});
    DrawRectangle(static_cast<int>(cx + 20), static_cast<int>(cy - 3), 40, 6, Color{255, 220, 60, 255});
    DrawCircle(static_cast<int>(cx), static_cast<int>(cy), 5, Color{255, 220, 60, 255});
    // speed / alt boxes
    DrawRectangle(8, static_cast<int>(cy - 16), 74, 32, Color{0, 0, 0, 200});
    DrawRectangle(kMfd - 90, static_cast<int>(cy - 16), 82, 32, Color{0, 0, 0, 200});
    ui::text(fmt("%.0f", s.airspeed * 1.943844), 76, cy - 12, 24, WHITE, Align::Right);
    ui::text(fmt("%.0f", s.pos.y * 3.28084), kMfd - 14.0f, cy - 12, 24, WHITE, Align::Right);
    DrawRectangle(static_cast<int>(cx - 34), 8, 68, 28, Color{0, 0, 0, 200});
    ui::text(fmt("%03.0f", compassHeadingDeg(s.q)), cx, 10, 24, WHITE, Align::Center);
    ui::text(fmt("G %.1f", s.gLoad), 12, kMfd - 34.0f, 20, kGreen);
    ui::text(fmt("M %.2f", s.mach), kMfd - 12.0f, kMfd - 34.0f, 20, kGreen, Align::Right);
    bezel(0, 0, kMfd, kMfd);
    endMfd();
}

void Cockpit::drawEngine(const Jet& p, double time) {
    beginMfd(mfd_[2]);
    ClearBackground(Color{4, 8, 10, 255});
    const FlightState& s = p.fm->state();
    auto gauge = [&](float x, float y, float r, double v, const char* lbl, Color c) {
        DrawRing({x, y}, r - 4, r, 135, 405, 40, Color{60, 70, 70, 255});
        DrawRing({x, y}, r - 4, r, 135, 135 + static_cast<float>(std::clamp(v, 0.0, 1.1)) * 270.0f, 40, c);
        ui::text(fmt("%.0f", v * 100.0), x, y - 12, 24, WHITE, Align::Center);
        ui::text(lbl, x, y + 18, 16, Color{160, 170, 170, 255}, Align::Center);
    };
    gauge(90, 92, 56, s.rpm, "RPM %", kGreen);
    gauge(230, 92, 56, s.thrustN / std::max(1.0, p.fm->profile().thrustAbN), "THRUST", s.abLevel > 0.1 ? Color{255, 150, 40, 255} : kGreen);
    ui::text(fmt("FUEL %.0f KG", s.fuelKg), 20, 172, 22, WHITE);
    const char* eng = s.engine == EngineState::Off ? "ENG OFF" : s.engine == EngineState::Starting ? "START" : s.engine == EngineState::Flameout ? "FLAMEOUT" : "RUN";
    ui::text(eng, 20, 202, 20, s.engine == EngineState::Running ? kGreen : Color{255, 170, 40, 255});
    if (s.abLevel > 0.1) ui::text("AB", 140, 202, 20, Color{255, 150, 40, 255});
    // gear lights
    for (int i = 0; i < 3; ++i) {
        Color gc = s.gearPos >= 1.0 ? Color{60, 230, 90, 255} : (s.gearPos > 0.0 ? Color{230, 60, 40, 255} : Color{40, 50, 45, 255});
        if (s.gearPos > 0.0 && s.gearPos < 1.0 && std::fmod(time, 0.5) < 0.25) gc = Color{40, 50, 45, 255};
        float gx = 200 + i * 34.0f, gy = i == 0 ? 210.0f : 236.0f;
        DrawCircle(static_cast<int>(gx), static_cast<int>(gy), 12, gc);
    }
    ui::text("GEAR", 234, 256, 16, Color{160, 170, 170, 255}, Align::Center);
    ui::text(s.parkingBrake ? "PARK BRK" : (s.wheelBrake ? "BRAKE" : ""), 20, 232, 20, Color{255, 170, 40, 255});
    ui::text(fmt("HULL %.0f%%", p.health), 20, 262, 20, p.health > 60 ? kGreen : Color{255, 80, 60, 255});
    bezel(0, 0, kMfd, kMfd);
    endMfd();
}

void Cockpit::drawUfc(const Jet& p, double time) {
    beginMfd(mfd_[3]);
    ClearBackground(Color{8, 12, 10, 255});
    const char* keys = "123456789*0#";
    for (int i = 0; i < 12; ++i) {
        int cx = 200 + (i % 3) * 58, cy = 14 + (i / 3) * 36;
        DrawRectangle(cx, cy, 50, 30, Color{36, 40, 38, 255});
        DrawRectangleLines(cx, cy, 50, 30, Color{70, 80, 74, 255});
        char k[2] = {keys[i], 0};
        ui::text(k, cx + 25.0f, cy + 4.0f, 20, kGreen, Align::Center);
    }
    const FlightState& s = p.fm->state();
    ui::text("COM1 121.50", 12, 14, 20, kGreen);
    ui::text("COM2 243.00", 12, 40, 20, kGreen);
    ui::text(fmt("IFF  %04.0f", 7700.0 - 6500.0), 12, 66, 20, kGreen);
    ui::text(fmt("BARO %.2f", 29.92), 12, 92, 20, kGreen);
    ui::text(s.onGround ? "WOW  ON" : "WOW  OFF", 12, 118, 20, s.onGround ? Color{255, 190, 60, 255} : kGreen);
    (void)time;
    endMfd();
}

void Cockpit::drawCni(const Jet& p, double time) {
    beginMfd(mfd_[4]);
    ClearBackground(Color{6, 9, 8, 255});
    const FlightState& s = p.fm->state();
    ui::text(fmt("FUEL %5.0f", s.fuelKg), 14, 12, 22, kGreen);
    ui::text(fmt("BINGO %4.0f", 1500.0), 14, 40, 22, kGreen);
    ui::text(fmt("THR  %3.0f%%", s.throttle * 100.0), 14, 68, 22, kGreen);
    ui::text(fmt("AOA  %4.1f", s.alpha / kDeg), 200, 12, 22, kGreen);
    ui::text(fmt("NZ   %4.1f", s.gLoad), 200, 40, 22, s.gLoad > 8 ? Color{255, 190, 60, 255} : kGreen);
    ui::text(fmt("MSL  %.0f", static_cast<double>(p.missiles)), 200, 68, 22, kGreen);
    if (p.lockProgress >= 1.0 && std::fmod(time, 0.6) < 0.4) ui::text("LOCK", 200, 100, 26, Color{255, 90, 70, 255});
    else ui::text(s.engine == EngineState::Running ? "ENG RUN" : "ENG OFF", 14, 104, 22, s.engine == EngineState::Running ? kGreen : Color{255, 190, 60, 255});
    endMfd();
}

void Cockpit::updateDisplays(const Battle& b, const Jet& player, double time, bool) {
    if (!ready_) return;
    drawRadar(b, player);
    drawPfd(player);
    drawEngine(player, time);
    drawUfc(player, time);
    drawCni(player, time);
}

void Cockpit::draw3D(Renderer& r, const Jet& player, const FrameParams& f, Vec3 origin) {
    if (!ready_) return;
    const FlightState& s = player.fm->state();
    Matrix M = bodyMatrix(s.pos - origin, s.q);
    Matrix E = MatrixMultiply(MatrixTranslate(eye_.x, eye_.y, eye_.z), M);
    Prog& lit = r.lit();
    r.setAtmosphere(lit, f);
    lit.setF("uEmissive", 0.06f);
    lit.setF("uSpecular", 0.25f);
    lit.setF("uFogOn", 0.0f);
    Material& mat = r.litMaterial();
    mat.maps[MATERIAL_MAP_DIFFUSE].color = WHITE;
    rlDisableBackfaceCulling();
    DrawMesh(panel_, mat, E);
    // side stick on the right console: tilts with pitch/roll input
    Matrix st = MatrixMultiply(MatrixMultiply(MatrixRotateX(static_cast<float>(player.lastInput.roll * 0.25)),
                                              MatrixRotateZ(static_cast<float>(-player.lastInput.pitch * 0.25))),
                               MatrixTranslate(0.20f, -0.44f, 0.40f));
    DrawMesh(stick_, mat, MatrixMultiply(st, E));
    // throttle on the left console: slides forward with the lever
    Matrix th = MatrixTranslate(-0.05f + static_cast<float>(s.throttle) * 0.16f, -0.44f, -0.40f);
    DrawMesh(throttle_, mat, MatrixMultiply(th, E));
    lit.setF("uEmissive", 0.85f);
    DrawMesh(lamps_, mat, E);
    lit.setF("uEmissive", 0.06f);
    rlEnableBackfaceCulling();

    // MFD screens as emissive textured quads
    Prog& ps = r.particle();
    BeginShaderMode(ps.sh);
    ps.setF("uIntensity", 1.6f);
    rlDisableBackfaceCulling();
    auto screen = [&](int idx, float t0, float t1, float z0, float z1) {
        Vector3 a = panelPoint(t0, z0), b = panelPoint(t0, z1), c = panelPoint(t1, z1), d = panelPoint(t1, z0);
        auto tr = [&](Vector3 p) { p.x -= 0.006f; return Vector3Transform(p, E); };
        a = tr(a); b = tr(b); c = tr(c); d = tr(d);
        rlSetTexture(mfd_[idx].texture.id);
        rlBegin(RL_QUADS);
        rlColor4ub(255, 255, 255, 255);
        rlTexCoord2f(0, 1); rlVertex3f(a.x, a.y, a.z);
        rlTexCoord2f(0, 0); rlVertex3f(d.x, d.y, d.z);
        rlTexCoord2f(1, 0); rlVertex3f(c.x, c.y, c.z);
        rlTexCoord2f(1, 1); rlVertex3f(b.x, b.y, b.z);
        rlEnd();
        rlSetTexture(0);
    };
    screen(0, 0.32f, 0.88f, -0.43f, -0.18f);
    screen(1, 0.32f, 0.94f, -0.115f, 0.115f);
    screen(2, 0.32f, 0.88f, 0.18f, 0.43f);
    screen(3, 0.03f, 0.23f, -0.11f, 0.11f);
    screen(4, 0.05f, 0.21f, -0.39f, -0.17f);
    screen(4, 0.05f, 0.21f, 0.17f, 0.39f);
    rlDrawRenderBatchActive();
    // canopy reflections: the glowing displays mirrored faintly on the windscreen sides
    BeginBlendMode(BLEND_ADDITIVE);
    ps.setF("uIntensity", 0.045f);
    auto reflect = [&](int idx, float sg) {
        Vector3 a = {0.58f, -0.10f, sg * 0.44f}, b = {0.58f, -0.10f, sg * 0.25f}, c = {0.50f, 0.03f, sg * 0.25f}, d = {0.50f, 0.03f, sg * 0.43f};
        a = Vector3Transform(a, E); b = Vector3Transform(b, E); c = Vector3Transform(c, E); d = Vector3Transform(d, E);
        rlSetTexture(mfd_[idx].texture.id);
        rlBegin(RL_QUADS);
        rlColor4ub(255, 255, 255, 255);
        rlTexCoord2f(0, 0); rlVertex3f(a.x, a.y, a.z);
        rlTexCoord2f(1, 0); rlVertex3f(b.x, b.y, b.z);
        rlTexCoord2f(1, 1); rlVertex3f(c.x, c.y, c.z);
        rlTexCoord2f(0, 1); rlVertex3f(d.x, d.y, d.z);
        rlEnd();
        rlSetTexture(0);
    };
    reflect(0, -1.0f);
    reflect(2, 1.0f);
    rlDrawRenderBatchActive();
    EndBlendMode();
    // HUD combiner glass (faint green tint)
    BeginBlendMode(BLEND_ALPHA);
    rlBegin(RL_QUADS);
    rlColor4ub(60, 120, 90, 40);
    Vector3 g0 = Vector3Transform({0.70f, -0.11f, -0.195f}, E), g1 = Vector3Transform({0.70f, -0.11f, 0.195f}, E);
    Vector3 g2 = Vector3Transform({0.69f, 0.13f, 0.195f}, E), g3 = Vector3Transform({0.69f, 0.13f, -0.195f}, E);
    rlTexCoord2f(0, 0); rlVertex3f(g0.x, g0.y, g0.z);
    rlTexCoord2f(1, 0); rlVertex3f(g1.x, g1.y, g1.z);
    rlTexCoord2f(1, 1); rlVertex3f(g2.x, g2.y, g2.z);
    rlTexCoord2f(0, 1); rlVertex3f(g3.x, g3.y, g3.z);
    rlEnd();
    rlDrawRenderBatchActive();
    EndBlendMode();
    rlEnableBackfaceCulling();
    EndShaderMode();
}

void Cockpit::debugExport(const char* dir) const {
    for (int i = 0; i < 5; ++i) {
        Image im = LoadImageFromTexture(mfd_[i].texture);
        char p[256];
        std::snprintf(p, sizeof p, "%s/mfd%d.png", dir, i);
        ExportImage(im, p);
        UnloadImage(im);
    }
}

Rectangle Cockpit::combinerRect(const Camera3D& cam, const Jet& player, int w, int h) const {
    const FlightState& s = player.fm->state();
    Matrix M = bodyMatrix(s.pos, s.q);
    Matrix E = MatrixMultiply(MatrixTranslate(eye_.x, eye_.y, eye_.z), M);
    Vector3 pts[4] = {{0.70f, -0.11f, -0.195f}, {0.70f, -0.11f, 0.195f}, {0.69f, 0.13f, 0.195f}, {0.69f, 0.13f, -0.195f}};
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (auto& p : pts) {
        Vector2 sp = GetWorldToScreenEx(Vector3Transform(p, E), cam, w, h);
        x0 = std::min(x0, sp.x); y0 = std::min(y0, sp.y); x1 = std::max(x1, sp.x); y1 = std::max(y1, sp.y);
    }
    x0 = std::max(x0, 0.0f); y0 = std::max(y0, 0.0f); x1 = std::min(x1, static_cast<float>(w)); y1 = std::min(y1, static_cast<float>(h));
    if (x1 <= x0 || y1 <= y0) return {0, 0, 0, 0};
    return {x0, y0, x1 - x0, y1 - y0};
}

}  // namespace sky
