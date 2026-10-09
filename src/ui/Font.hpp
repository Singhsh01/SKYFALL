// UI text helpers. Tries a few system fonts (never bundled) and falls back to raylib's default.
#pragma once

#include "raylib.h"

#include <string>

namespace sky::ui {

enum class Align { Left, Center, Right };

void loadFonts();
void unloadFonts();
const Font& font(bool bold = true);
void text(const std::string& s, float x, float y, float size, Color c, Align a = Align::Left, bool bold = true);
void textShadow(const std::string& s, float x, float y, float size, Color c, Align a = Align::Left, bool bold = true);
float measure(const std::string& s, float size, bool bold = true);
Color withAlpha(Color c, float a);
/// Thin line with optional dark outline for legibility over bright skies.
void line(Vector2 a, Vector2 b, float thick, Color c, bool outline = true);
void panel(Rectangle r, Color fill, Color border);

}  // namespace sky::ui
