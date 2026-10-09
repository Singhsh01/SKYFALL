#include "Font.hpp"

#include <algorithm>
#include <cmath>

namespace sky::ui {

namespace {
Font g_bold{}, g_regular{};
bool g_custom = false;

Font tryLoad(const char* const* paths, int count, int size) {
    for (int i = 0; i < count; ++i) {
        if (FileExists(paths[i])) {
            Font f = LoadFontEx(paths[i], size, nullptr, 0);
            if (f.texture.id != 0 && f.glyphCount > 0) {
                SetTextureFilter(f.texture, TEXTURE_FILTER_BILINEAR);
                return f;
            }
        }
    }
    return Font{};
}
}  // namespace

void loadFonts() {
    const char* bold[] = {
        "/System/Library/Fonts/Supplemental/DIN Alternate Bold.ttf",
        "/System/Library/Fonts/Supplemental/DIN Condensed Bold.ttf",
        "/System/Library/Fonts/Supplemental/Arial Bold.ttf",
        "/Library/Fonts/Arial Bold.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSansCondensed-Bold.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
        "C:/Windows/Fonts/bahnschrift.ttf",
        "C:/Windows/Fonts/arialbd.ttf",
    };
    const char* regular[] = {
        "/System/Library/Fonts/Supplemental/DIN Alternate Bold.ttf",
        "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/Library/Fonts/Arial.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSansCondensed.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "C:/Windows/Fonts/arial.ttf",
    };
    g_bold = tryLoad(bold, sizeof(bold) / sizeof(bold[0]), 64);
    g_regular = tryLoad(regular, sizeof(regular) / sizeof(regular[0]), 48);
    g_custom = g_bold.texture.id != 0;
    if (!g_custom) g_bold = GetFontDefault();
    if (g_regular.texture.id == 0) g_regular = g_bold;
}

void unloadFonts() {
    if (g_custom) {
        if (g_regular.texture.id != g_bold.texture.id) UnloadFont(g_regular);
        UnloadFont(g_bold);
    }
}

const Font& font(bool bold) { return bold ? g_bold : g_regular; }

float measure(const std::string& s, float size, bool bold) {
    return MeasureTextEx(font(bold), s.c_str(), size, size * 0.06f).x;
}

void text(const std::string& s, float x, float y, float size, Color c, Align a, bool bold) {
    float w = measure(s, size, bold);
    if (a == Align::Center) x -= w * 0.5f;
    else if (a == Align::Right) x -= w;
    DrawTextEx(font(bold), s.c_str(), {std::round(x), std::round(y)}, size, size * 0.06f, c);
}

void textShadow(const std::string& s, float x, float y, float size, Color c, Align a, bool bold) {
    Color sh = {0, 0, 0, static_cast<unsigned char>(c.a * 0.6f)};
    text(s, x + 1.5f, y + 1.5f, size, sh, a, bold);
    text(s, x, y, size, c, a, bold);
}

Color withAlpha(Color c, float a) {
    c.a = static_cast<unsigned char>(std::clamp(a, 0.0f, 1.0f) * c.a);
    return c;
}

void line(Vector2 a, Vector2 b, float thick, Color c, bool outline) {
    if (outline) DrawLineEx(a, b, thick + 2.0f, Color{0, 0, 0, static_cast<unsigned char>(c.a * 0.35f)});
    DrawLineEx(a, b, thick, c);
}

void panel(Rectangle r, Color fill, Color border) {
    DrawRectangleRec(r, fill);
    DrawRectangleLinesEx(r, 1.0f, border);
}

}  // namespace sky::ui
