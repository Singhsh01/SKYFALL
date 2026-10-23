#include "Hud.hpp"

#include "../render/Convert.hpp"
#include "Font.hpp"
#include "../sim/Terrain.hpp"
#include "raymath.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace sky {

using ui::Align;

namespace {
const Color kHud = {150, 255, 205, 235};
const Color kHudDim = {150, 255, 205, 120};
const Color kEnemy = {255, 70, 60, 240};
const Color kAlly = {90, 170, 255, 235};
const Color kWarn = {255, 170, 40, 245};
const Color kPanel = {8, 14, 18, 150};
const Color kPanelBorder = {150, 255, 205, 60};

std::string fmt(const char* f, double v) { char b[64]; std::snprintf(b, sizeof b, f, v); return b; }
std::string fmt2(const char* f, int a, int b2) { char b[96]; std::snprintf(b, sizeof b, f, a, b2); return b; }
std::string fmti(const char* f, int v) { char b[64]; std::snprintf(b, sizeof b, f, v); return b; }

/// Odometer-style rolling readout: every digit is a drum; the lowest drum rolls continuously
/// and higher drums roll over (carry) as the lower ones pass 9 -> 0. `step` quantises the
/// rolling unit (10 => the last digit is a fixed '0' and the tens drum rolls).
void drawRoller(Rectangle box, double value, int digits, int step, float size, Color col, Color bg, bool clipBlank = true) {
    double v = std::max(0.0, value) / step;
    float digitW = ui::measure("0", size) + size * 0.06f;
    int fixedTail = step == 10 ? 1 : 0;
    float right = box.x + box.width - 6.0f - (fixedTail ? digitW : 0.0f);
    DrawRectangleRec(box, bg);
    DrawRectangleLinesEx(box, 1.0f, col);
    float cy = box.y + box.height * 0.5f - size * 0.5f;
    float lineH = std::max(size * 1.05f, box.height * 0.92f);   // the next digit stays hidden until it rolls in
    BeginScissorMode(static_cast<int>(box.x), static_cast<int>(box.y + 1), static_cast<int>(box.width), static_cast<int>(box.height - 2));
    double pow10 = 1.0;
    long long total = static_cast<long long>(std::floor(v));
    for (int k = 0; k < digits; ++k) {
        long long d = (total / static_cast<long long>(pow10)) % 10;
        double below = std::fmod(v, pow10);                       // value held by the lower drums
        // each drum holds its digit, then rolls through the last 30 % of the step (mechanical counter)
        double frac = k == 0 ? v - std::floor(v) : std::clamp((below - (pow10 - 1.0)), 0.0, 1.0);
        double f = std::clamp((frac - 0.7) / 0.3, 0.0, 1.0);
        f = f * f * (3.0 - 2.0 * f);
        float x = right - (k + 1) * digitW;
        bool leading = clipBlank && k > 0 && total < static_cast<long long>(pow10);  // drum still above the number
        char a[2] = {static_cast<char>('0' + d), 0}, b[2] = {static_cast<char>('0' + (d + 1) % 10), 0};
        if (!leading) ui::text(a, x, cy - static_cast<float>(f) * lineH, size, col);
        if (!leading || f > 0.0) ui::text(b, x, cy + (1.0f - static_cast<float>(f)) * lineH, size, col);
        pow10 *= 10.0;
    }
    if (fixedTail) ui::text("0", right, cy, size, col);
    EndScissorMode();
    // drum shading: darker band top and bottom
    DrawRectangleGradientV(static_cast<int>(box.x + 1), static_cast<int>(box.y + 1), static_cast<int>(box.width - 2), static_cast<int>(box.height * 0.28f), Color{0, 0, 0, 120}, Color{0, 0, 0, 0});
    DrawRectangleGradientV(static_cast<int>(box.x + 1), static_cast<int>(box.y + box.height * 0.72f), static_cast<int>(box.width - 2), static_cast<int>(box.height * 0.28f - 1), Color{0, 0, 0, 0}, Color{0, 0, 0, 120});
}

/// Vertical moving scale behind a roller (major ticks labelled).
void drawTape(float x, float cy, float halfH, double value, double step, int labelEvery, bool leftSide, float s, Color col) {
    float pxPer = 26.0f * s / static_cast<float>(step);
    double base = std::floor(value / step) * step;
    int n = static_cast<int>(halfH / (26.0f * s)) + 2;
    for (int k = -n; k <= n; ++k) {
        double v = base + k * step;
        if (v < 0) continue;
        float y = cy - static_cast<float>((v - value) * pxPer);
        if (std::fabs(y - cy) > halfH) continue;
        float fade = 1.0f - std::fabs(y - cy) / halfH;
        bool major = static_cast<long long>(std::llround(v / step)) % labelEvery == 0;
        float len = (major ? 11.0f : 6.0f) * s;
        Color c = ui::withAlpha(col, 0.25f + 0.75f * fade);
        ui::line({leftSide ? x - len : x, y}, {leftSide ? x : x + len, y}, 1.4f * s, c, false);
        if (major) {
            char b[16];
            std::snprintf(b, sizeof b, "%d", static_cast<int>(std::llround(v)));
            ui::text(b, leftSide ? x - len - 4 * s : x + len + 4 * s, y - 6 * s, 11.5f * s, c, leftSide ? Align::Right : Align::Left);
        }
    }
}

/// Greedy word wrap for HUD text.
std::vector<std::string> wrapText(const std::string& text, float size, float maxW, bool bold = false) {
    std::vector<std::string> out;
    std::string line, word;
    auto flush = [&]() {
        if (word.empty()) return;
        std::string cand = line.empty() ? word : line + " " + word;
        if (ui::measure(cand, size, bold) > maxW && !line.empty()) { out.push_back(line); line = word; }
        else line = cand;
        word.clear();
    };
    for (char ch : text) { if (ch == ' ') flush(); else word += ch; }
    flush();
    if (!line.empty()) out.push_back(line);
    return out;
}

/// Size of the non-critical information panels relative to their original design (0.55 by
/// default: a wider view of the battlefield). The minimap, the replay windows and warnings keep
/// their size.
float panelK(const HudContext& c) { return c.settings ? static_cast<float>(c.settings->hudPanelScale) : 0.55f; }
/// Font size for a panel: shrinks with the panel, but never below ~10.5 px at 720p (scaled with
/// the screen, so it stays readable at 4K too).
float pfont(float pt, float s, float k) { return std::max(pt * s * k, std::min(pt, 10.5f) * s); }

const Color kObjective = {255, 176, 60, 255};
const Color kFriendlyAsset = {110, 190, 255, 255};
}  // namespace

void Hud::update(double dt) {
    time_ += dt;
    hit_ = std::max(0.0, hit_ - dt);
    for (auto& r : radio_) r.t += dt;
    while (!radio_.empty() && radio_.front().t > radio_.front().dur) radio_.pop_front();
    for (auto& p : popups_) p.t += dt;
    while (!popups_.empty() && popups_.front().t > 2.4) popups_.pop_front();
}

void Hud::radio(const std::string& who, const std::string& text, double dur) {
    radio_.push_back({who, text, 0, dur});
    if (radio_.size() > 3) radio_.pop_front();
}

void Hud::popup(const std::string& text, Color col) {
    popups_.push_back({text, 0, col});
    if (popups_.size() > 4) popups_.pop_front();
}

Vector2 Hud::project(const HudContext& c, Vector3 p, bool* inFront) const {
    Vector3 fwd = Vector3Normalize(Vector3Subtract(c.cam.target, c.cam.position));
    if (inFront) *inFront = Vector3DotProduct(Vector3Subtract(p, c.cam.position), fwd) > 0.5f;
    return GetWorldToScreenEx(p, c.cam, c.w, c.h);
}

void Hud::draw(const HudContext& c) {
    if (!c.player) return;
    if (c.replay) { replayOverlay(c); return; }
    bool cockpit = c.camMode == CameraMode::Cockpit;
    float s = static_cast<float>(c.h) / 720.0f * static_cast<float>(c.settings ? c.settings->uiScale : 1.0);
    if (!c.dead) {
        if (cockpit && c.combiner.width > 10) {
            BeginScissorMode(static_cast<int>(c.combiner.x), static_cast<int>(c.combiner.y), static_cast<int>(c.combiner.width), static_cast<int>(c.combiner.height));
            pitchLadder(c);
            flightSymbology(c, true);
            EndScissorMode();
        } else if (!cockpit) {
            if (c.settings && c.settings->hudStyle == 0) minimalSymbology(c);
            else flightSymbology(c, false);
        }
        targets(c);
        if (c.training) trainingMarkers(c);
        else objectiveMarkers(c);
        weapons(c);
        minimap(c);
        warnings(c);
    }
    killFeed(c);
    if (c.training) {
        trainingPanel(c);
        if (!c.dead) trainingApproach(c);
        if (c.trainingDone) trainingDebrief(c);
    } else {
        missionPanel(c);
    }
    if (c.showGlidePath && !c.dead && !c.training) glidePath(c);
    radioPanel(c);
    popups(c);
    // hit marker
    if (hit_ > 0) {
        Vector2 ctr = {c.w * 0.5f, c.h * 0.5f};
        if (!cockpit) {
            bool f;
            Vector3 aim = toRl(c.player->fm->state().pos + c.player->fm->state().q.forward() * 1000.0);
            ctr = project(c, aim, &f);
        }
        Color hc = killHit_ ? kEnemy : Color{255, 255, 255, 240};
        float a = static_cast<float>(hit_ / 0.25);
        hc = ui::withAlpha(hc, a);
        float r0 = 10 * s, r1 = 22 * s;
        for (int k = 0; k < 4; ++k) {
            float ang = PI / 4 + k * PI / 2;
            ui::line({ctr.x + cosf(ang) * r0, ctr.y + sinf(ang) * r0}, {ctr.x + cosf(ang) * r1, ctr.y + sinf(ang) * r1}, 2.5f * s, hc);
        }
    }
    if (c.dead) {
        DrawRectangle(0, 0, c.w, c.h, Color{0, 0, 0, 90});
        ui::textShadow(c.deathReason.empty() ? "AIRCRAFT LOST" : c.deathReason, c.w * 0.5f, c.h * 0.40f, 34 * s, kEnemy, Align::Center);
        if (c.respawnTimer > 0)
            ui::textShadow(fmt("RESPAWNING IN %.0f", std::ceil(c.respawnTimer)), c.w * 0.5f, c.h * 0.40f + 46 * s, 22 * s, WHITE, Align::Center);
        else
            ui::textShadow(c.training ? "PRESS  R  TO RETRY FROM THE LAST CHECKPOINT     ESC  FOR MENU" : "PRESS  R  TO RETRY     ESC  FOR MENU",
                           c.w * 0.5f, c.h * 0.40f + 46 * s, 22 * s, WHITE, Align::Center);
    }
    if (c.pipActive) pipPanel(c);
    if (c.killActive) killPanel(c);
    if (!c.dead && !c.showHelp) hintsBar(c);
    if (c.showDebug) debug(c);
    if (c.showHelp) help(c);
}

void Hud::pitchLadder(const HudContext& c) {
    const FlightState& st = c.player->fm->state();
    Vector3 cam = c.cam.position;
    Vec3 f = st.q.forward();
    Vector3 h = Vector3Normalize({static_cast<float>(f.x), 0, static_cast<float>(f.z)});
    Vector3 rt = {-h.z, 0, h.x};
    Vector3 up = {0, 1, 0};
    Euler e = toEuler(st.q);
    float pitchDeg = static_cast<float>(e.pitch / kDeg);
    float s = c.h / 720.0f;
    for (int a = -90; a <= 90; a += 5) {
        if (std::fabs(a - pitchDeg) > 22) continue;
        float th = a * DEG2RAD;
        Vector3 d = Vector3Add(Vector3Scale(h, cosf(th)), Vector3Scale(up, sinf(th)));
        float half = a == 0 ? 0.22f : 0.085f, gap = a == 0 ? 0.03f : 0.025f;
        bool f1, f2;
        Vector2 L0 = project(c, Vector3Add(cam, Vector3Scale(Vector3Add(d, Vector3Scale(rt, -half)), 1000)), &f1);
        Vector2 L1 = project(c, Vector3Add(cam, Vector3Scale(Vector3Add(d, Vector3Scale(rt, -gap)), 1000)), &f2);
        Vector2 R0 = project(c, Vector3Add(cam, Vector3Scale(Vector3Add(d, Vector3Scale(rt, gap)), 1000)));
        Vector2 R1 = project(c, Vector3Add(cam, Vector3Scale(Vector3Add(d, Vector3Scale(rt, half)), 1000)));
        if (!f1 || !f2) continue;
        Color col = kHud;
        if (a < 0) {
            // dashed negative bars
            for (int k = 0; k < 3; ++k) {
                float t0 = k / 3.0f, t1 = t0 + 0.2f;
                ui::line(Vector2Lerp(L0, L1, t0), Vector2Lerp(L0, L1, t1), 1.6f * s, col, false);
                ui::line(Vector2Lerp(R0, R1, 1 - t1), Vector2Lerp(R0, R1, 1 - t0), 1.6f * s, col, false);
            }
        } else {
            ui::line(L0, L1, 1.6f * s, col, false);
            ui::line(R0, R1, 1.6f * s, col, false);
        }
        if (a != 0) {
            Vector2 dir = Vector2Normalize(Vector2Subtract(R1, R0));
            Vector2 n = {-dir.y, dir.x};
            float tick = (a > 0 ? 7.0f : -7.0f) * s;
            ui::line(L0, {L0.x + n.x * tick, L0.y + n.y * tick}, 1.6f * s, col, false);
            ui::line(R1, {R1.x + n.x * tick, R1.y + n.y * tick}, 1.6f * s, col, false);
            std::string lbl = std::to_string(std::abs(a));
            ui::text(lbl, L0.x - 8 * s, L0.y - 7 * s, 13 * s, col, Align::Right);
            ui::text(lbl, R1.x + 8 * s, R1.y - 7 * s, 13 * s, col, Align::Left);
        }
    }
}

