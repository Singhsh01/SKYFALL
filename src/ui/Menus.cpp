#include "Menus.hpp"

#include "Font.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace sky::ui {

namespace {
const Color kAccent = {150, 255, 205, 255};
const Color kFocusFill = {150, 255, 205, 40};
bool hovered(Rectangle r) { return CheckCollisionPointRec(GetMousePosition(), r); }
}  // namespace

void MenuNav::begin(int items, bool up, bool down, bool enter, bool l, bool r) {
    count = items;
    if (count > 0) {
        if (up) focus = (focus + count - 1) % count;
        if (down) focus = (focus + 1) % count;
        focus = std::clamp(focus, 0, count - 1);
    }
    confirm = enter;
    left = l;
    right = r;
}

static void frame(Rectangle r, bool focused) {
    if (focused) {
        DrawRectangleRec(r, kFocusFill);
        DrawRectangle(static_cast<int>(r.x), static_cast<int>(r.y), 4, static_cast<int>(r.height), kAccent);
    } else {
        DrawRectangleRec(r, Color{0, 0, 0, 90});
    }
}

bool button(MenuNav& nav, int idx, Rectangle r, const std::string& label, const std::string& sub, bool enabled) {
    bool hov = hovered(r);
    if (hov && (GetMouseDelta().x != 0 || GetMouseDelta().y != 0)) nav.focus = idx;
    bool focused = nav.focus == idx;
    frame(r, focused);
    float s = r.height / 52.0f;
    Color c = enabled ? (focused ? WHITE : Color{220, 225, 225, 220}) : Color{140, 140, 140, 160};
    text(label, r.x + 20 * s, r.y + (sub.empty() ? r.height / 2 - 11 * s : 7 * s), 22 * s, c);
    if (!sub.empty()) text(sub, r.x + 20 * s, r.y + 31 * s, 14 * s, Color{170, 190, 185, 200}, Align::Left, false);
    bool clicked = enabled && ((hov && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) || (focused && nav.confirm));
    return clicked;
}

bool slider(MenuNav& nav, int idx, Rectangle r, const std::string& label, double& value, double lo, double hi, bool percent) {
    bool hov = hovered(r);
    if (hov && (GetMouseDelta().x != 0 || GetMouseDelta().y != 0)) nav.focus = idx;
    bool focused = nav.focus == idx;
    frame(r, focused);
    float s = r.height / 44.0f;
    text(label, r.x + 16 * s, r.y + r.height / 2 - 9 * s, 17 * s, WHITE);
    Rectangle track = {r.x + r.width * 0.45f, r.y + r.height / 2 - 3 * s, r.width * 0.40f, 6 * s};
    DrawRectangleRec(track, Color{255, 255, 255, 50});
    double t = (value - lo) / (hi - lo);
    DrawRectangleRec({track.x, track.y, track.width * static_cast<float>(t), track.height}, kAccent);
    DrawCircleV({track.x + track.width * static_cast<float>(t), track.y + track.height / 2}, 8 * s, WHITE);
    char buf[32];
    if (percent) std::snprintf(buf, sizeof buf, "%d%%", static_cast<int>(std::round(t * 100)));
    else std::snprintf(buf, sizeof buf, "%.0f", value);
    text(buf, r.x + r.width - 14 * s, r.y + r.height / 2 - 9 * s, 17 * s, WHITE, Align::Right);
    bool changed = false;
    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(GetMousePosition(), {track.x - 10, r.y, track.width + 20, r.height})) {
        double nt = std::clamp((GetMousePosition().x - track.x) / track.width, 0.0f, 1.0f);
        value = lo + nt * (hi - lo);
        changed = true;
    }
    if (focused && (nav.left || nav.right)) {
        value = std::clamp(value + (nav.right ? 1 : -1) * (hi - lo) * 0.05, lo, hi);
        changed = true;
    }
    return changed;
}

