// Tiny immediate-mode widgets for menus (mouse + keyboard focus).
#pragma once

#include "raylib.h"

#include <string>
#include <vector>

namespace sky::ui {

struct MenuNav {
    int focus = 0;
    int count = 0;
    bool confirm = false;
    bool left = false, right = false;
    void begin(int items, bool up, bool down, bool enter, bool l, bool r);
};

bool button(MenuNav& nav, int idx, Rectangle r, const std::string& label, const std::string& sub = "", bool enabled = true);
bool slider(MenuNav& nav, int idx, Rectangle r, const std::string& label, double& value, double lo = 0, double hi = 1, bool percent = true);
bool toggle(MenuNav& nav, int idx, Rectangle r, const std::string& label, bool& value, const char* on = "ON", const char* off = "OFF");
bool cycle(MenuNav& nav, int idx, Rectangle r, const std::string& label, int& value, const std::vector<std::string>& options);
void title(const std::string& text, float x, float y, float size);
void logo(float x, float y, float scale, double time);

}  // namespace sky::ui