void Hud::flightSymbology(const HudContext& c, bool cockpit) {
    const FlightState& st = c.player->fm->state();
    float s = c.h / 720.0f;
    Color col = kHud;
    Rectangle area = cockpit ? c.combiner : Rectangle{0, 0, static_cast<float>(c.w), static_cast<float>(c.h)};
    float cx = area.x + area.width * 0.5f;
    float cy = area.y + area.height * (cockpit ? 0.5f : 0.5f);
    float spread = cockpit ? area.width * 0.36f : 230 * s;
    float fs = cockpit ? 15 * s : 18 * s;

    // boresight / gun cross and flight path marker
    bool f1, f2;
    Vector2 bore = project(c, toRl(st.pos + st.q.forward() * 1000.0), &f1);
    if (f1) {
        float r = 9 * s;
        ui::line({bore.x - 2.2f * r, bore.y}, {bore.x - r, bore.y}, 2 * s, col);
        ui::line({bore.x + r, bore.y}, {bore.x + 2.2f * r, bore.y}, 2 * s, col);
        ui::line({bore.x, bore.y - 2.2f * r}, {bore.x, bore.y - r}, 2 * s, col);
        ui::line({bore.x, bore.y + r}, {bore.x, bore.y + 2.2f * r}, 2 * s, col);
        DrawCircleV(bore, 1.8f * s, col);
    }
    if (st.airspeed > 15) {
        Vector2 fpm = project(c, toRl(st.pos + normalize(st.vel) * 1000.0), &f2);
        if (f2) {
            float r = 7 * s;
            DrawRing(fpm, r - 1.2f * s, r + 0.8f * s, 0, 360, 24, col);
            ui::line({fpm.x - r, fpm.y}, {fpm.x - 2.6f * r, fpm.y}, 2 * s, col, false);
            ui::line({fpm.x + r, fpm.y}, {fpm.x + 2.6f * r, fpm.y}, 2 * s, col, false);
            ui::line({fpm.x, fpm.y - r}, {fpm.x, fpm.y - 2.0f * r}, 2 * s, col, false);
        }
    }

    // speed (left) and altitude (right) boxes with tapes
    double kmh = st.airspeed * 3.6, kt = st.airspeed * 1.943844;
    double alt = st.pos.y;
    float bw = 86 * s, bh = 30 * s;
    Rectangle sb = {cx - spread - bw, cy - bh / 2, bw, bh};
    Rectangle ab = {cx + spread, cy - bh / 2, bw, bh};
    // tapes
    auto tape = [&](float x, double value, double step, int labelEvery, bool leftSide) {
        float pxPer = 46 * s / static_cast<float>(step);
        double base = std::floor(value / step) * step;
        for (int k = -5; k <= 5; ++k) {
            double v = base + k * step;
            if (v < 0 && step > 1) continue;
            float y = cy - static_cast<float>((v - value) * pxPer);
            if (std::fabs(y - cy) < bh * 0.7f || std::fabs(y - cy) > 140 * s) continue;
            float len = (static_cast<int>(std::round(v / step)) % labelEvery == 0) ? 12 * s : 6 * s;
            float x0 = leftSide ? x - len : x, x1 = leftSide ? x : x + len;
            ui::line({x0, y}, {x1, y}, 1.5f * s, kHudDim, false);
            if (len > 8 * s) ui::text(fmti("%d", static_cast<int>(std::round(v))), leftSide ? x - 16 * s : x + 16 * s, y - 7 * s, 13 * s, kHudDim, leftSide ? Align::Right : Align::Left);
        }
    };
    tape(sb.x + bw + 4 * s, kmh, 50, 2, true);
    tape(ab.x - 4 * s, alt, 100, 5, false);
    drawRoller(sb, kmh, 4, 1, 22 * s, col, Color{0, 0, 0, 80});
    drawRoller(ab, alt, 5, 10, 22 * s, col, Color{0, 0, 0, 80});
    ui::text("KM/H", sb.x, sb.y - 18 * s, 13 * s, kHudDim);
    ui::text("ALT M", ab.x, ab.y - 18 * s, 13 * s, kHudDim);
    ui::text(fmti("%d KT", static_cast<int>(kt)), sb.x, sb.y + bh + 6 * s, fs * 0.8f, col);
    ui::text(fmt("M %.2f", st.mach), sb.x, sb.y + bh + 24 * s, fs * 0.8f, col);
    ui::text(fmt("G %.1f", st.gLoad), sb.x, sb.y + bh + 42 * s, fs * 0.8f, st.gLoad > 8.0 ? kWarn : col);
    ui::text(fmt("AOA %.0f", st.alpha / kDeg), sb.x, sb.y + bh + 60 * s, fs * 0.8f, st.stalled ? kWarn : col);
    double vs = st.vel.y;
    ui::text(fmti("VS %+d", static_cast<int>(vs * 60.0)), ab.x, ab.y + bh + 6 * s, fs * 0.8f, col);
    if (st.agl < 1500) ui::text(fmti("R %d", static_cast<int>(st.agl)), ab.x, ab.y + bh + 24 * s, fs * 0.8f, st.agl < 150 && !st.onGround ? kWarn : col);
    // throttle
    {
        float tx = ab.x, ty = ab.y + bh + (st.agl < 1500 ? 46 : 28) * s;
        float tw = bw, th = 7 * s;
        DrawRectangleLinesEx({tx, ty, tw, th}, 1, kHudDim);
        float fill = static_cast<float>(std::min(st.throttle / FlightModel::kAbDetent, 1.0));
        DrawRectangle(static_cast<int>(tx), static_cast<int>(ty), static_cast<int>(tw * fill * 0.9f), static_cast<int>(th), kHud);
        if (st.throttle > FlightModel::kAbDetent)
            DrawRectangle(static_cast<int>(tx + tw * 0.9f), static_cast<int>(ty), static_cast<int>(tw * 0.1f * (st.throttle - 0.9) / 0.1), static_cast<int>(th), kWarn);
        ui::text(st.abLevel > 0.1 ? "AB" : fmti("THR %d%%", static_cast<int>(std::min(st.throttle / 0.9, 1.0) * 100)), tx, ty + th + 3 * s, 13 * s, st.abLevel > 0.1 ? kWarn : col);
    }
    // heading tape
    {
        double hdg = compassHeadingDeg(st.q);
        float top = cockpit ? area.y + 14 * s : 22 * s;
        float wTape = cockpit ? area.width * 0.7f : 360 * s;
        float pxPerDeg = wTape / 60.0f;
        for (int d = -35; d <= 35; ++d) {
            double v = std::floor(hdg) + d;
            int iv = (static_cast<int>(v) % 360 + 360) % 360;
            float x = cx + static_cast<float>((v - hdg) * pxPerDeg);
            if (std::fabs(x - cx) > wTape / 2) continue;
            if (iv % 5 != 0) continue;
            bool major = iv % 10 == 0;
            ui::line({x, top + 16 * s}, {x, top + (major ? 26 : 22) * s}, 1.5f * s, kHudDim, false);
            if (major) {
                std::string lbl = iv == 0 ? "N" : iv == 90 ? "E" : iv == 180 ? "S" : iv == 270 ? "W" : fmti("%02d", iv / 10);
                ui::text(lbl, x, top, 14 * s, kHudDim, Align::Center);
            }
        }
        Rectangle hb = {cx - 26 * s, top + 28 * s, 52 * s, 20 * s};
        ui::panel(hb, Color{0, 0, 0, 80}, col);
        ui::text(fmti("%03d", static_cast<int>(std::round(hdg)) % 360), cx, hb.y + 2 * s, 16 * s, col, Align::Center);
    }
    // configuration flags
    {
        float y = cy + (cockpit ? area.height * 0.32f : 150 * s);
        std::string cfg;
        if (st.gearPos > 0.01) ui::text(st.gearPos >= 1.0 ? "GEAR DN" : "GEAR TRANSIT", cx - 60 * s, y, 14 * s, st.gearPos >= 1.0 ? Color{120, 255, 120, 230} : kWarn, Align::Center);
        if (st.parkingBrake) ui::text("PARK BRK", cx + 60 * s, y, 14 * s, kWarn, Align::Center);
        else if (st.wheelBrake && st.onGround) ui::text("BRAKES", cx + 60 * s, y, 14 * s, kWarn, Align::Center);
        else if (st.airbrake && !st.onGround) ui::text("SPD BRK", cx + 60 * s, y, 14 * s, kWarn, Align::Center);
        const char* eng = st.engine == EngineState::Off ? "ENG OFF" : st.engine == EngineState::Starting ? "ENG START" :
                          st.engine == EngineState::Flameout ? "FLAMEOUT" : nullptr;
        if (eng) ui::text(eng, cx, y + 18 * s, 14 * s, kWarn, Align::Center);
    }
}