bool toggle(MenuNav& nav, int idx, Rectangle r, const std::string& label, bool& value, const char* on, const char* off) {
    bool hov = hovered(r);
    if (hov && (GetMouseDelta().x != 0 || GetMouseDelta().y != 0)) nav.focus = idx;
    bool focused = nav.focus == idx;
    frame(r, focused);
    float s = r.height / 44.0f;
    text(label, r.x + 16 * s, r.y + r.height / 2 - 9 * s, 17 * s, WHITE);
    Rectangle sw = {r.x + r.width - 74 * s, r.y + r.height / 2 - 11 * s, 58 * s, 22 * s};
    DrawRectangleRounded(sw, 1.0f, 8, value ? kAccent : Color{255, 255, 255, 50});
    DrawCircleV({value ? sw.x + sw.width - 11 * s : sw.x + 11 * s, sw.y + 11 * s}, 9 * s, WHITE);
    text(value ? on : off, sw.x - 10 * s, r.y + r.height / 2 - 8 * s, 14 * s, Color{200, 210, 210, 220}, Align::Right);
    bool clicked = (hov && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) || (focused && (nav.confirm || nav.left || nav.right));
    if (clicked) value = !value;
    return clicked;
}

bool cycle(MenuNav& nav, int idx, Rectangle r, const std::string& label, int& value, const std::vector<std::string>& options) {
    bool hov = hovered(r);
    if (hov && (GetMouseDelta().x != 0 || GetMouseDelta().y != 0)) nav.focus = idx;
    bool focused = nav.focus == idx;
    frame(r, focused);
    float s = r.height / 44.0f;
    text(label, r.x + 16 * s, r.y + r.height / 2 - 9 * s, 17 * s, WHITE);
    int n = static_cast<int>(options.size());
    text("<  " + options[(value % n + n) % n] + "  >", r.x + r.width - 16 * s, r.y + r.height / 2 - 9 * s, 17 * s, kAccent, Align::Right);
    bool changed = false;
    if ((hov && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) || (focused && (nav.confirm || nav.right))) { value = (value + 1) % n; changed = true; }
    else if (focused && nav.left) { value = (value + n - 1) % n; changed = true; }
    return changed;
}

int selector(MenuNav& nav, int idx, Rectangle r, const std::string& label, const std::string& value, const std::string& note) {
    bool hov = hovered(r);
    if (hov && (GetMouseDelta().x != 0 || GetMouseDelta().y != 0)) nav.focus = idx;
    bool focused = nav.focus == idx;
    frame(r, focused);
    float s = r.height / 46.0f;
    text(label, r.x + 12 * s, r.y + 6 * s, 11 * s, Color{255, 176, 60, 220});
    if (!note.empty()) text(note, r.x + r.width - 12 * s, r.y + 6 * s, 11 * s, Color{170, 190, 185, 220}, Align::Right, false);
    text("<", r.x + 12 * s, r.y + r.height - 25 * s, 17 * s, kAccent);
    text(">", r.x + r.width - 12 * s, r.y + r.height - 25 * s, 17 * s, kAccent, Align::Right);
    std::string v = value;
    while (v.size() > 3 && measure(v + "..", 16 * s, true) > r.width - 64 * s) v.pop_back();
    if (v != value) v += "..";
    text(v, r.x + r.width / 2, r.y + r.height - 26 * s, 16 * s, WHITE, Align::Center);
    if (hov && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) return GetMousePosition().x < r.x + r.width / 2 ? -1 : 1;
    if (focused && (nav.confirm || nav.right)) return 1;
    if (focused && nav.left) return -1;
    return 0;
}

void title(const std::string& t, float x, float y, float size) {
    textShadow(t, x, y, size, WHITE);
    DrawRectangle(static_cast<int>(x), static_cast<int>(y + size + 6), 60, 3, kAccent);
}

void logo(float x, float y, float scale, double time) {
    float s = scale;
    // stylised delta mark (original)
    Vector2 a = {x, y + 6 * s}, b = {x + 34 * s, y + 52 * s}, c = {x - 34 * s, y + 52 * s};
    DrawTriangle(a, c, b, Color{150, 255, 205, 230});
    DrawTriangle({x, y + 26 * s}, {x - 14 * s, y + 52 * s}, {x + 14 * s, y + 52 * s}, Color{10, 20, 24, 255});
    textShadow("SKYFALL", x + 52 * s, y - 4 * s, 72 * s, WHITE);
    float glow = static_cast<float>(0.6 + 0.4 * std::sin(time * 1.6));
    text("COMBAT FLIGHT SIMULATOR  &  VISION LAB", x + 56 * s, y + 66 * s, 16 * s, withAlpha(kAccent, 0.6f + 0.4f * glow));
}

}  // namespace sky::ui
