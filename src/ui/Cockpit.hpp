// Virtual cockpit: 3D instrument panel, glare shield, consoles, moving stick and throttle,
// live multi-function displays (radar / attitude / engine) and the HUD combiner location.
#pragma once

#include "../combat/Combat.hpp"
#include "raylib.h"

namespace sky {

class Renderer;
struct FrameParams;

class Cockpit {
public:
    void init();
    void unload();
    /// Redraw the MFD render textures (call outside any other texture mode).
    void updateDisplays(const Battle& b, const Jet& player, double time, bool missionMode);
    /// Draw the 3D cockpit in the overlay pass (body frame of the player aircraft).
    void draw3D(Renderer& r, const Jet& player, const FrameParams& f, Vec3 origin = Vec3{});
    /// Screen-space rectangle of the HUD combiner glass.
    Rectangle combinerRect(const Camera3D& cam, const Jet& player, int w, int h) const;
    Vector3 eyeOffset() const { return eye_; }
    void setEye(Vector3 e) { eye_ = e; }
    void debugExport(const char* dir) const;

private:
    void drawRadar(const Battle& b, const Jet& p);
    void drawPfd(const Jet& p);
    void drawEngine(const Jet& p, double time);
    void drawUfc(const Jet& p, double time);
    void drawCni(const Jet& p, double time);
    Mesh panel_{};
    Mesh stick_{}, throttle_{};
    RenderTexture2D mfd_[5]{};   // 0 TSD radar, 1 PFD, 2 engine, 3 up-front keypad, 4 CNI text
    Vector3 eye_{4.28f, 0.96f, 0.0f};
    bool ready_ = false;
};

}  // namespace sky