void Hud::targets(const HudContext& c) {
    if (!c.battle) return;
    const Jet& p = *c.player;
    const FlightState& ps = p.fm->state();
    float s = c.h / 720.0f;
    Vector2 ctr = {c.w * 0.5f, c.h * 0.5f};
    for (const auto& jp : c.battle->jets()) {
        const Jet& j = *jp;
        if (j.isPlayer || !j.alive) continue;
        const FlightState& js = j.fm->state();
        Vector3 wp = toRl(js.pos);
        double dist = length(js.pos - ps.pos);
        bool front;
        Vector2 sp = project(c, wp, &front);
        bool enemy = j.team != p.team;
        Color col = enemy ? kEnemy : kAlly;
        bool onScreen = front && sp.x > 0 && sp.x < c.w && sp.y > 0 && sp.y < c.h;
        if (onScreen) {
            float r = std::clamp(static_cast<float>(2200.0 / std::max(dist, 1.0)) * 6.0f, 9.0f, 34.0f) * s;
            if (enemy) {
                // diamond marker
                Vector2 pts[4] = {{sp.x, sp.y - r}, {sp.x + r, sp.y}, {sp.x, sp.y + r}, {sp.x - r, sp.y}};
                for (int k = 0; k < 4; ++k) ui::line(pts[k], pts[(k + 1) % 4], 1.8f * s, col);
                // BF-style spot triangle above
                Vector2 t0 = {sp.x, sp.y - r - 6 * s}, t1 = {sp.x - 6 * s, sp.y - r - 16 * s}, t2 = {sp.x + 6 * s, sp.y - r - 16 * s};
                DrawTriangle(t0, t2, t1, col);
                ui::textShadow(dist > 1000 ? fmt("%.1f km", dist / 1000.0) : fmti("%d m", static_cast<int>(dist)), sp.x + r + 6 * s, sp.y - 8 * s, 14 * s, col);
                if (dist < 4000) ui::textShadow(j.callsign, sp.x + r + 6 * s, sp.y + 6 * s, 12 * s, ui::withAlpha(col, 0.8f));
            } else {
                Vector2 t0 = {sp.x, sp.y - 6 * s}, t1 = {sp.x - 7 * s, sp.y - 18 * s}, t2 = {sp.x + 7 * s, sp.y - 18 * s};
                DrawTriangle(t0, t2, t1, col);
                ui::textShadow(j.callsign, sp.x, sp.y - 36 * s, 12 * s, col, Align::Center);
            }
        } else if (enemy && dist < 12000) {
            // off-screen arrow at the edge
            Vector3 camFwd = Vector3Normalize(Vector3Subtract(c.cam.target, c.cam.position));
            Matrix view = GetCameraMatrix(c.cam);
            Vector3 right = {view.m0, view.m4, view.m8}, up = {view.m1, view.m5, view.m9};
            Vector3 d = Vector3Subtract(wp, c.cam.position);
            Vector2 dir = Vector2Normalize({Vector3DotProduct(d, right), -Vector3DotProduct(d, up)});
            (void)camFwd;
            float rx = c.w * 0.42f, ry = c.h * 0.40f;
            Vector2 e = {ctr.x + dir.x * rx, ctr.y + dir.y * ry};
            Vector2 n = {-dir.y, dir.x};
            Vector2 a = {e.x + dir.x * 14 * s, e.y + dir.y * 14 * s};
            Vector2 b = {e.x + n.x * 8 * s, e.y + n.y * 8 * s};
            Vector2 cc = {e.x - n.x * 8 * s, e.y - n.y * 8 * s};
            DrawTriangle(a, cc, b, col);
            DrawTriangle(a, b, cc, col);
            ui::textShadow(fmt("%.1f", dist / 1000.0), e.x - dir.x * 22 * s, e.y - dir.y * 22 * s - 7 * s, 13 * s, col, Align::Center);
        }
    }
    // ground targets
    std::vector<Rectangle> labelRects;   // declutter: one label per screen area
    auto labelFree = [&](Rectangle r) {
        for (const Rectangle& o : labelRects) if (CheckCollisionRecs(r, o)) return false;
        labelRects.push_back(r);
        return true;
    };
    for (size_t gi = 0; gi < c.battle->ground().size(); ++gi) {
        const GroundTarget& g = c.battle->ground()[gi];
        if (!g.alive) continue;
        double dist = length(g.pos - ps.pos);
        bool objective = g.group > 0 && g.team == Team::Red;
        bool friendly = g.team == Team::Blue;
        // mission targets are marked from far away so they can be found; others only up close
        if (dist > (objective || friendly ? 26000.0 : 3500.0)) continue;
        double off = std::acos(std::clamp(dot(normalize(g.pos - ps.pos), ps.q.forward()), -1.0, 1.0));
        bool important = objective || friendly || groundShootsAir(g.kind);
        if (off > (important ? 40.0 : 14.0) * kDeg && p.lockTarget != -1000 - static_cast<int>(gi)) continue;
        bool front;
        Vector2 sp = project(c, toRl(g.pos + Vec3{0, groundAimHeight(g.kind) + 1.5, 0}), &front);
        if (!front || sp.x < 0 || sp.x > c.w || sp.y < 0 || sp.y > c.h) continue;
        Color col = friendly ? kFriendlyAsset : objective ? kObjective : kEnemy;
        float r = (objective ? 7.0f : 6.0f) * s;
        DrawRectangleLinesEx({sp.x - r, sp.y - r, 2 * r, 2 * r}, (objective ? 2.0f : 1.5f) * s, ui::withAlpha(col, 0.9f));
        const std::string name = g.label.empty() ? groundKindName(g.kind) : g.label;
        if (objective || friendly || dist < 2500) {
            float lw = ui::measure(name, 11 * s);
            if (labelFree({sp.x - lw / 2 - 2 * s, sp.y + r + 1 * s, lw + 4 * s, 14 * s}))
                ui::textShadow(name, sp.x, sp.y + r + 2 * s, 11 * s, ui::withAlpha(col, 0.9f), Align::Center);
            if (dist > 3000 && labelFree({sp.x - 14 * s, sp.y - r - 16 * s, 28 * s, 14 * s}))
                ui::textShadow(fmt("%.1f", dist / 1000.0), sp.x, sp.y - r - 15 * s, 11 * s, ui::withAlpha(col, 0.8f), Align::Center);
        }
    }
    // lock box
    if (p.lockTarget != -1) {
        Vec3 tp, tv;
        bool ok = false;
        if (p.lockTarget >= 0) {
            const Jet* t = c.battle->jetById(p.lockTarget);
            if (t && t->alive) { tp = t->fm->state().pos; tv = t->fm->state().vel; ok = true; }
        } else if (p.lockTarget <= -1000) {
            int gi = -1000 - p.lockTarget;
            if (gi < static_cast<int>(c.battle->ground().size())) { tp = c.battle->ground()[gi].pos + Vec3{0, 2, 0}; ok = true; }
        }
        bool front;
        Vector2 sp = ok ? project(c, toRl(tp), &front) : Vector2{};
        if (ok && front) {
            float prog = static_cast<float>(p.lockProgress);
            bool locked = prog >= 1.0f;
            float r = (locked ? 26.0f : 26.0f + (1.0f - prog) * 60.0f) * s;
            float rot = locked ? 45.0f : static_cast<float>(time_ * 180.0);
            Color lc = locked ? kEnemy : kWarn;
            for (int k = 0; k < 4; ++k) {
                float a0 = (rot + k * 90) * DEG2RAD;
                Vector2 corner = {sp.x + cosf(a0) * r, sp.y + sinf(a0) * r};
                Vector2 l1 = {corner.x + cosf(a0 + 2.36f) * 12 * s, corner.y + sinf(a0 + 2.36f) * 12 * s};
                Vector2 l2 = {corner.x + cosf(a0 - 2.36f) * 12 * s, corner.y + sinf(a0 - 2.36f) * 12 * s};
                ui::line(corner, l1, 2.5f * s, lc);
                ui::line(corner, l2, 2.5f * s, lc);
            }
            ui::textShadow(locked ? "LOCKED" : "LOCKING", sp.x, sp.y + r + 10 * s, 15 * s, lc, Align::Center);
            // gun lead pipper
            double dist = length(tp - ps.pos);
            if (p.lockTarget >= 0 && dist < 1800) {
                double tof = dist / 1050.0;
                // where the nose must point: relative target motion plus gravity drop compensation
                Vec3 lead = tp + (tv - ps.vel) * tof + Vec3{0, 0.5 * kG * tof * tof, 0};
                bool lf;
                Vector2 lp = project(c, toRl(lead), &lf);
                if (lf) {
                    DrawRing(lp, 9 * s, 11 * s, 0, 360, 24, kHud);
                    DrawCircleV(lp, 2 * s, kHud);
                    ui::text("GUN", lp.x + 14 * s, lp.y - 7 * s, 12 * s, kHud);
                }
            }
        }
    }
    // incoming missiles markers
    for (const Missile& m : c.battle->missiles()) {
        if (m.targetJet != p.id || m.decoyFlare >= 0) continue;
        bool front;
        Vector2 sp = project(c, toRl(m.pos), &front);
        if (!front) continue;
        DrawRing(sp, 7 * s, 9 * s, 0, 360, 16, kEnemy);
        ui::textShadow("M", sp.x, sp.y - 24 * s, 13 * s, kEnemy, Align::Center);
    }
}

void Hud::weapons(const HudContext& c) {
    // compact status block, bottom right: hull, cannon, missiles, flares
    const Jet& p = *c.player;
    float s = c.h / 720.0f, k = panelK(c), ps = s * k;
    float f = pfont(13, s, k), fs = pfont(11, s, k);
    float row = std::max(20 * ps, f + 4 * s);
    float w = std::max(230 * ps, 128 * s), h = 10 * ps + row * 5 + 6 * s;
    Rectangle r = {c.w - w - 18 * s, c.h - h - 18 * s, w, h};
    ui::panel(r, kPanel, kPanelBorder);
    float x = r.x + 8 * s, y = r.y + 5 * s;
    float labelW = ui::measure("HULL", fs) + 6 * s;
    // hull
    float hp = static_cast<float>(p.health / 100.0);
    Color hc = hp > 0.6f ? Color{120, 240, 160, 230} : hp > 0.3f ? kWarn : kEnemy;
    ui::text("HULL", x, y + 1 * s, fs, Color{200, 210, 210, 230});
    Rectangle bar = {x + labelW, y + row * 0.25f, w - labelW - 40 * s, std::max(5 * s, row * 0.35f)};
    DrawRectangleRec(bar, Color{255, 255, 255, 30});
    DrawRectangleRec({bar.x, bar.y, bar.width * hp, bar.height}, hc);
    ui::text(fmti("%d", static_cast<int>(p.health)), r.x + w - 8 * s, y, f, hc, Align::Right);
    y += row;
    ui::text("GUN", x, y + 1 * s, fs, Color{200, 210, 210, 230});
    ui::text(fmti("%d", p.rounds), r.x + w - 8 * s, y, f, p.gunFiring ? kWarn : WHITE, Align::Right);
    y += row;
    auto missileRow = [&](const char* name, int have, int max, bool selected) {
        if (selected) DrawRectangle(static_cast<int>(r.x + 2 * s), static_cast<int>(y - 1 * s), static_cast<int>(w - 4 * s), static_cast<int>(row - 1 * s), Color{150, 255, 205, 40});
        ui::text(name, x, y + 1 * s, fs, selected ? Color{150, 255, 205, 255} : Color{200, 210, 210, 200});
        float pw = std::max(6 * s, 12 * ps), gap = std::max(2 * s, 5 * ps);
        for (int i = 0; i < max; ++i) {
            Rectangle pip = {x + labelW + i * (pw + gap), y + row * 0.22f, pw, std::max(5 * s, row * 0.42f)};
            if (i < have) DrawRectangleRec(pip, selected ? WHITE : Color{255, 255, 255, 150});
            else DrawRectangleLinesEx(pip, 1, Color{255, 255, 255, 90});
        }
        y += row;
    };
    missileRow("ATA", p.missiles, p.fm->profile().missiles, !p.weaponAG);
    missileRow("ATS", p.agms, 4, p.weaponAG);
    ui::text("FLR", x, y + 1 * s, fs, Color{200, 210, 210, 230});
    float fw = std::max(12 * s, 24 * ps), fg = std::max(3 * s, 6 * ps);
    for (int i = 0; i < 3; ++i) {
        Rectangle pip = {x + labelW + i * (fw + fg), y + row * 0.22f, fw, std::max(5 * s, row * 0.42f)};
        if (i < p.flareCharges) DrawRectangleRec(pip, Color{255, 200, 90, 240});
        else {
            DrawRectangleLinesEx(pip, 1, Color{255, 200, 90, 90});
            if (i == p.flareCharges) {
                float prog = static_cast<float>(1.0 - p.flareCooldown / 9.0);
                DrawRectangleRec({pip.x, pip.y, pip.width * std::clamp(prog, 0.0f, 1.0f), pip.height}, Color{255, 200, 90, 90});
            }
        }
    }
    lastWeaponsRect_ = r;
}

void Hud::minimap(const HudContext& c) {
    if (c.minimap.id == 0) return;
    const FlightState& ps = c.player->fm->state();
    float s = c.h / 720.0f;
    float size = 190 * s;
    Rectangle r = {18 * s, c.h - size - 18 * s, size, size};
    double range = 7000.0;            // metres from centre to edge
    double mPerPx = 2.0 * c.minimapHalf / c.minimap.width;
    double hdg = compassHeadingDeg(ps.q);
    float cx = r.x + size / 2, cy = r.y + size / 2;
    float pxPerM = static_cast<float>(size / 2 / range);
    BeginScissorMode(static_cast<int>(r.x), static_cast<int>(r.y), static_cast<int>(r.width), static_cast<int>(r.height));
    DrawRectangleRec(r, Color{10, 16, 20, 255});
    float srcHalf = static_cast<float>(range * 1.45 / mPerPx);
    float u = static_cast<float>((ps.pos.x + c.minimapHalf) / mPerPx), v = static_cast<float>((ps.pos.z + c.minimapHalf) / mPerPx);
    float dstHalf = static_cast<float>(range * 1.45) * pxPerM;
    DrawTexturePro(c.minimap, {u - srcHalf, v - srcHalf, srcHalf * 2, srcHalf * 2}, {cx, cy, dstHalf * 2, dstHalf * 2}, {dstHalf, dstHalf},
                   static_cast<float>(-hdg), Color{255, 255, 255, 215});
    // range rings
    DrawRingLines({cx, cy}, size * 0.25f, size * 0.25f, 0, 360, 48, Color{255, 255, 255, 40});
    DrawRingLines({cx, cy}, size * 0.5f, size * 0.5f, 0, 360, 64, Color{255, 255, 255, 40});
    double psi = hdg * DEG2RAD;
    Vector2 fw = {static_cast<float>(std::sin(psi)), static_cast<float>(-std::cos(psi))};
    Vector2 rt = {static_cast<float>(std::cos(psi)), static_cast<float>(std::sin(psi))};
    auto toMap = [&](const Vec3& p) {
        float dx = static_cast<float>(p.x - ps.pos.x), dz = static_cast<float>(p.z - ps.pos.z);
        return Vector2{cx + (dx * rt.x + dz * rt.y) * pxPerM, cy - (dx * fw.x + dz * fw.y) * pxPerM};
    };
    auto tri = [&](Vector2 p, double headingDeg, float sz, Color col) {
        double rel = (headingDeg - hdg) * DEG2RAD;
        Vector2 f = {static_cast<float>(std::sin(rel)), static_cast<float>(-std::cos(rel))};
        Vector2 n = {-f.y, f.x};
        Vector2 a = {p.x + f.x * sz, p.y + f.y * sz};
        Vector2 b = {p.x - f.x * sz * 0.7f + n.x * sz * 0.65f, p.y - f.y * sz * 0.7f + n.y * sz * 0.65f};
        Vector2 d = {p.x - f.x * sz * 0.7f - n.x * sz * 0.65f, p.y - f.y * sz * 0.7f - n.y * sz * 0.65f};
        DrawTriangle(a, d, b, col);
        DrawTriangle(a, b, d, col);
    };
    if (c.deck.active) {
        // the carrier: a ship outline along its heading
        Vec3 f{std::cos(c.deck.heading), 0, std::sin(c.deck.heading)};
        Vector2 bow = toMap(c.deck.center + f * 170.0), stern = toMap(c.deck.center - f * 170.0);
        DrawLineEx(stern, bow, std::max(3.0f, 330.0f * pxPerM * 0.13f) * 1.0f + 2.0f * s, Color{150, 200, 255, 230});
        Vector2 m = toMap(c.deck.center);
        ui::text("CV", m.x + 6 * s, m.y - 6 * s, 11 * s, Color{150, 200, 255, 230}, Align::Left, false);
    }
    if (c.battle) {
        for (const GroundTarget& g : c.battle->ground()) {
            if (!g.alive) continue;
            Vector2 m = toMap(g.pos);
            bool objective = g.group > 0 && g.team == Team::Red;
            Color col = g.team == Team::Blue ? kFriendlyAsset : objective ? kObjective : Color{255, 90, 70, 200};
            float hs = (objective || g.team == Team::Blue ? 3.0f : 2.0f) * s;
            DrawRectangleV({m.x - hs, m.y - hs}, {2 * hs, 2 * hs}, col);
        }
        if (c.campaign && c.mission && c.mission->status() == MissionStatus::Running && c.mission->stepIndex() < c.campaign->objectives.size()) {
            Vec3 op;
            if (objectivePoint(*c.campaign, c.campaign->objectives[c.mission->stepIndex()], c.battle, op)) {
                Vector2 m = toMap(op);
                // clamp to the minimap edge so the direction is always shown
                Vector2 d = {m.x - cx, m.y - cy};
                float len = std::sqrt(d.x * d.x + d.y * d.y), maxR = size * 0.47f;
                if (len > maxR) { m = {cx + d.x / len * maxR, cy + d.y / len * maxR}; }
                float k = 5 * s;
                DrawTriangle({m.x, m.y - k}, {m.x - k, m.y}, {m.x + k, m.y}, kObjective);
                DrawTriangle({m.x - k, m.y}, {m.x, m.y + k}, {m.x + k, m.y}, kObjective);
            }
        }
        for (const auto& jp : c.battle->jets()) {
            if (jp->isPlayer || !jp->alive) continue;
            tri(toMap(jp->fm->state().pos), compassHeadingDeg(jp->fm->state().q), 6 * s, jp->team == Team::Red ? kEnemy : kAlly);
        }
        for (const Missile& m : c.battle->missiles()) DrawCircleV(toMap(m.pos), 2 * s, m.team == Team::Red ? kEnemy : WHITE);
    }
    tri({cx, cy}, hdg, 8 * s, WHITE);
    EndScissorMode();
    DrawRectangleLinesEx(r, 1, kPanelBorder);
    // north marker
    Vector2 north = {cx + (0 * rt.x + -1 * rt.y) * size * 0.44f, cy - (0 * fw.x + -1 * fw.y) * size * 0.44f};
    ui::textShadow("N", north.x, north.y - 7 * s, 14 * s, WHITE, Align::Center);
    ui::text(fmt("%.0f KM", range / 1000.0), r.x + 4 * s, r.y + 4 * s, 11 * s, Color{255, 255, 255, 140});
}

void Hud::minimalSymbology(const HudContext& c) {
    // Battlefield-like: small aiming reticle, flight path dot, compact speed/altitude readouts
    const FlightState& st = c.player->fm->state();
    float s = c.h / 720.0f;
    bool f1, f2;
    Vector2 bore = project(c, toRl(st.pos + st.q.forward() * 1000.0), &f1);
    if (f1) {
        DrawRing(bore, 9 * s, 10.6f * s, 0, 360, 32, Color{255, 255, 255, 210});
        DrawCircleV(bore, 1.6f * s, Color{255, 255, 255, 230});
    }
    {
        // rolling airspeed (km/h) and altitude (m) drums with moving tapes, either side of centre
        Color white = {255, 255, 255, 225};
        Color bg = {0, 0, 0, 110};
        float cx = c.w * 0.5f, cy = c.h * 0.5f;
        const float kd = std::max(panelK(c), 0.62f);   // compact readouts (panelK)
        const float fz = std::max(20 * s * kd, 12.0f * s);
        float off = 230.0f * s, bw = std::max(78.0f * s * kd, ui::measure("0000", fz) + 16 * s), bh = std::max(30.0f * s * kd, fz + 6 * s);
        double kmh = st.airspeed * 3.6;
        Rectangle sb = {cx - off - bw, cy - bh * 0.5f, bw, bh};
        Rectangle ab = {cx + off, cy - bh * 0.5f, bw + 8 * s, bh};
        drawTape(sb.x + bw + 6 * s, cy, 95 * s * kd, kmh, 20.0, 5, false, s * kd, white);
        drawTape(ab.x - 6 * s, cy, 95 * s * kd, st.pos.y, 50.0, 4, true, s * kd, white);
        // the speed drum steps in tens above 1000 km/h so it stays readable while accelerating
        drawRoller(sb, kmh, 4, kmh >= 1000.0 ? 10 : 1, fz, white, bg);
        drawRoller(ab, st.pos.y, 5, 10, fz, white, bg);
        ui::textShadow("KM/H", sb.x, sb.y - 14 * s, 10.5f * s, ui::withAlpha(white, 0.8f));
        ui::textShadow("ALT M", ab.x, ab.y - 14 * s, 10.5f * s, ui::withAlpha(white, 0.8f));
        // vertical speed arrow next to the altimeter
        double vs = st.vel.y;
        float vlen = static_cast<float>(std::clamp(vs / 120.0, -1.0, 1.0)) * 40 * s;
        if (std::fabs(vlen) > 2 * s) {
            float vx = ab.x + ab.width + 8 * s;
            ui::line({vx, cy}, {vx, cy - vlen}, 2.0f * s, white);
            ui::textShadow(fmt("%+.0f", vs), vx + 5 * s, cy - vlen - 6 * s, 11 * s, white);
        }
        ui::textShadow(fmt("G %.1f", st.gLoad), sb.x, sb.y + bh + 4 * s, 10.5f * s, st.gLoad > 8.0 ? kWarn : white);
        if (st.agl < 600 && !st.onGround) ui::textShadow(fmt("R %.0f", st.agl), ab.x, ab.y + bh + 4 * s, 10.5f * s, st.agl < 150 ? kWarn : white);
    }
    if (st.airspeed > 20) {
        Vector2 fpm = project(c, toRl(st.pos + normalize(st.vel) * 1000.0), &f2);
        if (f2) DrawCircleLinesV(fpm, 4 * s, Color{150, 255, 205, 200});
    }
    // compact heading at the top
    double hdg = compassHeadingDeg(st.q);
    ui::textShadow(fmt("%03.0f", hdg), c.w * 0.5f, 14 * s, 16 * s, Color{255, 255, 255, 210}, Align::Center);
    // throttle / afterburner under the minimap-side
    float tx = c.w * 0.5f - 60 * s, ty = c.h - 58 * s;
    DrawRectangle(static_cast<int>(tx), static_cast<int>(ty), static_cast<int>(120 * s), static_cast<int>(5 * s), Color{255, 255, 255, 50});
    float fill = static_cast<float>(std::min(st.throttle / FlightModel::kAbDetent, 1.0));
    DrawRectangle(static_cast<int>(tx), static_cast<int>(ty), static_cast<int>(108 * s * fill), static_cast<int>(5 * s), Color{255, 255, 255, 200});
    if (st.abLevel > 0.1) DrawRectangle(static_cast<int>(tx + 108 * s), static_cast<int>(ty), static_cast<int>(12 * s), static_cast<int>(5 * s), kWarn);
    if (st.stalled) ui::textShadow("STALL", c.w * 0.5f, c.h * 0.62f, 18 * s, kWarn, Align::Center);
}

void Hud::hintsBar(const HudContext& c) {
    float s = c.h / 720.0f * std::max(panelK(c), 0.75f);   // compact panel (see panelK)
    const char* items[][2] = {{"W", "AFTERBURNER"}, {"F", c.player->weaponAG ? "ATS MISSILE" : "ATA MISSILE"}, {"Q", "SWAP MSL"},
                              {"X", "FLARES"}, {"<<", "ROLL"}, {"C", "CAMERA"}, {"ESC", "MENU"}};
    float x = c.w * 0.5f - 300 * s, y = c.h - 30 * s;
    for (auto& it : items) {
        float kw = ui::measure(it[0], 11 * s) + 10 * s;
        DrawRectangleRounded({x, y, kw, 16 * s}, 0.3f, 4, Color{255, 255, 255, 200});
        ui::text(it[0], x + kw / 2, y + 2 * s, 11 * s, BLACK, Align::Center);
        x += kw + 5 * s;
        ui::textShadow(it[1], x, y + 1 * s, 12 * s, Color{255, 255, 255, 190});
        x += ui::measure(it[1], 12 * s) + 16 * s;
    }
}

void Hud::pipPanel(const HudContext& c) {
    const PipState& p = *c.pip;
    float s = c.h / 720.0f;
    float w = 300 * s, h = w * c.pipH / c.pipW;
    // above the (compact) status block
    float statusTop = lastWeaponsRect_.height > 0 ? lastWeaponsRect_.y : c.h - 18 * s - 138 * s;
    Rectangle r = {c.w - w - 18 * s, statusTop - 14 * s - h, w, h};
    bool hitCam = p.mode == PipMode::Hit;
    Color frame = hitCam ? (p.result > 0 ? Color{255, 90, 70, 255} : Color{255, 200, 90, 255}) : kHud;
    // video feed (render textures are stored bottom-up)
    DrawTexturePro(c.pipTex, {0, 0, static_cast<float>(c.pipW), static_cast<float>(-c.pipH)}, r, {0, 0}, 0, WHITE);
    // sensor-video look: scanlines + slight green cast for the target camera
    if (p.mode == PipMode::Target) DrawRectangleRec(r, Color{40, 80, 50, 40});
    for (float y = r.y; y < r.y + r.height; y += 3 * s) DrawLineEx({r.x, y}, {r.x + r.width, y}, 1, Color{0, 0, 0, 28});
    if (p.flash > 0) DrawRectangleRec(r, Color{255, 255, 255, static_cast<unsigned char>(std::min(1.0, p.flash) * 200)});
    DrawRectangleLinesEx(r, 2.0f * s, frame);
    // corner brackets
    float k = 14 * s;
    for (int i = 0; i < 4; ++i) {
        float x = (i & 1) ? r.x + r.width - 4 * s : r.x + 4 * s, y = (i & 2) ? r.y + r.height - 4 * s : r.y + 4 * s;
        float dx = (i & 1) ? -k : k, dy = (i & 2) ? -k : k;
        DrawLineEx({x, y}, {x + dx, y}, 2 * s, frame);
        DrawLineEx({x, y}, {x, y + dy}, 2 * s, frame);
    }
    const char* label = p.mode == PipMode::Target ? (p.targetGround >= 0 ? "TGT POD" : "TGT CAM")
                      : p.mode == PipMode::Missile ? (p.ats ? "ATS CAM" : "ATA CAM") : "HIT CAM";
    ui::textShadow(label, r.x + 10 * s, r.y + 8 * s, 14 * s, frame);
    if (std::fmod(time_, 1.0) < 0.6) {
        DrawCircleV({r.x + r.width - 50 * s, r.y + 15 * s}, 4 * s, Color{255, 50, 40, 255});
        ui::textShadow("REC", r.x + r.width - 42 * s, r.y + 8 * s, 12 * s, Color{255, 255, 255, 230});
    }
    // projected target track box inside the feed
    auto toFeed = [&](const Vec3& wp, bool& ok) {
        Vector3 d = Vector3Subtract(toRl(wp), p.cam.position);
        Vector3 f = Vector3Normalize(Vector3Subtract(p.cam.target, p.cam.position));
        ok = Vector3DotProduct(d, f) > 1.0f;
        Vector2 q = GetWorldToScreenEx(toRl(wp), p.cam, c.pipW, c.pipH);
        return Vector2{r.x + q.x / c.pipW * r.width, r.y + q.y / c.pipH * r.height};
    };
    if (p.mode != PipMode::Hit && p.haveTarget) {
        bool ok;
        Vector2 q = toFeed(p.lastTarget, ok);
        if (ok && CheckCollisionPointRec(q, r)) {
            float b = 16 * s;
            Color tc = p.mode == PipMode::Missile ? Color{255, 90, 70, 255} : kHud;
            for (int i = 0; i < 4; ++i) {
                float sx = (i & 1) ? 1.0f : -1.0f, sy = (i & 2) ? 1.0f : -1.0f;
                Vector2 cpt = {q.x + sx * b, q.y + sy * b};
                DrawLineEx(cpt, {cpt.x - sx * 6 * s, cpt.y}, 2 * s, tc);
                DrawLineEx(cpt, {cpt.x, cpt.y - sy * 6 * s}, 2 * s, tc);
            }
            DrawLineEx({q.x - 4 * s, q.y}, {q.x + 4 * s, q.y}, 1.5f * s, tc);
            DrawLineEx({q.x, q.y - 4 * s}, {q.x, q.y + 4 * s}, 1.5f * s, tc);
            ui::textShadow("TRK", q.x + b + 4 * s, q.y - b, 11 * s, tc);
        }
    }
    // bottom data line
    std::string info;
    if (p.mode == PipMode::Target) {
        info = fmt("RNG %.1f KM", p.lastDist / 1000.0) + (c.player->lockProgress >= 1.0 ? "   LOCKED - FIRE [F]" : fmt("   LOCKING %.0f%%", c.player->lockProgress * 100.0));
    } else if (p.mode == PipMode::Missile) {
        double closing = 680.0;
        info = p.haveTarget ? fmt("RNG %.0f M", p.lastDist) + fmt("   TTI %.1f S", p.lastDist / closing) : std::string("NO TRACK - BALLISTIC");
    } else {
        info = p.result == 2 ? "TARGET DESTROYED" : (p.result == 1 ? "HIT" : "MISSED");
        if (p.missDist > 0 && p.missDist < 400) info += fmt("   MISS DIST %.1f M", p.missDist);
    }
    DrawRectangle(static_cast<int>(r.x + 2 * s), static_cast<int>(r.y + r.height - 24 * s), static_cast<int>(r.width - 4 * s), static_cast<int>(22 * s), Color{0, 0, 0, 120});
    ui::text(info, r.x + 10 * s, r.y + r.height - 20 * s, 13 * s, hitCam && p.result > 0 ? Color{255, 120, 100, 255} : frame);
    if (hitCam && p.result > 0) {
        float a = static_cast<float>(std::min(1.0, p.timer));
        ui::textShadow(p.result == 2 ? "SPLASH" : "HIT", r.x + r.width * 0.5f, r.y + r.height * 0.38f, 26 * s, ui::withAlpha(Color{255, 80, 60, 255}, a), Align::Center);
    }
}

void Hud::killPanel(const HudContext& c) {
    // same size as the missile camera window; stacked above it when both are open
    float s = c.h / 720.0f;
    float w = 300 * s, h = w * c.pipH / c.pipW;
    float statusTop = lastWeaponsRect_.height > 0 ? lastWeaponsRect_.y : c.h - 18 * s - 138 * s;
    float y = statusTop - 14 * s - h;
    if (c.pipActive) y -= h + 12 * s;
    Rectangle r = {c.w - w - 18 * s, y, w, h};
    Color frame = c.killAfter ? Color{255, 90, 70, 255} : Color{255, 200, 90, 255};
    DrawTexturePro(c.killTex, {0, 0, static_cast<float>(c.pipW), static_cast<float>(-c.pipH)}, r, {0, 0}, 0, WHITE);
    for (float yy = r.y; yy < r.y + r.height; yy += 3 * s) DrawLineEx({r.x, yy}, {r.x + r.width, yy}, 1, Color{0, 0, 0, 22});
    // the hit: white flash, then a red tint while the wreck burns
    if (c.killImpact > 0.02) DrawRectangleRec(r, Color{255, 230, 200, static_cast<unsigned char>(std::min(1.0, c.killImpact) * 210)});
    if (c.killAfter) DrawRectangleRec(r, Color{140, 20, 10, 60});
    DrawRectangleLinesEx(r, 2.0f * s, frame);
    float k = 14 * s;
    for (int i = 0; i < 4; ++i) {
        float x = (i & 1) ? r.x + r.width - 4 * s : r.x + 4 * s, yy = (i & 2) ? r.y + r.height - 4 * s : r.y + 4 * s;
        float dx = (i & 1) ? -k : k, dy = (i & 2) ? -k : k;
        DrawLineEx({x, yy}, {x + dx, yy}, 2 * s, frame);
        DrawLineEx({x, yy}, {x, yy + dy}, 2 * s, frame);
    }
    std::string title = "KILL REPLAY  -  " + (c.killName.empty() ? std::string("BANDIT") : c.killName) + " VIEW";
    ui::textShadow(title, r.x + 10 * s, r.y + 8 * s, 13 * s, frame);
    if (std::fmod(time_, 1.0) < 0.6) {
        DrawCircleV({r.x + r.width - 54 * s, r.y + 15 * s}, 4 * s, Color{255, 50, 40, 255});
        ui::textShadow("REPLAY", r.x + r.width - 46 * s, r.y + 8 * s, 11 * s, Color{255, 255, 255, 230});
    }
    // progress and the skip key
    DrawRectangle(static_cast<int>(r.x + 2 * s), static_cast<int>(r.y + r.height - 24 * s), static_cast<int>(r.width - 4 * s), static_cast<int>(22 * s), Color{0, 0, 0, 120});
    DrawRectangle(static_cast<int>(r.x + 2 * s), static_cast<int>(r.y + r.height - 4 * s), static_cast<int>((r.width - 4 * s) * c.killProgress), static_cast<int>(2 * s), frame);
    std::string info = c.killAfter ? "DESTROYED" : (c.killWeapon.empty() ? std::string("INCOMING") : "INCOMING  " + c.killWeapon);
    ui::text(info, r.x + 10 * s, r.y + r.height - 20 * s, 13 * s, frame);
    ui::text("N  SKIP", r.x + r.width - 10 * s, r.y + r.height - 20 * s, 11 * s, Color{255, 255, 255, 170}, Align::Right);
    if (c.killAfter) {
        float a = static_cast<float>(std::clamp(1.0 - c.killImpact * 0.5, 0.0, 1.0));
        ui::textShadow("SPLASH", r.x + r.width * 0.5f, r.y + r.height * 0.38f, 26 * s, ui::withAlpha(Color{255, 80, 60, 255}, a), Align::Center);
    }
}

void Hud::replayOverlay(const HudContext& c) {
    float s = c.h / 720.0f;
    float bar = c.h * 0.085f;
    DrawRectangle(0, 0, c.w, static_cast<int>(bar), BLACK);
    DrawRectangle(0, static_cast<int>(c.h - bar), c.w, static_cast<int>(bar), BLACK);
    if (std::fmod(time_, 1.0) < 0.65) DrawCircleV({28 * s, bar * 0.5f}, 7 * s, Color{235, 40, 35, 255});
    ui::text("REPLAY", 42 * s, bar * 0.5f - 11 * s, 22 * s, WHITE);
    bool slow = c.replayT > c.deathTime - 1.4 && c.replayT < c.deathTime + 0.5;
    if (slow) ui::text("SLOW MOTION  x0.33", 160 * s, bar * 0.5f - 8 * s, 15 * s, Color{255, 200, 90, 255});
    ui::text("ENTER / SPACE  skip", c.w - 24 * s, bar * 0.5f - 8 * s, 14 * s, Color{255, 255, 255, 170}, Align::Right);
    double tRel = c.replayT - c.deathTime;
    ui::text(fmt("T %+.1f S", tRel), c.w * 0.5f, bar * 0.5f - 9 * s, 18 * s, tRel < 0 ? WHITE : Color{255, 90, 70, 255}, Align::Center);
    // progress
    float prog = static_cast<float>((c.replayT - c.replayFrom) / std::max(0.1, c.replayTo - c.replayFrom));
    DrawRectangle(0, static_cast<int>(bar - 3 * s), static_cast<int>(c.w * std::clamp(prog, 0.0f, 1.0f)), static_cast<int>(3 * s), Color{235, 40, 35, 255});
    // analysis panel
    if (!c.loss) return;
    const LossReport& L = *c.loss;
    float pw = 600 * s;
    auto wrap = [&](const std::string& text, float maxW) {
        std::vector<std::string> out;
        std::string line, word;
        auto flush = [&]() {
            if (word.empty()) return;
            std::string cand = line.empty() ? word : line + " " + word;
            if (ui::measure(cand, 14 * s, false) > maxW && !line.empty()) { out.push_back(line); line = word; }
            else line = cand;
            word.clear();
        };
        for (char ch : text) { if (ch == ' ') flush(); else word += ch; }
        flush();
        if (!line.empty()) out.push_back(line);
        return out;
    };
    std::vector<std::pair<std::string, bool>> rows;   // text, isAdvice
    for (const std::string& f : L.facts) for (auto& l : wrap(f, pw - 30 * s)) rows.push_back({l, false});
    for (const std::string& a : L.advice) {
        auto ls = wrap(a, pw - 44 * s);
        for (size_t i = 0; i < ls.size(); ++i) rows.push_back({(i == 0 ? "> " : "   ") + ls[i], true});
    }
    float ph = (64 + rows.size() * 21.0f) * s;
    Rectangle r = {28 * s, c.h - bar - ph - 18 * s, pw, ph};
    ui::panel(r, Color{0, 0, 0, 175}, Color{255, 90, 70, 120});
    ui::text("WHAT WENT WRONG", r.x + 14 * s, r.y + 10 * s, 13 * s, Color{255, 120, 100, 255});
    ui::text(L.title, r.x + 14 * s, r.y + 28 * s, 19 * s, WHITE);
    float y = r.y + 56 * s;
    for (const auto& row : rows) {
        ui::text(row.first, r.x + 14 * s, y, 14 * s, row.second ? Color{150, 255, 205, 255} : Color{200, 210, 215, 255}, Align::Left, false);
        y += 21 * s;
    }
}

void Hud::killFeed(const HudContext& c) {
    if (!c.battle) return;
    float s = c.h / 720.0f * std::max(panelK(c), 0.75f);   // compact panel (see panelK)
    float y = 18 * s;
    for (const KillFeedEntry& k : c.battle->killFeed()) {
        float a = static_cast<float>(std::clamp(9.0 - k.age, 0.0, 1.0));
        std::string mid = k.weapon.empty() ? "  >  " : "  [" + k.weapon + "]  ";
        std::string all = k.killer + mid + k.victim;
        float tw = ui::measure(all, 15 * s);
        Rectangle r = {c.w - tw - 32 * s, y, tw + 16 * s, 24 * s};
        DrawRectangleRec(r, Color{0, 0, 0, static_cast<unsigned char>(110 * a)});
        float x = r.x + 8 * s;
        Color kc = ui::withAlpha(k.killerBlue ? kAlly : kEnemy, a);
        Color vc = ui::withAlpha(k.killerBlue ? kEnemy : kAlly, a);
        if (k.killer == "YOU") kc = ui::withAlpha(Color{255, 225, 120, 255}, a);
        ui::text(k.killer, x, y + 4 * s, 15 * s, kc);
        x += ui::measure(k.killer, 15 * s);
        ui::text(mid, x, y + 4 * s, 15 * s, ui::withAlpha(WHITE, a * 0.7f));
        x += ui::measure(mid, 15 * s);
        ui::text(k.victim, x, y + 4 * s, 15 * s, k.victim == "YOU" ? ui::withAlpha(Color{255, 225, 120, 255}, a) : vc);
        y += 28 * s;
    }
}

void Hud::warnings(const HudContext& c) {
    const FlightState& ps = c.player->fm->state();
    float s = c.h / 720.0f;
    bool blink = std::fmod(time_, 0.5) < 0.3;
    float y = c.h * 0.30f;
    if (c.battle && c.battle->playerMissileWarning()) {
        if (blink) {
            const char* t = "MISSILE INCOMING";
            float tw = ui::measure(t, 30 * s);
            DrawRectangle(static_cast<int>(c.w / 2 - tw / 2 - 16 * s), static_cast<int>(y - 6 * s), static_cast<int>(tw + 32 * s), static_cast<int>(44 * s), Color{120, 0, 0, 150});
            ui::textShadow(t, c.w * 0.5f, y, 30 * s, Color{255, 80, 70, 255}, Align::Center);
        }
        ui::textShadow(fmt("DEPLOY FLARES  [X]   %.1f KM", c.battle->nearestThreatDistance() / 1000.0), c.w * 0.5f, y + 42 * s, 16 * s, WHITE, Align::Center);
        // direction arrow around the centre
        Vec3 d = c.battle->nearestThreatDir();
        Matrix view = GetCameraMatrix(c.cam);
        Vector3 right = {view.m0, view.m4, view.m8}, fwd = Vector3Normalize(Vector3Subtract(c.cam.target, c.cam.position));
        Vector3 dd = toRl(d);
        Vector2 dir = Vector2Normalize({Vector3DotProduct(dd, right), -Vector3DotProduct(dd, fwd)});
        Vector2 ctr = {c.w * 0.5f, c.h * 0.5f};
        float rr = 110 * s;
        Vector2 tip = {ctr.x + dir.x * (rr + 22 * s), ctr.y + dir.y * (rr + 22 * s)};
        Vector2 n = {-dir.y, dir.x};
        Vector2 b1 = {ctr.x + dir.x * rr + n.x * 14 * s, ctr.y + dir.y * rr + n.y * 14 * s};
        Vector2 b2 = {ctr.x + dir.x * rr - n.x * 14 * s, ctr.y + dir.y * rr - n.y * 14 * s};
        DrawTriangle(tip, b2, b1, Color{255, 60, 50, 230});
        DrawTriangle(tip, b1, b2, Color{255, 60, 50, 230});
        y += 80 * s;
    }
    if (c.gpws && blink) {
        ui::textShadow("PULL UP", c.w * 0.5f, y, 32 * s, Color{255, 60, 50, 255}, Align::Center);
        y += 44 * s;
    }
    if (ps.stalled && blink) {
        ui::textShadow("STALL", c.w * 0.5f, y, 28 * s, kWarn, Align::Center);
        y += 40 * s;
    }
    if (!ps.onGround && ps.gearPos > 0.5 && ps.airspeed > 150 && blink) {
        ui::textShadow("GEAR OVERSPEED", c.w * 0.5f, y, 22 * s, kWarn, Align::Center);
        y += 32 * s;
    }
    double horiz = std::sqrt(ps.pos.x * ps.pos.x + ps.pos.z * ps.pos.z);
    if (c.arena && horiz > 14000 && blink) {
        ui::textShadow("RETURN TO THE COMBAT AREA", c.w * 0.5f, y, 22 * s, kWarn, Align::Center);
        y += 32 * s;
    }
    if (ps.fuelKg < 900 && ps.engine == EngineState::Running && blink)
        ui::textShadow("BINGO FUEL", c.w * 0.5f, y, 20 * s, kWarn, Align::Center);
}

void Hud::missionPanel(const HudContext& c) {
    float s = c.h / 720.0f * std::max(panelK(c), 0.75f);   // compact panel (see panelK)
    if (c.mission) {
        const Mission& m = *c.mission;
        float w = 330 * s;
        float lineH = 22 * s;
        std::vector<std::string> instr;
        if (const MissionStep* cur = m.current()) instr = wrapText(cur->instruction, 13 * s, w - 24 * s);
        bool chip = m.current() && !m.current()->keyHint.empty();
        float h = 52 * s + lineH * m.stepCount() + 26 * s + 17 * s * std::max<size_t>(1, instr.size()) + (chip ? 24 * s : 0);
        Rectangle r = {18 * s, 18 * s, w, h};
        ui::panel(r, kPanel, kPanelBorder);
        ui::text(std::string("MISSION ") + m.id().substr(1), r.x + 12 * s, r.y + 8 * s, 13 * s, Color{150, 255, 205, 200});
        ui::text(m.title(), r.x + 12 * s, r.y + 24 * s, 19 * s, WHITE);
        float y = r.y + 52 * s;
        for (size_t i = 0; i < m.stepCount(); ++i) {
            const MissionStep& st = m.steps()[i];
            bool done = i < m.stepIndex();
            bool cur = i == m.stepIndex() && m.status() == MissionStatus::Running;
            Color col = done ? Color{120, 240, 160, 220} : cur ? WHITE : Color{255, 255, 255, 90};
            if (cur) DrawRectangle(static_cast<int>(r.x + 4 * s), static_cast<int>(y - 3 * s), static_cast<int>(w - 8 * s), static_cast<int>(lineH), Color{150, 255, 205, 35});
            const char* mark = done ? "+" : cur ? ">" : " ";
            ui::text(mark, r.x + 12 * s, y, 15 * s, col);
            ui::text(st.title, r.x + 30 * s, y, 15 * s, col);
            if (cur && !st.keyHint.empty()) {
                // key chip sits in the header row so long objective titles are never covered
                float kw = ui::measure(st.keyHint, 12 * s) + 12 * s;
                Rectangle kb = {r.x + 12 * s, r.y + 52 * s + lineH * m.stepCount() + 2 * s, kw, 18 * s};
                DrawRectangleRec(kb, Color{255, 255, 255, 220});
                ui::text(st.keyHint, kb.x + kw / 2, kb.y + 3 * s, 12 * s, BLACK, Align::Center);
            }
            y += lineH;
        }
        if (const MissionStep* cur = m.current()) {
            float iy = y + 4 * s + (chip ? 24 * s : 0);
            for (const std::string& l : instr) { ui::text(l, r.x + 12 * s, iy, 13 * s, Color{220, 230, 230, 230}, Align::Left, false); iy += 17 * s; }
            if (cur->progress && c.player) {
                MissionContext ctx{&c.player->fm->state(), &c.player->fm->profile(), nullptr, 0, c.battle};
                float pr = static_cast<float>(std::clamp(cur->progress(ctx), 0.0, 1.0));
                if (cur->holdSeconds > 0 && m.holdTimer() > 0) pr = static_cast<float>(m.holdTimer() / cur->holdSeconds);
                DrawRectangle(static_cast<int>(r.x + 12 * s), static_cast<int>(iy + 4 * s), static_cast<int>((w - 24 * s)), static_cast<int>(4 * s), Color{255, 255, 255, 40});
                DrawRectangle(static_cast<int>(r.x + 12 * s), static_cast<int>(iy + 4 * s), static_cast<int>((w - 24 * s) * pr), static_cast<int>(4 * s), Color{150, 255, 205, 220});
            }
            // live counters for campaign objectives
            if (c.campaign && c.battle && m.stepIndex() < c.campaign->objectives.size()) {
                const ObjectiveDef& o = c.campaign->objectives[m.stepIndex()];
                std::string tag;
                if (o.kind == ObjectiveKind::DestroyGroup)
                    tag = fmt2("TARGETS LEFT  %d / %d", c.battle->groundAlive(o.group), c.battle->groundTotal(o.group));
                else if (o.kind == ObjectiveKind::DestroyAir)
                    tag = fmti("BANDITS LEFT  %d", c.battle->enemiesAlive());
                if (!tag.empty()) ui::text(tag, r.x + w - 12 * s, r.y + 8 * s, 13 * s, kObjective, Align::Right);
            }
        }
        if (m.stepFlash() > 0 && m.stepIndex() > 0) {
            float a = static_cast<float>(std::min(1.0, m.stepFlash()));
            ui::textShadow(std::string("COMPLETE: ") + m.steps()[m.stepIndex() - 1].title, c.w * 0.5f, c.h * 0.2f, 22 * s, ui::withAlpha(Color{120, 255, 170, 255}, a), Align::Center);
        }
        if (m.status() == MissionStatus::Complete && c.missionDone) {
            ui::textShadow("MISSION COMPLETE", c.w * 0.5f, c.h * 0.16f, 40 * s, Color{120, 255, 170, 255}, Align::Center);
            // debrief card
            if (c.battle && c.player) {
                const Battle::PlayerStats& ps = c.battle->playerStats();
                Rectangle d = {c.w * 0.5f - 210 * s, c.h * 0.16f + 52 * s, 420 * s, 150 * s};
                ui::panel(d, Color{0, 0, 0, 170}, Color{120, 255, 170, 90});
                int secs = static_cast<int>(m.elapsed());
                struct Row { const char* k; std::string v; } rows[] = {
                    {"FLIGHT TIME", fmt2("%d:%02d", secs / 60, secs % 60)},
                    {"GROUND TARGETS DESTROYED", fmti("%d", c.battle->groundKilledByPlayer())},
                    {"AIRCRAFT SHOT DOWN", fmti("%d", c.player->kills)},
                    {"MISSILES  FIRED / HIT", fmt2("%d / %d", ps.missilesFired, ps.missileHits)},
                    {"CANNON ACCURACY", ps.roundsFired > 0 ? fmti("%d %%", static_cast<int>(100.0 * ps.roundHits / ps.roundsFired)) : std::string("-")},
                    {"HULL", fmti("%d %%", static_cast<int>(std::max(0.0, c.player->health)))},
                };
                float yy = d.y + 12 * s;
                for (const Row& r : rows) {
                    ui::text(r.k, d.x + 16 * s, yy, 13 * s, Color{200, 210, 215, 255});
                    ui::text(r.v, d.x + d.width - 16 * s, yy, 13 * s, WHITE, Align::Right);
                    yy += 21 * s;
                }
                ui::textShadow(c.missionNext, c.w * 0.5f, d.y + d.height + 12 * s, 17 * s, WHITE, Align::Center);
            } else {
                ui::textShadow(fmt("TIME %.0f s     ", m.elapsed()) + c.missionNext, c.w * 0.5f, c.h * 0.16f + 50 * s, 17 * s, WHITE, Align::Center);
            }
        }
    } else if (c.arena && c.battle) {
        Rectangle r = {18 * s, 18 * s, 250 * s, 78 * s};
        ui::panel(r, kPanel, kPanelBorder);
        ui::text("DOGFIGHT ARENA", r.x + 12 * s, r.y + 8 * s, 13 * s, Color{150, 255, 205, 200});
        ui::text(fmti("WAVE %d", c.battle->wave()), r.x + 12 * s, r.y + 26 * s, 22 * s, WHITE);
        ui::text(fmti("SCORE %d", c.battle->score()), r.x + r.width - 12 * s, r.y + 26 * s, 22 * s, Color{255, 225, 120, 255}, Align::Right);
        ui::text(fmti("BANDITS %d", c.battle->enemiesAlive()), r.x + 12 * s, r.y + 54 * s, 14 * s, kEnemy);
        ui::text(fmti("KILLS %d", c.player->kills), r.x + r.width - 12 * s, r.y + 54 * s, 14 * s, WHITE, Align::Right);
        if (c.battle->waveBannerTimer() > 0) {
            float a = static_cast<float>(std::min(1.0, c.battle->waveBannerTimer()));
            ui::textShadow(fmti("WAVE %d", c.battle->wave()), c.w * 0.5f, c.h * 0.19f, 46 * s, ui::withAlpha(WHITE, a), Align::Center);
            ui::textShadow(fmti("%d BANDITS INBOUND", c.battle->enemiesAlive()), c.w * 0.5f, c.h * 0.19f + 52 * s, 18 * s, ui::withAlpha(kEnemy, a), Align::Center);
        }
    }
}

void Hud::objectiveMarkers(const HudContext& c) {
    // waypoint diamond (with distance) for the current campaign objective; target groups are
    // already marked individually by targets()
    if (!c.campaign || !c.mission || c.mission->status() != MissionStatus::Running) return;
    size_t idx = c.mission->stepIndex();
    if (idx >= c.campaign->objectives.size()) return;
    const ObjectiveDef& o = c.campaign->objectives[idx];
    float s = c.h / 720.0f;
    const FlightState& ps = c.player->fm->state();
    if (c.battle && c.battle->tanker().active) refuelCues(c);
    if (ps.catArmed || ps.catFiring) {
        // catapult launch cues: one clear instruction at a time
        bool ab = ps.throttle >= FlightModel::kAbDetent - 1e-3;
        bool lit = ps.abLevel > 0.5;
        const char* msg = ps.catFiring ? "LAUNCH!" : !ab ? "HOLD  W  -  FULL AFTERBURNER" : !lit ? "AFTERBURNER LIGHTING..."
                        : ps.parkingBrake ? "PRESS  B  -  RELEASE BRAKE TO LAUNCH" : "STAND BY...";
        float pulse = 0.65f + 0.35f * std::sin(static_cast<float>(ps.simTime) * 5.0f);
        ui::textShadow(msg, c.w * 0.5f, c.h * 0.68f, 24 * s, ui::withAlpha(ps.catFiring ? Color{255, 220, 120, 255} : kHud, pulse), Align::Center);
        // throttle bar towards the afterburner detent
        float bw = 260 * s, bx = c.w * 0.5f - bw / 2, by = c.h * 0.68f + 34 * s;
        DrawRectangle(static_cast<int>(bx), static_cast<int>(by), static_cast<int>(bw), static_cast<int>(8 * s), Color{0, 0, 0, 120});
        DrawRectangle(static_cast<int>(bx), static_cast<int>(by), static_cast<int>(bw * std::min(1.0, ps.throttle)), static_cast<int>(8 * s),
                      ab ? Color{120, 190, 255, 230} : kHud);
        float det = bx + bw * static_cast<float>(FlightModel::kAbDetent);
        DrawRectangle(static_cast<int>(det), static_cast<int>(by - 4 * s), static_cast<int>(2 * s), static_cast<int>(16 * s), WHITE);
        ui::text("AB", det + 4 * s, by - 14 * s, 11 * s, Color{120, 190, 255, 230}, Align::Left, false);
    }
    if (o.kind != ObjectiveKind::Waypoint && o.kind != ObjectiveKind::Rendezvous) return;
    Vec3 wp{o.x, ps.pos.y, o.z};
    double dist = std::hypot(o.x - ps.pos.x, o.z - ps.pos.z);
    if (o.kind == ObjectiveKind::Rendezvous) {
        wp = c.battle->tanker().pos;
        dist = length(wp - ps.pos);
    }
    bool front;
    Vector2 sp = project(c, toRl(wp), &front);
    Vector2 ctr = {c.w * 0.5f, c.h * 0.5f};
    bool onScreen = front && sp.x > 30 * s && sp.x < c.w - 30 * s && sp.y > 30 * s && sp.y < c.h - 30 * s;
    if (!onScreen) {
        // pin to the screen edge in the direction of the waypoint
        Vector2 d = {sp.x - ctr.x, sp.y - ctr.y};
        if (!front) { d.x = -d.x; d.y = -d.y; }
        float len = std::max(1.0f, std::sqrt(d.x * d.x + d.y * d.y));
        float rad = std::min(c.w, c.h) * 0.42f;
        sp = {ctr.x + d.x / len * rad, ctr.y + d.y / len * rad};
    }
    float k = 11 * s;
    Vector2 top = {sp.x, sp.y - k}, left = {sp.x - k, sp.y}, bot = {sp.x, sp.y + k}, right = {sp.x + k, sp.y};
    ui::line(top, right, 2 * s, kObjective); ui::line(right, bot, 2 * s, kObjective);
    ui::line(bot, left, 2 * s, kObjective); ui::line(left, top, 2 * s, kObjective);
    ui::textShadow(o.title, sp.x, sp.y + k + 3 * s, 12 * s, kObjective, Align::Center);
    ui::textShadow(fmt("%.1f KM", dist / 1000.0), sp.x, sp.y - k - 16 * s, 12 * s, kObjective, Align::Center);
    if (o.maxAgl > 0) {
        bool ok = ps.agl < o.maxAgl;
        ui::textShadow(fmt2("AGL %d  /  MAX %d", static_cast<int>(ps.agl), static_cast<int>(o.maxAgl)), ctr.x, c.h * 0.28f, 15 * s,
                       ok ? Color{150, 255, 205, 255} : Color{255, 90, 70, 255}, Align::Center);
    }
}

// ------------------------------------------------------------------------------------------
// Training Academy
// ------------------------------------------------------------------------------------------
namespace {
const char* sectionWord(SectionKind k) {
    switch (k) {
        case SectionKind::Route: return "ROUTE";
        case SectionKind::Landing: case SectionKind::CarrierLanding: return "APPROACH";
        case SectionKind::Formation: return "FORMATION";
        case SectionKind::Rendezvous: case SectionKind::Refuel: return "TANKER";
        case SectionKind::ColdStart: case SectionKind::Lineup: case SectionKind::Takeoff: case SectionKind::GearUp: return "DEPARTURE";
        case SectionKind::Climb: return "CLIMB";
        case SectionKind::Engage: return "COMBAT";
        default: return "MANOEUVRE";
    }
}
}  // namespace

void Hud::trainingPanel(const HudContext& c) {
    if (!c.mission || !c.training) return;
    const Mission& m = *c.mission;
    const TrainingSession& t = *c.training;
    const Course& course = t.course();
    float s = c.h / 720.0f * std::max(panelK(c), 0.75f);   // compact panel (see panelK)
    float w = 340 * s;
    const MissionStep* cur = m.current();
    std::vector<std::string> instr;
    if (cur) instr = wrapText(cur->instruction, 13 * s, w - 24 * s);
    // a window of the section list around the current one
    const int n = static_cast<int>(m.stepCount());
    const int idx = static_cast<int>(std::min(m.stepIndex(), m.stepCount() ? m.stepCount() - 1 : 0));
    int first = std::clamp(idx - 2, 0, std::max(0, n - 6)), last = std::min(n, first + 6);
    float lineH = 20 * s;
    bool chip = cur && !cur->keyHint.empty();
    float h = 56 * s + lineH * (last - first) + 30 * s + 17 * s * std::max<size_t>(1, instr.size()) + (chip ? 24 * s : 0) + 40 * s;
    Rectangle r = {18 * s, 18 * s, w, h};
    ui::panel(r, kPanel, kPanelBorder);
    {
        std::string tt = c.trainingTitle;   // keep clear of the section counter on the right
        while (tt.size() > 8 && ui::measure(tt, 12 * s) > w - 140 * s) tt.pop_back();
        if (tt.size() < c.trainingTitle.size()) tt += "..";
        ui::text(tt, r.x + 12 * s, r.y + 8 * s, 12 * s, Color{150, 255, 205, 200});
    }
    ui::text(fmt2("SECTION %d / %d", idx + 1, n), r.x + w - 12 * s, r.y + 8 * s, 12 * s, Color{255, 255, 255, 150}, Align::Right);
    ui::text(cur ? cur->title : "COURSE COMPLETE", r.x + 12 * s, r.y + 24 * s, 18 * s, WHITE);
    float y = r.y + 54 * s;
    for (int i = first; i < last; ++i) {
        const MissionStep& st = m.steps()[i];
        bool done = i < static_cast<int>(m.stepIndex());
        bool now = i == static_cast<int>(m.stepIndex()) && m.status() == MissionStatus::Running;
        Color col = done ? Color{120, 240, 160, 220} : now ? WHITE : Color{255, 255, 255, 90};
        if (now) DrawRectangle(static_cast<int>(r.x + 4 * s), static_cast<int>(y - 3 * s), static_cast<int>(w - 8 * s), static_cast<int>(lineH), Color{150, 255, 205, 35});
        ui::text(done ? "+" : now ? ">" : " ", r.x + 12 * s, y, 14 * s, col);
        ui::text(st.title, r.x + 28 * s, y, 14 * s, col);
        if (i < static_cast<int>(course.sections.size()))
            ui::text(sectionWord(course.sections[i].kind), r.x + w - 12 * s, y + 2 * s, 10 * s, ui::withAlpha(col, 0.6f), Align::Right);
        y += lineH;
    }
    if (chip) {
        float kw = ui::measure(cur->keyHint, 12 * s) + 12 * s;
        Rectangle kb = {r.x + 12 * s, y + 2 * s, kw, 18 * s};
        DrawRectangleRec(kb, Color{255, 255, 255, 220});
        ui::text(cur->keyHint, kb.x + kw / 2, kb.y + 3 * s, 12 * s, BLACK, Align::Center);
        y += 24 * s;
    }
    y += 6 * s;
    for (const std::string& l : instr) { ui::text(l, r.x + 12 * s, y, 13 * s, Color{220, 230, 230, 230}, Align::Left, false); y += 17 * s; }
    if (cur && cur->progress && c.player) {
        MissionContext ctx{&c.player->fm->state(), &c.player->fm->profile(), nullptr, 0, c.battle};
        float pr = static_cast<float>(std::clamp(cur->progress(ctx), 0.0, 1.0));
        DrawRectangle(static_cast<int>(r.x + 12 * s), static_cast<int>(y + 4 * s), static_cast<int>(w - 24 * s), static_cast<int>(4 * s), Color{255, 255, 255, 40});
        DrawRectangle(static_cast<int>(r.x + 12 * s), static_cast<int>(y + 4 * s), static_cast<int>((w - 24 * s) * pr), static_cast<int>(4 * s), Color{150, 255, 205, 220});
    }
    // live score line
    ScoreCard sc = t.liveScore();
    std::string live = fmt2("GATES %d / %d", sc.gatesHit, sc.gatesTotal);
    if (sc.accuracy >= 0) live += fmti("   ACCURACY %d", static_cast<int>(sc.accuracy));
    live += fmti("   TIME %d:", static_cast<int>(sc.flightSeconds) / 60) + fmti("%02d", static_cast<int>(sc.flightSeconds) % 60);
    ui::text(live, r.x + 12 * s, r.y + h - 22 * s, 12 * s, Color{255, 225, 120, 230});
    // task status for manoeuvres / formation
    if (!t.taskStatus().empty() && m.status() == MissionStatus::Running)
        ui::textShadow(t.taskStatus(), c.w * 0.5f, c.h * 0.72f, 18 * s, kHud, Align::Center);
    if (m.stepFlash() > 0 && m.stepIndex() > 0) {
        float a = static_cast<float>(std::min(1.0, m.stepFlash()));
        ui::textShadow(std::string("COMPLETE: ") + m.steps()[m.stepIndex() - 1].title, c.w * 0.5f, c.h * 0.2f, 22 * s, ui::withAlpha(Color{120, 255, 170, 255}, a), Align::Center);
    }
}

void Hud::trainingMarkers(const HudContext& c) {
    const TrainingSession& t = *c.training;
    const FlightState& ps = c.player->fm->state();
    float s = c.h / 720.0f;
    if (c.battle && c.battle->tanker().active) refuelCues(c);
    const Gate* g = t.currentGate();
    if (!g) return;
    bool front;
    Vector2 sp = project(c, toRl(g->pos), &front);
    Vector2 ctr = {c.w * 0.5f, c.h * 0.5f};
    bool onScreen = front && sp.x > 30 * s && sp.x < c.w - 30 * s && sp.y > 30 * s && sp.y < c.h - 30 * s;
    double dist = length(g->pos - ps.pos);
    double agl = ps.pos.y - g->ground;
    bool windowSet = g->maxAgl > 0;
    bool low = windowSet && agl < g->minAgl, high = windowSet && agl > g->maxAgl;
    Color col = low || high ? kWarn : Color{120, 255, 190, 255};
    if (!onScreen) {
        Vector2 d = {sp.x - ctr.x, sp.y - ctr.y};
        if (!front) { d.x = -d.x; d.y = -d.y; }
        float len = std::max(1.0f, std::sqrt(d.x * d.x + d.y * d.y));
        float rad = std::min(c.w, c.h) * 0.42f;
        sp = {ctr.x + d.x / len * rad, ctr.y + d.y / len * rad};
        DrawPoly(sp, 3, 10 * s, std::atan2(d.y, d.x) * RAD2DEG, col);
    } else {
        // bracket the gate ring and label it
        Vector2 edge = project(c, toRl(g->pos + Vec3{0, g->radius, 0}));
        float rr = std::clamp(std::fabs(edge.y - sp.y), 8.0f * s, 260.0f * s);
        DrawRingLines(sp, rr, rr, 0, 360, 40, ui::withAlpha(col, 0.6f));
        float k = 7 * s;
        ui::line({sp.x - k, sp.y}, {sp.x + k, sp.y}, 1.5f * s, col);
        ui::line({sp.x, sp.y - k}, {sp.x, sp.y + k}, 1.5f * s, col);
    }
    const Section& sec = t.course().sections[t.section()];
    int gi = t.gateIndex() - (sec.kind == SectionKind::Route ? sec.gateBegin : sec.gateStart) + 1;
    int gn = sec.gateEnd - (sec.kind == SectionKind::Route ? sec.gateBegin : sec.gateStart);
    if (gi < 1) gi = 0;   // flying the go-around pattern
    ui::textShadow(dist > 2000 ? fmt("%.1f KM", dist / 1000.0) : fmti("%d M", static_cast<int>(dist)), sp.x, sp.y - 30 * s, 12 * s, col, Align::Center);
    if (gi > 0) ui::textShadow(fmt2("GATE %d / %d", gi, gn), sp.x, sp.y + 22 * s, 11 * s, col, Align::Center);
    else ui::textShadow("PATTERN", sp.x, sp.y + 22 * s, 11 * s, col, Align::Center);
    // altitude window / ceiling / speed band read-outs (top centre)
    float y = c.h * 0.24f;
    if (windowSet) {
        ui::textShadow(fmt2("GATE WINDOW %d-%d M AGL", static_cast<int>(g->minAgl), static_cast<int>(g->maxAgl)) + fmti("   YOU %d", static_cast<int>(agl)), ctr.x, y, 15 * s,
                       col, Align::Center);
        y += 20 * s;
    }
    if (sec.ceilingAgl > 0) {
        bool over = ps.agl > sec.ceilingAgl;
        ui::textShadow(fmt2("CEILING %d M AGL   (AGL %d)", static_cast<int>(sec.ceilingAgl), static_cast<int>(ps.agl)), ctr.x, y, 14 * s, over ? kWarn : kHud, Align::Center);
        y += 19 * s;
    }
    if (sec.speedMax > 0) {
        double kmh = ps.airspeed * 3.6;
        bool ok = ps.airspeed >= sec.speedMin * 0.95 && ps.airspeed <= sec.speedMax * 1.05;
        ui::textShadow(fmt2("SPEED BAND %d-%d KM/H", static_cast<int>(sec.speedMin * 3.6), static_cast<int>(sec.speedMax * 3.6)) + fmti("   (%d)", static_cast<int>(kmh)),
                       ctr.x, y, 14 * s, ok ? kHud : kWarn, Align::Center);
    }
}

void Hud::trainingApproach(const HudContext& c) {
    const TrainingSession& t = *c.training;
    if (!t.onApproach()) return;
    const FlightState& st = c.player->fm->state();
    float s = c.h / 720.0f;
    double d = t.distanceToAim(st);
    if (d < -200 || d > 12000) return;
    double dev = t.glideDeviation(st);       // + = high
    double lat = t.lineupOffset(st);         // + = right of the centreline
    bool carrier = t.course().carrier;
    float cx = c.w * 0.5f, cy = c.h * 0.5f;
    float sx = cx + 150 * s, top = cy - 90 * s, hgt = 180 * s;
    double fullDev = carrier ? 30.0 : 60.0, fullLat = carrier ? 40.0 : 120.0;
    for (int k = -2; k <= 2; ++k) DrawCircleLines(static_cast<int>(sx), static_cast<int>(cy + k * hgt / 4), 4 * s, kHudDim);
    float gy = cy + static_cast<float>(std::clamp(dev / fullDev, -1.0, 1.0)) * hgt / 2;
    // carrier: the "meatball" (amber light) against the datum bar; runway: a glide-slope diamond
    if (carrier) {
        DrawLineEx({sx - 22 * s, cy}, {sx + 22 * s, cy}, 3 * s, Color{120, 255, 150, 230});
        DrawCircleV({sx, gy}, 7 * s, Color{255, 190, 60, 255});
        ui::text("BALL", sx - 14 * s, top - 20 * s, 13 * s, kHud);
    } else {
        DrawPoly({sx, gy}, 4, 8 * s, 45, kHud);
        ui::text("GS", sx - 6 * s, top - 20 * s, 13 * s, kHud);
    }
    float ly = cy + 120 * s, lw = 180 * s;
    for (int k = -2; k <= 2; ++k) DrawCircleLines(static_cast<int>(cx + k * lw / 4), static_cast<int>(ly), 4 * s, kHudDim);
    float lx = cx - static_cast<float>(std::clamp(lat / fullLat, -1.0, 1.0)) * lw / 2;
    DrawPoly({lx, ly}, 4, 8 * s, 45, kHud);
    ui::text("LOC", cx - lw / 2 - 40 * s, ly - 7 * s, 13 * s, kHud);
    const AircraftProfile& p = c.player->fm->profile();
    double vref = p.approachSpeedMs;
    ui::textShadow(fmt(carrier ? "DECK %.1f KM" : "RWY %.1f KM", std::max(0.0, d) / 1000.0), sx, top + hgt + 10 * s, 15 * s, kHud, Align::Center);
    double tol = std::max(carrier ? 4.0 : 6.0, d * 0.012);
    const char* gs = dev > tol ? "HIGH" : dev < -tol ? "LOW" : "ON PATH";
    ui::textShadow(gs, sx, top + hgt + 28 * s, 14 * s, std::fabs(dev) > tol ? kWarn : kHud, Align::Center);
    bool fast = st.airspeed > vref + 15, slow = st.airspeed < vref - 10;
    ui::textShadow(fmti("APPROACH SPEED %d KM/H", static_cast<int>(vref * 3.6)) + fmti("   (%d)", static_cast<int>(st.airspeed * 3.6)), cx, ly + 22 * s, 14 * s,
                   fast || slow ? kWarn : kHud, Align::Center);
    if (!st.gearDown && d < 7000.0) ui::textShadow(carrier ? "GEAR + HOOK DOWN  (G)" : "GEAR DOWN  (G)", cx, ly + 42 * s, 18 * s, kWarn, Align::Center);
}

void Hud::trainingDebrief(const HudContext& c) {
    if (!c.trainingResult) return;
    const ScoreCard& sc = *c.trainingResult;
    float s = c.h / 720.0f;
    // no medals: the score, the categories and the completion
    Color medalCol = sc.total >= 78.0 ? Color{150, 255, 205, 255} : sc.total >= 62.0 ? Color{255, 225, 120, 255} : Color{255, 160, 110, 255};
    Rectangle d = {c.w * 0.5f - 230 * s, c.h * 0.14f, 460 * s, 352 * s};
    ui::panel(d, Color{0, 0, 0, 190}, ui::withAlpha(medalCol, 0.5f));
    ui::textShadow("COURSE COMPLETE", d.x + d.width / 2, d.y + 12 * s, 26 * s, WHITE, Align::Center);
    ui::textShadow(fmti("SCORE %d", static_cast<int>(std::lround(sc.total))), d.x + d.width / 2, d.y + 46 * s, 30 * s, medalCol, Align::Center);
    ui::textShadow(c.trainingNewBest ? "NEW PERSONAL BEST" : "COMPLETED", d.x + d.width / 2, d.y + 84 * s, 16 * s, Color{255, 225, 120, 255}, Align::Center);
    struct Row { const char* k; double v; } rows[] = {
        {"WAYPOINT ACCURACY", sc.accuracy}, {"MANOEUVRING", sc.manoeuvre}, {"ALTITUDE / SPEED DISCIPLINE", sc.discipline},
        {"TERRAIN CLEARANCE", sc.clearance}, {"STABILITY / CONTROL", sc.stability}, {"REFUELLING", sc.refuel}, {"LANDING", sc.landing},
        {"COMBAT", sc.combat},
    };
    float y = d.y + 112 * s;
    for (const Row& r : rows) {
        ui::text(r.k, d.x + 18 * s, y, 13 * s, Color{200, 210, 215, 255});
        if (r.v < 0) ui::text("-", d.x + d.width - 18 * s, y, 13 * s, Color{255, 255, 255, 120}, Align::Right);
        else {
            float bw = 120 * s, bx = d.x + d.width - 18 * s - bw - 40 * s;
            DrawRectangle(static_cast<int>(bx), static_cast<int>(y + 4 * s), static_cast<int>(bw), static_cast<int>(6 * s), Color{255, 255, 255, 40});
            DrawRectangle(static_cast<int>(bx), static_cast<int>(y + 4 * s), static_cast<int>(bw * r.v / 100.0), static_cast<int>(6 * s), medalCol);
            ui::text(fmti("%d", static_cast<int>(std::lround(r.v))), d.x + d.width - 18 * s, y, 13 * s, WHITE, Align::Right);
        }
        y += 21 * s;
    }
    int secs = static_cast<int>(sc.flightSeconds);
    std::string foot = fmt2("GATES %d / %d", sc.gatesHit, sc.gatesTotal) + fmt2("     TIME %d:%02d", secs / 60, secs % 60);
    if (sc.targets > 0) foot += fmt2("     KILLS %d / %d", sc.kills, sc.targets);
    if (sc.crashes) foot += fmti("     RETRIES %d", sc.crashes);
    if (sc.wire > 0) foot += fmti("     WIRE %d", sc.wire);
    ui::text(foot, d.x + d.width / 2, y + 6 * s, 12 * s, Color{220, 230, 230, 230}, Align::Center);
    ui::textShadow(c.trainingNext, c.w * 0.5f, d.y + d.height + 14 * s, 17 * s, WHITE, Align::Center);
}

void Hud::refuelCues(const HudContext& c) {
    // Tanker marker at any range; within 700 m of the basket, precision steering cues for the probe.
    const Tanker& tk = c.battle->tanker();
    const FlightState& ps = c.player->fm->state();
    float s = c.h / 720.0f;
    const Color cyan = {110, 220, 255, 255};
    bool front;
    Vector2 tp = project(c, toRl(tk.pos + Vec3{0, 6, 0}), &front);
    double dT = length(tk.pos - ps.pos);
    if (front && dT > 250) {
        DrawRectangleLinesEx({tp.x - 9 * s, tp.y - 9 * s, 18 * s, 18 * s}, 2 * s, cyan);
        ui::textShadow("TANKER", tp.x, tp.y + 11 * s, 11 * s, cyan, Align::Center);
        ui::textShadow(fmt("%.1f", dT / 1000.0), tp.x, tp.y - 24 * s, 11 * s, cyan, Align::Center);
    }
    // fuel readout whenever a tanker is around
    double maxFuel = c.player->fm->profile().fuelMassKg;
    float fuelPct = static_cast<float>(100.0 * ps.fuelKg / std::max(1.0, maxFuel));
    ui::textShadow(fmt("FUEL %.0f %%", fuelPct), c.w * 0.5f, c.h * 0.78f, 18 * s, fuelPct < 20 ? Color{255, 90, 70, 255} : cyan, Align::Center);
    Vec3 probe = ps.pos + ps.q.forward() * 8.0 + ps.q.up() * 1.0;
    Vec3 rel = tk.basket() - probe;           // where the basket is relative to the probe
    double dist = length(rel);
    if (dist > 700) return;
    Vector2 bp = project(c, toRl(tk.basket()), &front);
    if (front) {
        DrawRing(bp, 10 * s, 13 * s, 0, 360, 32, tk.contact ? Color{120, 255, 170, 255} : cyan);
        ui::textShadow("BASKET", bp.x, bp.y + 15 * s, 11 * s, cyan, Align::Center);
    }
    double closure = -dot(ps.vel - tk.vel, normalize(rel));   // + = closing
    // steering in the jet's own frame
    double up = dot(rel, ps.q.up()), right = dot(rel, ps.q.right()), fwd = dot(rel, ps.q.forward());
    float cx = c.w * 0.5f, cy = c.h * 0.66f;
    if (tk.contact) {
        ui::textShadow(fmt("CONTACT  -  FUEL FLOWING  %.0f %%", fuelPct), cx, cy, 22 * s, Color{120, 255, 170, 255}, Align::Center);
        return;
    }
    std::string cue;
    if (std::fabs(up) > 2.5) cue += up > 0 ? "UP  " : "DOWN  ";
    if (std::fabs(right) > 2.5) cue += right > 0 ? "RIGHT  " : "LEFT  ";
    if (fwd > 6 && closure < 2.0) cue += "ADD POWER  ";
    if (closure > 8.0 && dist < 120) cue += "SLOW DOWN  ";
    if (cue.empty()) cue = "STEADY...";
    ui::textShadow(cue, cx, cy, 20 * s, cyan, Align::Center);
    ui::textShadow(fmt("BASKET %.0f m", dist) + fmt("   CLOSURE %+.0f m/s", closure), cx, cy + 26 * s, 15 * s, WHITE, Align::Center);
}

void Hud::glidePath(const HudContext& c) {
    // ILS-style glide-slope and localiser deviation for Mission 03
    const FlightState& st = c.player->fm->state();
    const RunwayInfo& rw = c.runway;   // never construct a Terrain here: that rebuilds the whole eroded world
    float s = c.h / 720.0f;
    const int dir = c.approachDir;
    double d = (landingThreshold(rw, dir).x - st.pos.x) * dir;
    double hAbove = st.pos.y - rw.center.y;
    double dev = hAbove - glidePathHeight(d);          // + = above the path
    double lat = (st.pos.z - rw.center.z) * dir;       // + = right of centreline
    float cx = c.w * 0.5f, cy = c.h * 0.5f;
    float sx = cx + 150 * s, top = cy - 90 * s, hgt = 180 * s;
    for (int k = -2; k <= 2; ++k) DrawCircleLines(static_cast<int>(sx), static_cast<int>(cy + k * hgt / 4), 4 * s, kHudDim);
    float gy = cy + static_cast<float>(std::clamp(dev / 120.0, -1.0, 1.0)) * hgt / 2;   // above path -> diamond low ("fly down")
    DrawPoly({sx, gy}, 4, 8 * s, 45, kHud);
    ui::text("GS", sx - 6 * s, top - 20 * s, 13 * s, kHud);
    float ly = cy + 120 * s, lw = 180 * s;
    for (int k = -2; k <= 2; ++k) DrawCircleLines(static_cast<int>(cx + k * lw / 4), static_cast<int>(ly), 4 * s, kHudDim);
    float lx = cx - static_cast<float>(std::clamp(lat / 150.0, -1.0, 1.0)) * lw / 2;    // right of centreline -> bar left ("fly left")
    DrawPoly({lx, ly}, 4, 8 * s, 45, kHud);
    ui::text("LOC", cx - lw / 2 - 40 * s, ly - 7 * s, 13 * s, kHud);
    ui::textShadow(fmt("RWY %.1f KM", d / 1000.0), sx, top + hgt + 10 * s, 15 * s, kHud, Align::Center);
    ui::textShadow(dev > 45 ? "HIGH" : dev < -45 ? "LOW" : "ON PATH", sx, top + hgt + 28 * s, 14 * s, std::fabs(dev) > 45 ? kWarn : kHud, Align::Center);
}

void Hud::radioPanel(const HudContext& c) {
    if (c.settings && !c.settings->captions) return;
    float s = c.h / 720.0f;
    float y = 84 * s;
    // keep captions in the centre column, clear of the mission panel (left) and gauges (right)
    float maxW = std::max(320.0f * s, c.w - 2.0f * 372.0f * s);
    for (auto it = radio_.begin(); it != radio_.end(); ++it) {
        float a = static_cast<float>(std::clamp(std::min(it->t * 4.0, it->dur - it->t), 0.0, 1.0));
        std::string who = it->who + ":  ";
        float whoW = ui::measure(who, 16 * s);
        std::vector<std::string> lines = wrapText(it->text, 16 * s, maxW - whoW);
        float tw = 0;
        for (const auto& l : lines) tw = std::max(tw, ui::measure(l, 16 * s, false));
        float boxW = whoW + tw;
        float boxH = 22 * s * lines.size() + 4 * s;
        DrawRectangle(static_cast<int>(c.w / 2 - boxW / 2 - 12 * s), static_cast<int>(y - 4 * s), static_cast<int>(boxW + 24 * s),
                      static_cast<int>(boxH + 4 * s), Color{0, 0, 0, static_cast<unsigned char>(130 * a)});
        float x = c.w / 2 - boxW / 2;
        ui::text(it->who + ":", x, y, 16 * s, ui::withAlpha(Color{150, 255, 205, 255}, a));
        for (const auto& l : lines) {
            ui::text(l, x + whoW, y, 16 * s, ui::withAlpha(WHITE, a), Align::Left, false);
            y += 22 * s;
        }
        y += 12 * s;
    }
}

void Hud::popups(const HudContext& c) {
    float s = c.h / 720.0f;
    float y = c.h * 0.58f;
    for (const Popup& p : popups_) {
        float a = static_cast<float>(std::clamp(2.4 - p.t, 0.0, 1.0));
        float rise = static_cast<float>(p.t * 12.0);
        ui::textShadow(p.text, c.w * 0.5f, y - rise * s, 20 * s, ui::withAlpha(p.color, a), Align::Center);
        y += 26 * s;
    }
}

void Hud::debug(const HudContext& c) {
    float s = c.h / 720.0f * std::max(panelK(c), 0.75f);   // compact panel (see panelK)
    const FlightState& st = c.player->fm->state();
    Rectangle r = {c.w - 300 * s, 200 * s, 282 * s, 340 * s};
    ui::panel(r, Color{0, 0, 0, 170}, kPanelBorder);
    float x = r.x + 10 * s, y = r.y + 8 * s, lh = 17 * s;
    auto row = [&](const std::string& t) { ui::text(t, x, y, 13 * s, WHITE, Align::Left, false); y += lh; };
    row(fmt("FPS %.0f", c.frame.mean > 0 ? 1000.0 / c.frame.mean : 0.0));
    row(fmt("frame p50 %.2f ms", c.frame.p50) + fmt("  p95 %.2f", c.frame.p95));
    row(fmt("frame p99 %.2f ms", c.frame.p99) + fmt("  max %.1f", c.frame.max));
    row(fmt("world pass %.2f ms (CPU submit)", c.worldMs));
    row(fmt("simulation %.2f ms", c.simMs));
    row(fmt("hud %.2f ms (previous frame)", c.hudMs));
    row(fmti("draw calls %d", c.drawCalls) + fmti("   particles %d", static_cast<int>(c.particles)));
    row(fmt("sim %.0f Hz fixed", c.fixedHz) + fmti("  dropped %d", c.droppedSteps));
    row(fmt("pos %.0f, ", st.pos.x) + fmt("%.0f, ", st.pos.y) + fmt("%.0f", st.pos.z));
    row(fmt("TAS %.1f m/s", st.airspeed) + fmt("  qbar %.0f Pa", st.qbar));
    row(fmt("alpha %.1f deg", st.alpha / kDeg) + fmt("  beta %.1f", st.beta / kDeg));
    row(fmt("CL %.2f", st.cl) + fmt("  CD %.3f", st.cd) + fmt("  n %.2f g", st.gLoad));
    row(fmt("thrust %.0f kN", st.thrustN / 1000.0) + fmt("  rpm %.0f%%", st.rpm * 100.0));
    row(fmt("fuel %.0f kg", st.fuelKg) + fmt("  mass %.0f kg", c.player->fm->massKg()));
    row(std::string("handling ") + (c.player->fm->handling() == HandlingMode::Arcade ? "ARCADE" : c.player->fm->handling() == HandlingMode::Assisted ? "ASSISTED" : "SIMULATION"));
    row(std::string("time of day ") + c.timeOfDay + (c.gamepad ? "   PAD" : ""));
    row(std::string("camera ") + cameraName(c.camMode));
}

void Hud::help(const HudContext& c) {
    float s = c.h / 720.0f;
    Rectangle r = {c.w * 0.5f - 300 * s, c.h * 0.5f - 250 * s, 600 * s, 500 * s};
    ui::panel(r, Color{0, 0, 0, 200}, kPanelBorder);
    ui::text("CONTROLS", r.x + 20 * s, r.y + 14 * s, 22 * s, WHITE);
    const char* rows[][2] = {
        {"W / S", "Throttle up / down (full = afterburner)"}, {"Arrow Up / Down", "Pitch (Down = pull up)"},
        {"Arrow Left / Right", "Roll"}, {"A / D", "Yaw / nosewheel steering"}, {"E", "Engine start"},
        {"B", "Parking brake"}, {"Left Ctrl", "Wheel brakes / air brake (hold)"}, {"G", "Landing gear"},
        {"Space / Left mouse", "Cannon"}, {"F", "Fire missile (lock first)"}, {"Q  /  1  /  2", "Swap / select ATA or ATS missiles"},
        {"Double-tap Left/Right", "Aileron roll"}, {"X", "Flares"}, {"T / Tab", "Cycle target"},
        {"C", "Camera: cockpit / chase / external"}, {"Right mouse drag, I J K L", "Look around / orbit"},
        {"R", "Reset / respawn"}, {"F3 / F5 / U", "Telemetry / time of day / HUD style"}, {"Esc / P", "Pause menu (Cmd+Q quits)"}, {"H", "Close help"}};
    float y = r.y + 50 * s;
    for (auto& row : rows) {
        ui::text(row[0], r.x + 20 * s, y, 15 * s, Color{150, 255, 205, 255});
        ui::text(row[1], r.x + 230 * s, y, 15 * s, WHITE, Align::Left, false);
        y += 20 * s;
    }
}

}  // namespace sky
